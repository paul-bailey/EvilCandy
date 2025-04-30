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

        /* delete old entry */
        bug_on(va->items[i] == NULL);
        VAR_DECR_REF(va->items[i]);

        va->items[i] = child;
        VAR_INCR_REF(child);
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
        struct arrayvar_t *h = V2ARR(array);
        size_t size = seqvar_size(array);

        if (h->lock) {
                err_locked();
                return RES_ERROR;
        }

        array_resize(array, size + 1);
        h->items[size] = child;

        seqvar_set_size(array, size + 1);
        VAR_INCR_REF(child);
        return RES_OK;
}

static Object *
arrayvar_new_common(int n_items, struct type_t *type)
{
        int i;
        Object *array = var_new(type);
        struct arrayvar_t *va = V2ARR(array);
        va->alloc_size = 0;
        va->items = NULL;

        array_resize(array, n_items);
        seqvar_set_size(array, n_items);
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
Object *
arrayvar_new(int n_items)
{
        return arrayvar_new_common(n_items, &ArrayType);
}

/**
 * tuplevar_new - Create a new tuple of size @n_items
 * Return: new tuple.  Each slot is filled with NullVar.
 */
Object *
tuplevar_new(int n_items)
{
        return arrayvar_new_common(n_items, &TupleType);
}

/**
 * tuple_validate - Ensure a certain tuple length
 *                  and arrangement of contents.
 * @tup: Tuple to validate
 * @descr: Description of contents, explained below.
 * @map_function: If true, then 'x' in @descr could be either for a
 *              function or for a method object.  If false, then 'x' is
 *              strictly for functions.
 *
 * Return: RES_OK if contents match, RES_ERROR if either the tuple size
 *      is not the length of @descr or if any of its contents do not
 *      match.
 *
 * tuples make for some useful pseudo-class objects.  tuple_validate can
 * be used to make sure its contents are the right type in the right order.
 *
 * As a general rule, upper-case letters in @descr are for internal-use
 * types or types the user does not normally deal with, while lower-case
 * letters are for types that user is most involved with.  Specifically,
 * @descr must contain a sequence of the following letters:
 *      letter:      Type:
 *      -------      -----
 *        *         wildcard (any type is valid)
 *        F         Filetype
 *        U         UuidptrType
 *        X         XptrType
 *        a         ArrayType
 *        b         BytesType
 *        d         DictType
 *        e         EmptyType (ie NullVar)
 *        f         FloatType
 *        i         IntType
 *        m         MethodType
 *        r         RangeType
 *        s         StringType
 *        x         FunctionType
 */
enum result_t
tuple_validate(Object *tup, const char *descr, bool map_function)
{
        Object **data;
        if (!isvar_tuple(tup))
                goto nope;
        if (seqvar_size(tup) != strlen(descr))
                goto nope;

        data = tuple_get_data(tup);
        while (*descr) {
                struct type_t *check;
                switch (*descr) {
                case '*':
                        check = NULL;
                        break;
                case 'F':
                        check = &FileType;
                        break;
                case 'U':
                        check = &UuidptrType;
                        break;
                case 'X':
                        check = &XptrType;
                        break;
                case 'a':
                        check = &ArrayType;
                        break;
                case 'b':
                        check = &BytesType;
                        break;
                case 'd':
                        check = &DictType;
                        break;
                case 'e':
                        check = &EmptyType;
                        break;
                case 'f':
                        check = &FloatType;
                        break;
                case 'i':
                        check = &IntType;
                        break;
                case 'm':
                        check = &MethodType;
                        break;
                case 'r':
                        check = &RangeType;
                        break;
                case 's':
                        check = &StringType;
                        break;
                case 'x':
                        check = &FunctionType;
                        break;
                default:
                        check = NULL;
                        bug();
                }
                if (check && (*data)->v_type != check) {
                        if (!map_function)
                                goto nope;
                        if (*descr != 'x')
                                goto nope;
                        if ((*data)->v_type != &MethodType)
                                goto nope;
                }
                descr++;
                data++;
        }
        return RES_OK;

nope:
        return RES_ERROR;
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
        int i, n = seqvar_size(a);
        Object **aitems = V2ARR(a)->items;
        Object **bitems = V2ARR(b)->items;
        if (n > seqvar_size(b))
                n = seqvar_size(b);
        for (i = 0; i < n; i++) {
                int x = var_compare(aitems[i], bitems[i]);
                if (x)
                        return x;
        }
        return seqvar_size(a) - seqvar_size(b);
}

/* helper to array_cat and tuple_cat */
static Object *
array_cat_common(Object *a, Object *b, struct type_t *type)
{
        size_t size_a, size_b;
        Object **ppa, **ppb, **ppc, *c;

        if (!b)
                return arrayvar_new_common(0, type);

        size_a = seqvar_size(a);
        size_b = seqvar_size(b);
        ppa = V2ARR(a)->items;
        ppb = V2ARR(b)->items;
        c = arrayvar_new_common(size_a + size_b, type);
        ppc = V2ARR(c)->items;
        if (size_a) {
                memcpy(ppc, ppa, size_a * sizeof(Object *));
                ppc += size_a;
        }

        if (size_b)
                memcpy(ppc, ppb, size_b * sizeof(Object *));

        return c;
}

/* implement concatenation of a + b */
static Object *
array_cat(Object *a, Object *b)
{
        return array_cat_common(a, b, &ArrayType);
}

/* implement concatenation of a + b */
static Object *
tuple_cat(Object *a, Object *b)
{
        return array_cat_common(a, b, &TupleType);
}

/* type_t .str callbacks for array and tuple */
static Object *
array_or_tuple_str(Object *t, int startchar)
{
        struct buffer_t b;
        Object *ret;
        size_t i, n = seqvar_size(t);
        buffer_init(&b);
        buffer_putc(&b, startchar);

        for (i = 0; i < n; i++) {
                Object *item;
                if (i > 0)
                        buffer_puts(&b, ", ");
                item = var_str(V2ARR(t)->items[i]);
                buffer_puts(&b, string_get_cstring(item));
                VAR_DECR_REF(item);
        }
        buffer_putc(&b, startchar == '(' ? ')' : ']');
        ret = stringvar_from_buffer(&b);
        return ret;
}

static Object *
array_str(Object *a)
{
        return array_or_tuple_str(a, '[');
}

static Object *
tuple_str(Object *t)
{
        return array_or_tuple_str(t, '(');
}

/* implement 'x.len()' */
static Object *
do_array_len(Frame *fr)
{
        Object *self = get_this(fr);
        if (arg_type_check(self, &ArrayType) == RES_ERROR)
                return ErrorVar;
        return intvar_new(seqvar_size(self));
}

static Object *
do_tuple_len(Frame *fr)
{
        Object *self = get_this(fr);
        if (arg_type_check(self, &TupleType) == RES_ERROR)
                return ErrorVar;
        return intvar_new(seqvar_size(self));
}

/* implement 'x.foreach(myfunc, mypriv)' */
static Object *
array_tuple_foreach_common(Frame *fr, struct type_t *type)
{
        Object *self, *func, *priv, *argv[3];
        unsigned int idx, lock;
        struct arrayvar_t *h;
        int status = RES_OK;

        self = get_this(fr);
        if (arg_type_check(self, type) == RES_ERROR)
                return ErrorVar;
        /*
         * If 'func' is not function, it could still be callable,
         * so do not use arg_type_check() for it.
         */
        func = frame_get_arg(fr, 0);
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
                Object *retval;

                argv[0] = h->items[idx];
                argv[1] = intvar_new(idx);
                argv[2] = priv;

                retval = vm_exec_func(fr, func, NULL, 3, argv, false);
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

static Object *
do_array_foreach(Frame *fr)
{
        return array_tuple_foreach_common(fr, &ArrayType);
}

static Object *
do_tuple_foreach(Frame *fr)
{
        return array_tuple_foreach_common(fr, &TupleType);
}

/* implement 'x.append(y)' */
static Object *
do_array_append(Frame *fr)
{
        Object *self, *arg;
        self = get_this(fr);
        arg = vm_get_arg(fr, 0);

        /* array only, tuples are read-only... */
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

static const struct type_inittbl_t array_cb_methods[] = {
        V_INITTBL("append",     do_array_append,   1, 1),
        V_INITTBL("len",        do_array_len,      0, 0),
        V_INITTBL("foreach",    do_array_foreach,  1, 2),
        V_INITTBL("allocated",  do_array_allocated, 0, 0),
        TBLEND,
};

static const struct seq_methods_t array_seq_methods = {
        .getitem        = array_getitem,
        .setitem        = array_setitem,
        .hasitem        = array_hasitem,
        .cat            = array_cat,
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
        V_INITTBL("len",        do_tuple_len,           0, 0),
        V_INITTBL("foreach",    do_tuple_foreach,       1, 2),
        TBLEND,
};

static const struct seq_methods_t tuple_seq_methods = {
        .getitem        = array_getitem,
        .setitem        = NULL,
        .hasitem        = array_hasitem,
        .cat            = tuple_cat,
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
