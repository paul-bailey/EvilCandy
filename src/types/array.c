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

#define V2ARR(v_)       ((struct arrayvar_t *)(v_))
#define V2SQ(v_)        ((struct seqvar_t *)(v_))

static void
array_resize(Object *array, int n_items)
{
        struct arrayvar_t *va = V2ARR(array);
        size_t new_size = va->alloc_size;
        size_t needsize = n_items * sizeof(Object *);

        if (needsize == 0) {
                /* 1-element size, just so we don't have aNULL .items */
                new_size = sizeof(Object *);
                goto resize;
        }

        /* 0 times X is still 0, would spinlock algorithm below */
        if (new_size == 0)
                new_size = sizeof(Object *);

        /*
         * Similar to our hash table algorithm, except we don't need to
         * worry about collisions, so alpha=.75 is fine.  This causes
         * amortization so that we aren't resizing all the time.
         */
        while (((new_size * 3) / 4) <= needsize)
                new_size *= 2;

        if (new_size != va->alloc_size)
                goto resize;

        /* maybe shrink, do so if <50% */
        while ((new_size / 4) > needsize)
                new_size /= 4;

        if (new_size == va->alloc_size)
                return;

resize:
        bug_on(needsize > new_size);
        va->alloc_size = new_size;
        va->items = erealloc(va->items, new_size);
}

/**
 * array_getitem - seq_methods_t .getitem callback
 *
 * Given extern linkage since some internal code needs it
 */
Object *
array_getitem(Object *array, int idx)
{
        struct arrayvar_t *va = V2ARR(array);

        bug_on(idx >= seqvar_size(array));
        VAR_INCR_REF(va->items[idx]);
        return va->items[idx];
}

/**
 * like array_getitem, but do not consume reference
 */
Object *
array_borrowitem(Object *array, int idx)
{
        Object *ret = array_getitem(array, idx);
        VAR_DECR_REF(ret);
        return ret;
}

/**
 * array_reverse - Reverse the order of an array
 */
void
array_reverse(Object *array)
{
        Object **low = array_get_data(array);
        Object **high = &low[seqvar_size(array) - 1];
        while (low < high) {
                Object *tmp = *high;
                *high = *low;
                *low = tmp;
                high--;
                low++;
        }
}

static enum result_t
array_insert_chunk(Object *array, int at,
                   Object **children, size_t n_items)
{
        struct arrayvar_t *h = V2ARR(array);
        size_t i, size = seqvar_size(array);

        /* FIXME: move lock check to calling funcs */
        if (h->lock) {
                err_locked();
                return RES_ERROR;
        }

        array_resize(array, size + n_items);

        if (size > at) {
                Object **src = &h->items[at];
                Object **dst = &h->items[at + n_items];
                size_t movsize = (size - at) * sizeof(Object *);
                memmove(dst, src, movsize);
        }

        for (i = 0; i < n_items; i++) {
                bug_on(at + i >= seqvar_size(array) + n_items);
                VAR_INCR_REF(children[i]);
                h->items[at + i] = children[i];
        }

        seqvar_set_size(array, size + n_items);
        return RES_OK;
}

static enum result_t
array_delete_chunk(Object *array, int at, size_t n_items)
{
        struct arrayvar_t *h = V2ARR(array);
        ssize_t i, size = seqvar_size(array);
        ssize_t movsize;

        bug_on(!isvar_array(array));

        if (at >= size)
                return RES_OK;
        if (size - at < n_items)
                n_items = size - at;

        for (i = at; i < at + n_items; i++)
                VAR_DECR_REF(h->items[i]);

        movsize = (size - (at + n_items)) * sizeof(Object *);
        if (movsize > 0)
                memmove(&h->items[at], &h->items[at + n_items], movsize);

        seqvar_set_size(array, size - n_items);
        array_resize(array, size - n_items);
        return RES_OK;
}

/* comparisons, helpers to array_getslice */
static bool slice_cmp_lt(int a, int b) { return a < b; }
static bool slice_cmp_gt(int a, int b) { return a > b; }

static Object *
array_getslice(Object *obj, int start, int stop, int step)
{
        Object *ret, **src;
        bool (*cmp)(int, int);
        size_t n;

        bug_on(!isvar_array(obj));

        ret = arrayvar_new(0);

        n = seqvar_size(obj);
        if (start >= n)
                start = n - 1;
        bug_on(start < 0);

        if (start == stop)
                return ret;

        cmp = start < stop ? slice_cmp_lt : slice_cmp_gt;
        src = array_get_data(obj);

        while (cmp(start, stop)) {
                /*
                 * XXX: there is no 'tuple_append',
                 * and for good reason.
                 */
                array_append(ret, src[start]);
                start += step;
        }
        return ret;
}

static Object **
slice_src(Object *x)
{
        if (isvar_array(x)) {
                return array_get_data(x);
        } else if (isvar_tuple(x)) {
                return tuple_get_data(x);
        } else {
                err_setstr(TypeError,
                           "Cannot set list slice from type %s",
                           typestr(x));
                return NULL;
        }
}

static enum result_t
array_setslice_1step(Object *obj, int start,
                        int stop, int step, Object *val)
{
        Object **src, **dst;
        ssize_t nslc, nsrc, ndiff, i, k;

        bug_on(!isvar_array(obj));

        dst = array_get_data(obj);
        src = slice_src(val);
        if (!src)
                return RES_ERROR;

        bug_on(!isvar_seq(val));

        if (step < 0) {
                int tmp = stop + 1;
                stop = start + 1;
                start = tmp;
                step = -step;
        }

        nslc = stop - start;
        /* could happend if [length(obj):] */
        if (nslc < 0)
                nslc = 0;
        nsrc = seqvar_size(val);
        ndiff = nsrc - nslc;
        k = ndiff >= 0 ? nslc : nsrc;
        for (i = 0; i < k; i++) {
                VAR_DECR_REF(dst[i + start]);
                dst[i + start] = VAR_NEW_REF(src[i]);
        }
        if (ndiff > 0) {
                array_insert_chunk(obj, start + k,
                                   &src[k], ndiff);
        } else if (ndiff < 0) {
                array_delete_chunk(obj, start + k, -ndiff);
        }

        return RES_OK;
}

static enum result_t
array_setslice(Object *obj, int start, int stop, int step, Object *val)
{
        size_t selfsize;

        bug_on(!isvar_array(obj));

        selfsize = seqvar_size(obj);
        if (step == 1 || step == -1) {
                if (val) {
                        return array_setslice_1step(obj, start,
                                                    stop, step, val);
                } else {
                        if (step == -1) {
                                size_t tmp = stop + 1;
                                stop = start + 1;
                                start = tmp;
                                step = -step;
                        }
                        array_delete_chunk(obj, start, stop - start);
                        return RES_OK;
                }
        }

        if (!val) {
                /* delete slice */
                if (step < 0) {
                        size_t slclen = var_slice_size(start, stop, step);
                        stop = start + 1;
                        start = stop - 1 + step * (slclen - 1);
                        step = -step;
                }
                bug_on(start < 0 || stop < 0);
                if (stop > selfsize)
                        stop = selfsize;

                /*
                 * XXX REVISIT: Lots of memmoves, probably lots of reallocs.
                 * Quicker to create a new Object array and put the inverse
                 * of slice into new one.  See floats.c for ex.
                 */
                while (start < stop) {
                        array_delete_chunk(obj, start, 1);
                        start += step - 1;
                        stop--;
                }
                return RES_OK;
        } else {
                /* insert/add slice */
                Object **dst, **src;
                ssize_t slclen, i;

                if (stop >= selfsize)
                        stop = selfsize;

                dst = array_get_data(obj);
                if ((src = slice_src(val)) == NULL)
                        return RES_ERROR;

                bug_on(!isvar_seq(val));

                slclen = var_slice_size(start, stop, step);
                if (start >= selfsize || slclen != seqvar_size(val)) {
                        err_setstr(ValueError,
                                   "Cannot extend slice for step=%d",
                                   step);
                        return RES_ERROR;
                }

                for (i = 0; i < slclen; start += step, i++) {
                        bug_on(step < 0 && start <= stop);
                        bug_on(step > 0 && start >= stop);
                        VAR_DECR_REF(dst[start]);
                        VAR_INCR_REF(src[i]);
                        dst[start] = src[i];
                }

                return RES_OK;
        }
}

/* helper to array_sort */
static int
array_sort_cmp(const void *a, const void *b)
{
        return var_compare(*(Object **)a, *(Object **)b);
}

/* seq_methods_t .sort callback */
static void
array_sort(Object *array)
{
        struct arrayvar_t *a = V2ARR(array);
        if (seqvar_size(array) < 2)
                return;
        bug_on(!a->items);
        qsort(a->items, seqvar_size(array),
              sizeof(Object *), array_sort_cmp);
}

/**
 * array_setitem - Callback for sequence .setitem method
 * @array: Array to set an item in.
 * @i:     Index into the array to set the item
 * @child: New item to set into @array.  WARNING!! DO NOT let @child be NULL.
 *
 * Has extern linkage since some internal code needs it.
 */
enum result_t
array_setitem(Object *array, int i, Object *child)
{
        struct arrayvar_t *va = V2ARR(array);
        bug_on(!isvar_array(array) && !isvar_tuple(array));

        bug_on(i >= seqvar_size(array));

        if (child) {
                /* delete old entry */
                bug_on(va->items[i] == NULL);
                VAR_DECR_REF(va->items[i]);

                va->items[i] = child;
                VAR_INCR_REF(child);
        } else {
                return array_delete_chunk(array, i, 1);
        }
        return RES_OK;
}

static bool
array_hasitem(Object *array, Object *item)
{
        size_t i, n;
        Object **data;

        bug_on(!isvar_array(array) && !isvar_tuple(array));
        n = seqvar_size(array);
        data = array_get_data(array);

        for (i = 0; i < n; i++) {
                if (var_compare(data[i], item) == 0)
                        return true;
        }
        return false;
}

/**
 * array_append - Append an item to the tail of an array
 * @array: Array to append to
 * @child: Item to append to array WARNING!! DO NOT let @child be NULL.
 *
 * Has extern linkage since some internal code needs it.
 */
enum result_t
array_append(Object *array, Object *child)
{
        return array_insert_chunk(array, seqvar_size(array), &child, 1);
}

static Object *
arrayvar_new_common(int n_items, Object **src, bool consume)
{
        int i;
        Object *array = var_new(&ArrayType);
        struct arrayvar_t *va = V2ARR(array);
        va->alloc_size = 0;
        va->items = NULL;

        bug_on(consume && src == NULL);

        array_resize(array, n_items);
        seqvar_set_size(array, n_items);
        for (i = 0; i < n_items; i++) {
                Object *item = src ? src[i] : NullVar;
                /* effectively the same thing as consume */
                if (!consume)
                        VAR_INCR_REF(item);
                va->items[i] = item;
        }
        return array;
}

/**
 * arrayvar_new - Create a new array of size @n_items
 *
 * Return: new array.  Each slot is filled with NullVar.
 */
Object *
arrayvar_new(int n_items)
{
        return arrayvar_new_common(n_items, NULL, false);
}

/**
 * arrayvar_from_stack - Create a new array from the stack
 * @items: Pointer into the stack.  It doesn't have to be *the* stack,
 *         but the name implies that this array of pointers will be
 *         copied into a new array.
 * @n_items: Number of @items to add
 * @consume: True to consume references to @items.
 */
Object *
arrayvar_from_stack(Object **items, int n_items, bool consume)
{
        return arrayvar_new_common(n_items, items, consume);
}

/* type_t .reset callback */
static void
array_reset(Object *a)
{
        if (V2ARR(a)->items) {
                int i;
                Object **data = array_get_data(a);
                for (i = 0; i < seqvar_size(a); i++) {
                        VAR_DECR_REF(data[i]);
                }
                efree(V2ARR(a)->items);
        }
}

/* type_t .cmp callback */
static int
array_cmp(Object *a, Object *b)
{
        int i, res, n;
        Object **aitems, **bitems;

        RECURSION_DECLARE_FUNC();
        RECURSION_START_FUNC(RECURSION_MAX);

        aitems = V2ARR(a)->items;
        bitems = V2ARR(b)->items;
        n = seqvar_size(a);
        if (n > seqvar_size(b))
                n = seqvar_size(b);

        /*
         * XXX: slow policy here: We don't bail early if sizes don't
         * match, because we want to check if internals have any
         * mismatch and return that instead of length(a)-length(b).
         */
        for (i = 0; i < n; i++) {
                res = var_compare(aitems[i], bitems[i]);
                if (res)
                        break;
        }

        if (i == n)
                res = seqvar_size(a) - seqvar_size(b);

        RECURSION_END_FUNC();

        return res;
}

/* implement concatenation of a + b */
static Object *
array_cat(Object *a, Object *b)
{
        size_t size_a, size_b, i;
        Object **ppa, **ppb, **ppc, *c;

        if (!b)
                return arrayvar_new(0);

        size_a = seqvar_size(a);
        size_b = seqvar_size(b);
        ppa = V2ARR(a)->items;
        ppb = V2ARR(b)->items;
        c = arrayvar_new_common(size_a + size_b, NULL, false);
        ppc = V2ARR(c)->items;
        for (i = 0; i < size_a; i++) {
                VAR_DECR_REF(ppc[i]);
                VAR_INCR_REF(ppa[i]);
                ppc[i] = ppa[i];
        }

        for (i = 0; i < size_b; i++) {
                VAR_DECR_REF(ppc[i+size_a]);
                VAR_INCR_REF(ppb[i]);
                ppc[i + size_a] = ppb[i];
        }

        return c;
}

/* type_t .str callbacks for array and tuple */
static Object *
array_str(Object *t)
{
        RECURSION_DECLARE_FUNC();
        RECURSION_START_FUNC(RECURSION_MAX);

        struct buffer_t b;
        Object *ret;
        size_t i, n = seqvar_size(t);
        buffer_init(&b);
        buffer_putc(&b, '[');

        for (i = 0; i < n; i++) {
                Object *item;
                if (i > 0)
                        buffer_puts(&b, ", ");
                item = var_str(V2ARR(t)->items[i]);
                buffer_puts(&b, string_cstring(item));
                VAR_DECR_REF(item);
        }

        buffer_putc(&b, ']');
        ret = stringvar_from_buffer(&b);

        RECURSION_END_FUNC();

        return ret;
}

/* implement 'x.append(y)' */
static Object *
do_array_append(Frame *fr)
{
        Object *self, *arg;
        self = get_this(fr);
        arg = vm_get_arg(fr, 0);

        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;

        if (!arg) {
                /* ugh, I know what it means */
                err_argtype("item");
                return ErrorVar;
        }
        array_append(self, arg);
        return NULL;
}

/* implement 'x.copy()' */
static Object *
do_array_copy(Frame *fr)
{
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;
        return array_getslice(self, 0, seqvar_size(self), 1);
}

static Object *
array_getprop_length(Object *self)
{
        bug_on(!isvar_array(self));
        return intvar_new(seqvar_size(self));
}

/*
 * array.allocated() - Return number of actual slots available in the
 *                     array's memory.
 * This should always return a value greater than or equal to the return
 * value of array.len().  This is used for debugging the efficiency of
 * array_resize() above.  We don't want to trigger too many calls
 * to realloc(), but we don't want to have a ton of wasted real-estate
 * in RAM.
 */
static Object *
do_array_allocated(Frame *fr)
{
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;
        return intvar_new(V2ARR(self)->alloc_size
                          / sizeof(Object *));
}

static Object *
do_array_clear(Frame *fr)
{
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;
        if (array_delete_chunk(self, 0, seqvar_size(self)) != RES_OK)
                return ErrorVar;
        return NULL;
}

static Object *
do_array_extend(Frame *fr)
{
        Object *arg, *self, *ext, *ret;

        self = vm_get_this(fr);
        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;

        arg = vm_get_arg(fr, 0);
        bug_on(!arg);

        if (isvar_dict(arg)) {
                ext = dict_keys(arg, true);
        } else {
                ext = arg;
                VAR_INCR_REF(ext);
        }

        ret = NULL;
        if (!isvar_seq(ext)) {
                err_setstr(TypeError, "Expected: sequential object");
                ret = ErrorVar;
                goto out;
        }

        if (array_insert_chunk(self, seqvar_size(self),
                               array_get_data(ext),
                               seqvar_size(ext)) != RES_OK) {
                ret = ErrorVar;
        }

out:
        VAR_DECR_REF(ext);
        return ret;
}

static Object *
do_array_remove(Frame *fr)
{
        Object *arg, *self, **data;
        int i, n;

        self = vm_get_this(fr);
        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;

        arg = vm_get_arg(fr, 0);
        bug_on(!arg);

        data = array_get_data(self);

        n = seqvar_size(self);
        for (i = 0; i < n; i++) {
                if (var_compare(data[i], arg) == 0) {
                        if (array_delete_chunk(self, i, 1) != RES_OK) {
                                bug_on(!err_occurred());
                                return ErrorVar;
                        }
                        return NULL;
                }
        }
        err_setstr(ValueError, "item not in list");
        return ErrorVar;
}

static Object *
do_array_pop(Frame *fr)
{
        Object *arg, *self;
        int at;

        self = vm_get_this(fr);
        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;

        if ((arg = vm_get_arg(fr, 0)) != NULL) {
                if (seqvar_arg2idx(self, arg, &at) == RES_ERROR)
                        return ErrorVar;
        } else {
                at = 0;
        }

        if (array_delete_chunk(self, at, 1) == RES_ERROR)
                return ErrorVar;

        return NULL;
}

static Object *
do_array_insert(Frame *fr)
{
        Object *idxarg, *valarg, *self;
        int at;

        self = vm_get_this(fr);
        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;

        idxarg = vm_get_arg(fr, 0);
        valarg = vm_get_arg(fr, 1);
        bug_on(!valarg || !idxarg);

        if (arg_type_check(idxarg, &IntType) == RES_ERROR)
                return ErrorVar;

        at = intvar_toi(idxarg);
        if (err_occurred())
                return ErrorVar;

        if (at > seqvar_size(self)) {
                /*
                 * XXX REVISIT: Here I'm doing what Python does, quietly
                 * change the argument instead of throw an error.  I'm
                 * not so sure I like that.
                 */
                at = seqvar_size(self);
        }

        if (array_insert_chunk(self, at, &valarg, 1) == RES_ERROR)
                return ErrorVar;
        return NULL;
}

static Object *
do_array_index(Frame *fr)
{
        Object *self, *xarg, *startarg, **data;
        int i, start, stop;

        self = vm_get_this(fr);
        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;

        start = 0;
        stop = seqvar_size(self);

        xarg = vm_get_arg(fr, 0);
        bug_on(!xarg);
        startarg = vm_get_arg(fr, 1);
        if (startarg) {
                Object *stoparg;

                if (seqvar_arg2idx(self, startarg, &start) != RES_OK)
                        return ErrorVar;
                stoparg = vm_get_arg(fr, 2);
                if (stoparg) {
                        if (seqvar_arg2idx(self, stoparg, &stop) != RES_OK)
                                return ErrorVar;
                }
        }

        data = array_get_data(self);
        for (i = start; i < stop; i++) {
                if (var_compare(xarg, data[i]) == 0)
                        return intvar_new(i);
        }

        err_setstr(ValueError, "item not in list");
        return ErrorVar;
}

static Object *
do_array_count(Frame *fr)
{
        Object *self, *xarg, **data;
        int i, n, count;

        self = vm_get_this(fr);
        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;

        xarg = vm_get_arg(fr, 0);
        bug_on(!xarg);

        n = seqvar_size(self);
        data = array_get_data(self);
        count = 0;
        for (i = 0; i < n; i++) {
                if (var_compare(xarg, data[i]) == 0)
                        count++;
        }
        return intvar_new(count);
}

static Object *
do_array_reverse(Frame *fr)
{
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;

        array_reverse(self);
        return NULL;
}

static Object *
array_create(Frame *fr)
{
        Object *arg, *args;

        args = vm_get_arg(fr, 0);
        bug_on(!args || !isvar_array(args));
        if (seqvar_size(args) != 1) {
                err_setstr(ArgumentError,
                           "Expected 1 arg but got %d",
                           seqvar_size(args));
                return ErrorVar;
        }

        arg = array_borrowitem(args, 0);
        if (!isvar_seq(arg) && !isvar_dict(arg)) {
                err_setstr(TypeError, "Invalid type '%s' for list()",
                           typestr(arg));
                return ErrorVar;
        }
        return var_listify(arg);
}

static const struct type_inittbl_t array_cb_methods[] = {
        V_INITTBL("allocated",  do_array_allocated,  0, 0, -1, -1),
        V_INITTBL("append",     do_array_append,     1, 1, -1, -1),
        V_INITTBL("clear",      do_array_clear,      0, 0, -1, -1),
        V_INITTBL("copy",       do_array_copy,       0, 0, -1, -1),
        V_INITTBL("count",      do_array_count,      1, 1, -1, -1),
        V_INITTBL("extend",     do_array_extend,     1, 1, -1, -1),
        V_INITTBL("foreach",    var_foreach_generic, 1, 2, -1, -1),
        V_INITTBL("index",      do_array_index,      1, 3, -1, -1),
        V_INITTBL("insert",     do_array_insert,     2, 2, -1, -1),
        V_INITTBL("pop",        do_array_pop,        0, 1, -1, -1),
        V_INITTBL("remove",     do_array_remove,     1, 1, -1, -1),
        V_INITTBL("reverse",    do_array_reverse,    0, 0, -1, -1),
        /* TODO: sort */
        TBLEND,
};

static const struct seq_methods_t array_seq_methods = {
        .getitem        = array_getitem,
        .setitem        = array_setitem,
        .hasitem        = array_hasitem,
        .getslice       = array_getslice,
        .setslice       = array_setslice,
        .cat            = array_cat,
        .sort           = array_sort,
};

static const struct type_prop_t array_prop_getsets[] = {
        { .name = "length", .getprop = array_getprop_length, .setprop = NULL },
        { .name = NULL },
};

struct type_t ArrayType = {
        .flags = 0,
        .name = "list",
        .opm = NULL,
        .cbm = array_cb_methods,
        .mpm = NULL,
        .sqm = &array_seq_methods,
        .size = sizeof(struct arrayvar_t),
        .str = array_str,
        .cmp = array_cmp,
        .reset = array_reset,
        .prop_getsets = array_prop_getsets,
        .create = array_create,
};
