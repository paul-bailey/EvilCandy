/*
 * array.c - Code for managing numerical arrays and tuples
 *
 * Arrays are called "lists" in the documentation, since
 *      1) that's what Python calls them, so why not, and
 *      2) calling them "arrays" is misleading because it makes you think
 *         they are fast like C arrays.  They are not.
 *
 * But here I call them "arrays" because I started writing this
 * file before I thought things through.
 */
#include <evilcandy.h>

/**
 * struct arrayvar_t - Handle to a numerical array
 * @lock:    Lock to prevent add/remove during foreach
 * @items:   Array of pointers to variables stored in it
 *
 * Arrays and tuples share the same data struct.  Tuples do not need @lock,
 * but four bytes is not reason enough to invent a whole new data struct.
 */
struct arrayvar_t {
        struct seqvar_t base;
        int lock;
        struct var_t **items;
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
        struct arrayvar_t *va = V2ARR(array);

        bug_on(idx >= seqvar_size(array));
        VAR_INCR_REF(va->items[idx]);
        return va->items[idx];
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
        if (seqvar_size(array) < 2)
                return;
        bug_on(!a->items);
        qsort(a->items, seqvar_size(array),
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
        struct arrayvar_t *va = V2ARR(array);
        bug_on(!isvar_array(array) && !isvar_tuple(array));

        bug_on(i >= seqvar_size(array));

        /* delete old entry */
        bug_on(va->items[i] == NULL);
        VAR_DECR_REF(va->items[i]);

        va->items[i] = child;
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
        size_t size = seqvar_size(array);

        if (h->lock) {
                err_locked();
                return RES_ERROR;
        }

        h->items = erealloc(h->items, (size + 1) * sizeof(struct var_t *));
        h->items[size] = child;

        seqvar_set_size(array, size + 1);
        VAR_INCR_REF(child);
        return RES_OK;
}

static struct var_t *
arrayvar_new_common(int n_items, struct type_t *type)
{
        int i;
        struct var_t *array = var_new(type);
        struct arrayvar_t *va = V2ARR(array);
        size_t alloc_size = sizeof(struct var_t *) * n_items;
        if (!alloc_size)
                alloc_size = 1;

        seqvar_set_size(array, n_items);
        va->items = emalloc(alloc_size);
        for (i = 0; i < n_items; i++) {
                VAR_INCR_REF(NullVar);
                va->items[i] = NullVar;
        }
        return array;
}

/**
 * arrayvar_new - Create a new array of size @n_items
 *
 * Return: new array.  Each slot is filled with NullVar.
 */
struct var_t *
arrayvar_new(int n_items)
{
        return arrayvar_new_common(n_items, &ArrayType);
}

/**
 * tuplevar_new - Create a new tuple of size @n_items
 * Return: new tuple.  Each slot is filled with NullVar.
 */
struct var_t *
tuplevar_new(int n_items)
{
        return arrayvar_new_common(n_items, &TupleType);
}

/* type_t .reset callback */
static void
array_reset(struct var_t *a)
{
        efree(V2ARR(a)->items);
}

/* type_t .cmp callback */
static int
array_cmp(struct var_t *a, struct var_t *b)
{
        int i, n = seqvar_size(a);
        struct var_t **aitems = V2ARR(a)->items;
        struct var_t **bitems = V2ARR(b)->items;
        if (n > seqvar_size(b))
                n = seqvar_size(b);
        for (i = 0; i < n; i++) {
                int x = var_compare(aitems[i], bitems[i]);
                if (x)
                        return x;
        }
        return seqvar_size(a) - seqvar_size(b);
}

/* type_t .str callbacks for array and tuple */
static struct var_t *
array_or_tuple_str(struct var_t *t, int startchar)
{
        struct buffer_t b;
        struct var_t *ret;
        size_t i, n = seqvar_size(t);
        buffer_init(&b);
        buffer_putc(&b, startchar);

        for (i = 0; i < n; i++) {
                struct var_t *item;
                if (i > 0)
                        buffer_puts(&b, ", ");
                item = var_str(V2ARR(t)->items[i]);
                buffer_puts(&b, string_get_cstring(item));
                VAR_DECR_REF(item);
        }
        buffer_putc(&b, startchar == '(' ? ')' : ']');
        ret = stringvar_new(b.s);
        buffer_free(&b);
        return ret;
}

static struct var_t *
array_str(struct var_t *a)
{
        return array_or_tuple_str(a, '[');
}

static struct var_t *
tuple_str(struct var_t *t)
{
        return array_or_tuple_str(t, '(');
}

/* implement 'x.len()' */
static struct var_t *
do_array_len(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        bug_on(!isvar_array(self) && !isvar_tuple(self));
        return intvar_new(seqvar_size(self));
}

/* implement 'x.foreach(myfunc, mypriv)' */
static struct var_t *
do_array_foreach(struct vmframe_t *fr)
{
        struct var_t *self, *func, *priv, *argv[3];
        unsigned int idx, lock;
        struct arrayvar_t *h;
        int status = RES_OK;

        self = get_this(fr);
        func = frame_get_arg(fr, 0);
        bug_on(!isvar_array(self) && !isvar_tuple(self));
        if (!func) {
                err_argtype("function");
                return ErrorVar;
        }
        priv = frame_get_arg(fr, 1);
        if (!priv)
                priv = NullVar;
        h = V2ARR(self);
        if (!seqvar_size(self)) /* nothing to iterate over */
                goto out;

        /*
         * appends in the middle of a loop can cause spinlock or moved
         * arrays, so don't allow it.
         */
        lock = h->lock;
        h->lock = 1;

        for (idx = 0; idx < seqvar_size(self); idx++) {
                /*
                 * XXX creating a new intvar every time, maybe some
                 * back-door hacks to intvar should be allowed for
                 * just the files in this directory.
                 */
                struct var_t *retval;

                argv[0] = h->items[idx];
                argv[1] = intvar_new(idx);
                argv[2] = priv;

                retval = vm_exec_func(fr, func, NULL, 3, argv);
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
        /* array only, tuples are read-only... */
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
};

struct type_t ArrayType = {
        .name = "list",
        .opm = NULL,
        .cbm = array_cb_methods,
        .mpm = NULL,
        .sqm = &array_seq_methods,
        .size = sizeof(struct arrayvar_t),
        .str = array_str,
        .cmp = array_cmp,
        .reset = array_reset,
};

static const struct type_inittbl_t tuple_cb_methods[] = {
        V_INITTBL("len",        do_array_len,           0, 0),
        V_INITTBL("foreach",    do_array_foreach,       0, 0),
        TBLEND,
};

static const struct seq_methods_t tuple_seq_methods = {
        .getitem        = array_getitem,
        .setitem        = NULL,
        .cat            = NULL, /* TODO: this */
        .sort           = NULL,
};

struct type_t TupleType = {
        .name = "tuple",
        .opm = NULL,
        .cbm = tuple_cb_methods,
        .mpm = NULL,
        .sqm = &tuple_seq_methods,
        .size = sizeof(struct arrayvar_t),
        .str = tuple_str,
        .cmp = array_cmp,
        .reset = array_reset,
};
