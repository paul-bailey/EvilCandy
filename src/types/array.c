/*
 * array.c - Code for managing numerical arrays
 *
 * These are called "lists" in the documentation, since
 *      1) that's what Python calls them, so why not, and
 *      2) calling them "arrays" could mislead users into thinking
 *         these are fast in the way that C arrays are fast.
 *
 * But here I call them "arrays" because I started writing this
 * file before I thought things through. (LOL, cf. object.c)
 *
 * XXX REVISIT: Policy decision... Should I continue to enforce
 *      lists having all the same type of items?  JavaScript
 *      doesn't do that.  Neither does Python.  There are some
 *      actual advantages to NOT enforcing it.
 */
#include "types_priv.h"
#include <stdlib.h>
#include <limits.h>

/**
 * struct arrayvar_t - Handle to a numerical array
 * @lock:       lock to prevent add/remove during foreach
 * @children:   struct buffer_t containing the actual arrray
 */
struct arrayvar_t {
        struct seqvar_t base;
        int lock;
        struct buffer_t children;
};

#define V2ARR(v_)       ((struct arrayvar_t *)(v_))
#define V2SQ(v_)        ((struct seqvar_t *)(v_))

/**
 * array_getitem - seq_methods_t .getitem callback
 *
 * Given extern linkage since some internal code needs it
 */
struct var_t *
array_getitem(struct var_t *array, int idx)
{
        struct arrayvar_t *h = V2ARR(array);
        struct var_t **ppvar = (struct var_t **)h->children.s;

        bug_on(idx >= V2SQ(array)->v_size);
        VAR_INCR_REF(ppvar[idx]);
        return ppvar[idx];
}

/* helper to array_sort */
static int
array_sort_cmp(const void *a, const void *b)
{
        return var_compare(*(struct var_t **)a, *(struct var_t **)b);
}

/* seq_methods_t .sort callback */
static void
array_sort(struct var_t *array)
{
        struct arrayvar_t *a = V2ARR(array);
        if (V2SQ(array)->v_size < 2)
                return;
        bug_on(!a->children.s);
        qsort(a->children.s, V2SQ(array)->v_size,
              sizeof(struct var_t *), array_sort_cmp);
}

/**
 * array_setitem - Callback for sequence .setitem method
 * @array: Array to set an item in.
 * @i:     Index into the array to set the item
 * @child: New item to set into @array
 *
 * Has extern linkage since some internal code needs it.
 */
enum result_t
array_setitem(struct var_t *array, int i, struct var_t *child)
{
        struct var_t **ppvar;
        bug_on(!isvar_array(array));

        ppvar = (struct var_t **)(V2ARR(array)->children.s);
        bug_on(i >= V2SQ(array)->v_size);

        /* delete old entry */
        bug_on(ppvar[i] == NULL);
        VAR_DECR_REF(ppvar[i]);

        ppvar[i] = child;
        VAR_INCR_REF(child);
        return RES_OK;
}

/**
 * array_append - Append an item to the tail of an array
 * @array: Array to append to
 * @child: Item to append to array
 *
 * Has extern linkage since some internal code needs it.
 */
enum result_t
array_append(struct var_t *array, struct var_t *child)
{
        struct arrayvar_t *h = V2ARR(array);

        if (h->lock) {
                err_locked();
                return RES_ERROR;
        }

        V2SQ(array)->v_size++;
        /* XXX: poor amortization, maybe assert_array_pos instead */
        buffer_putd(&h->children, &child, sizeof(void *));
        VAR_INCR_REF(child);
        return RES_OK;
}

/**
 * type_t .len callback
 */
static size_t
array_len(struct var_t *array)
{
        bug_on(!isvar_array(array));
        return V2SQ(array)->v_size;
}

/**
 * arrayvar_new - Create a new array of size @n_items
 *
 * Return: new array.  Each slot is filled with NullVar.
 */
struct var_t *
arrayvar_new(int n_items)
{
        struct var_t *array = var_new(&ArrayType);
        V2SQ(array)->v_size = 0;
        buffer_init(&V2ARR(array)->children);
        /* TODO: Replace buffer operations with more efficient method */
        while (n_items-- > 0) {
                VAR_INCR_REF(NullVar);
                array_append(array, NullVar);
        }
        return array;
}

/* type_t .reset callback */
static void
array_reset(struct var_t *a)
{
        buffer_free(&(V2ARR(a)->children));
}

/* type_t .cmp callback */
static int
array_cmp(struct var_t *a, struct var_t *b)
{
        int i, n = V2SQ(a)->v_size;
        struct var_t **aitems = (struct var_t **)V2ARR(a)->children.s;
        struct var_t **bitems = (struct var_t **)V2ARR(b)->children.s;
        if (n > V2SQ(b)->v_size)
                n = V2SQ(b)->v_size;
        for (i = 0; i < n; i++) {
                int x = var_compare(aitems[i], bitems[i]);
                if (x)
                        return x;
        }
        return V2SQ(a)->v_size - V2SQ(b)->v_size;
}

/* type_t .cp callback */
static struct var_t *
array_cp(struct var_t *a)
{
        VAR_INCR_REF(a);
        return a;
}

/* implement 'x.len()' */
static struct var_t *
do_array_len(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        bug_on(!isvar_array(self));
        return intvar_new(V2SQ(self)->v_size);
}

/* implement 'x.foreach(myfunc, mypriv)' */
static struct var_t *
do_array_foreach(struct vmframe_t *fr)
{
        struct var_t *self, *func, *priv, *argv[3], **ppvar;
        unsigned int idx, lock, nmemb;
        struct arrayvar_t *h;
        int status = RES_OK;

        self = get_this(fr);
        func = frame_get_arg(fr, 0);
        bug_on(!isvar_array(self));
        if (!func) {
                err_argtype("function");
                return ErrorVar;
        }
        priv = frame_get_arg(fr, 1);
        if (!priv)
                priv = NullVar;
        h = V2ARR(self);
        if (!V2SQ(self)->v_size) /* nothing to iterate over */
                goto out;

        ppvar = (struct var_t **)h->children.s;

        /*
         * appends in the middle of a loop can cause spinlock or moved
         * arrays, so don't allow it.
         */
        lock = h->lock;
        h->lock = 1;

        nmemb = V2SQ(self)->v_size;
        for (idx = 0; idx < nmemb; idx++) {
                /*
                 * XXX creating a new intvar every time, maybe some
                 * back-door hacks to intvar should be allowed for
                 * just the files in this directory.
                 */
                struct var_t *retval;

                argv[0] = ppvar[idx];
                argv[1] = intvar_new(idx);
                argv[2] = priv;

                retval = vm_reenter(fr, func, NULL, 3, argv);
                VAR_DECR_REF(argv[1]);

                if (retval == ErrorVar) {
                        status = RES_ERROR;
                        break;
                }
                /* foreach throws away retval */
                if (retval)
                        VAR_DECR_REF(retval);
        }
        h->lock = lock;

out:
        return status == RES_OK ? NULL : ErrorVar;
}

/* implement 'x.append(y)' */
static struct var_t *
do_array_append(struct vmframe_t *fr)
{
        struct var_t *self, *arg;
        self = get_this(fr);
        arg = vm_get_arg(fr, 0);
        bug_on(!isvar_array(self));

        if (!arg) {
                /* ugh, I know what it means */
                err_argtype("item");
                return ErrorVar;
        }
        array_append(self, arg);
        return NULL;
}

static const struct type_inittbl_t array_cb_methods[] = {
        V_INITTBL("append",     do_array_append,   0, 0),
        V_INITTBL("len",        do_array_len,      0, 0),
        V_INITTBL("foreach",    do_array_foreach,  0, 0),
        TBLEND,
};

static const struct seq_methods_t array_seq_methods = {
        .getitem        = array_getitem,
        .setitem        = array_setitem,
        .cat            = NULL, /* TODO: this */
        .sort           = array_sort,
        .len            = array_len,
};

struct type_t ArrayType = {
        .name = "list",
        .opm = NULL,
        .cbm = array_cb_methods,
        .mpm = NULL,
        .sqm = &array_seq_methods,
        .size = sizeof(struct arrayvar_t),
        .cmp = array_cmp,
        .cp  = array_cp,
        .reset = array_reset,
};

