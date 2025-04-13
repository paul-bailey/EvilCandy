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
 * @nmemb:      Size of the array, in number of elements
 * @children:   struct buffer_t containing the actual arrray
 */
struct arrayvar_t {
        struct var_t base;
        int lock;
        unsigned int nmemb;
        struct buffer_t children;
};

#define V2ARR(v_)       ((struct arrayvar_t *)(v_))

/**
 * array_child - Get nth member of an array
 * @array: Array to seek
 * @idx:   Index into the array
 *
 * Return: child member, or NULL if idx out of bounds.
 *         This does not return ErrorVar because the call could just
 *         be a probe.
 *
 * Calling code needs to call VAR_INCR_REF if it uses it.
 * That's not done here.
 */
struct var_t *
array_child(struct var_t *array, int idx)
{
        struct arrayvar_t *h = V2ARR(array);
        struct var_t **ppvar = (struct var_t **)h->children.s;

        idx = index_translate(idx, h->nmemb);
        if (idx < 0)
                return NULL;

        return ppvar[idx];
}

static int
array_sort_cmp(const void *a, const void *b)
{
        return var_compare(*(struct var_t **)a, *(struct var_t **)b);
}

/**
 * array_sort - Sort the items in an array
 */
void
array_sort(struct var_t *array)
{
        struct arrayvar_t *a = V2ARR(array);
        if (a->nmemb < 2)
                return;
        bug_on(!a->children.s);
        qsort(a->children.s, a->nmemb,
              sizeof(struct var_t *), array_sort_cmp);
}

/**
 * array_insert - Insert @child into @idx and throw error if
 *                array out of bounds.
 * @array:      Array to operate on
 * @idx:        Index into the array
 * @child:      Variable storing the data to set in array
 *
 * Data is moved from @child to the array member, @child must still be
 * dealt with w/r/t garbage collection by calling code.
 */
enum result_t
array_insert(struct var_t *array, struct var_t *idx, struct var_t *child)
{
        struct var_t **ppvar;
        long long ill;
        int i;

        bug_on(!isvar_array(array));

        if (!isvar_int(idx)) {
                err_setstr(RuntimeError, "Array subscript must be integer");
                return RES_ERROR;
        }
        ill = intvar_toll(idx);
        if (ill < INT_MIN || ill > INT_MAX) {
                err_setstr(RuntimeError, "Array index out of bounds");
                return RES_ERROR;
        }

        if (V2ARR(array)->lock) {
                err_locked();
                return RES_ERROR;
        }

        ppvar = (struct var_t **)(V2ARR(array)->children.s);
        if ((i = index_translate((int)ill, V2ARR(array)->nmemb)) < 0)
                return RES_ERROR;

        /* delete old entry */
        bug_on(ppvar[i] == NULL);
        VAR_DECR_REF(ppvar[i]);

        ppvar[i] = child;
        VAR_INCR_REF(child);
        return RES_OK;
}

/**
 * array_append - Append a new element to an array
 * @array:  Array to add to
 * @child:  New element to put in array
 */
enum result_t
array_append(struct var_t *array, struct var_t *child)
{
        struct arrayvar_t *h = V2ARR(array);

        if (h->lock) {
                err_locked();
                return RES_ERROR;
        }

        h->nmemb++;
        /* XXX: poor amortization, maybe assert_array_pos instead */
        buffer_putd(&h->children, &child, sizeof(void *));
        VAR_INCR_REF(child);
        return RES_OK;
}

int
array_length(struct var_t *array)
{
        bug_on(!isvar_array(array));
        return V2ARR(array)->nmemb;
}

struct var_t *
arrayvar_new(void)
{
        struct var_t *array = var_new(&ArrayType);
        buffer_init(&V2ARR(array)->children);
        return array;
}

static void
array_reset(struct var_t *a)
{
        buffer_free(&(V2ARR(a)->children));
}

static int
array_cmp(struct var_t *a, struct var_t *b)
{
        int i, n = V2ARR(a)->nmemb;
        struct var_t **aitems = (struct var_t **)V2ARR(a)->children.s;
        struct var_t **bitems = (struct var_t **)V2ARR(b)->children.s;
        if (n > V2ARR(b)->nmemb)
                n = V2ARR(b)->nmemb;
        for (i = 0; i < n; i++) {
                int x = var_compare(aitems[i], bitems[i]);
                if (x)
                        return x;
        }
        return V2ARR(a)->nmemb - V2ARR(b)->nmemb;
}

static struct var_t *
array_cp(struct var_t *a)
{
        VAR_INCR_REF(a);
        return a;
}

static const struct operator_methods_t array_primitives = {
        /* To do, I may want to support some of these */
        .cmp = array_cmp,
        .cp  = array_cp,
        .reset = array_reset,
};

static struct var_t *
do_array_len(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        bug_on(!isvar_array(self));
        return intvar_new(V2ARR(self)->nmemb);
}

static struct var_t *
do_array_foreach(struct vmframe_t *fr)
{
        struct var_t *self, *func, *priv, *argv[3], **ppvar;
        unsigned int idx, lock;
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
        if (!h->nmemb) /* nothing to iterate over */
                goto out;

        ppvar = (struct var_t **)h->children.s;

        lock = h->lock;
        h->lock = 1;
        for (idx = 0; idx < h->nmemb; idx++) {
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
                if (retval == ErrorVar) {
                        status = RES_ERROR;
                        break;
                }
                /* foreach throws away retval */
                if (retval)
                        VAR_DECR_REF(retval);
                VAR_DECR_REF(argv[1]);
        }
        h->lock = lock;

        VAR_DECR_REF(argv[1]);
out:
        return status == RES_OK ? NULL : ErrorVar;
}

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

static const struct type_inittbl_t array_methods[] = {
        V_INITTBL("append",     do_array_append,   0, 0),
        V_INITTBL("len",        do_array_len,      0, 0),
        V_INITTBL("foreach",    do_array_foreach,  0, 0),
        TBLEND,
};

struct type_t ArrayType = {
        .name = "list",
        .opm = &array_primitives,
        .cbm = array_methods,
        .size = sizeof(struct arrayvar_t),
};

