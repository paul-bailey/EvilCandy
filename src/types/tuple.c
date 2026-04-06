/* tuple.c - Code for managing tuples */
#include <evilcandy.h>

#define V2TUP(v_)               ((struct tuplevar_t *)(v_))

/* **********************************************************************
 *              Type Methods
 ***********************************************************************/

static Object *
tuple_str(Object *t)
{
        RECURSION_DECLARE_FUNC();
        RECURSION_START_FUNC(RECURSION_MAX);

        Object *ret;
        size_t i, n = seqvar_size(t);
        struct string_writer_t wr;
        string_writer_init(&wr, 1);
        string_writer_append(&wr, '(');

        for (i = 0; i < n; i++) {
                Object *item;
                if (i > 0)
                        string_writer_appends(&wr, ", ");
                item = var_str(V2TUP(t)->items[i]);
                string_writer_append_strobj(&wr, item);
                VAR_DECR_REF(item);
        }

        /*
         * Print what we can read back as the same type.  In the case of
         * a tuple of size 1, parentheses around a single expression are
         * interpreted as just that expression. A comma between the
         * expression and closing parethesis ensures that it will be
         * interpreted as a tuple.
         */
        if (n == 1)
                string_writer_append(&wr, ',');

        string_writer_append(&wr, ')');
        ret = stringvar_from_writer(&wr);

        RECURSION_END_FUNC();

        return ret;
}

static bool
tuple_cmpeq(Object *a, Object *b)
{
        bool res;
        Object **aitems, **bitems;
        size_t i, n;

        bug_on(!isvar_tuple(a) || !isvar_tuple(b));

        n = seqvar_size(a);
        if (n != seqvar_size(b))
                return false;

        aitems = tuple_get_data(a);
        bitems = tuple_get_data(b);

        RECURSION_DECLARE_FUNC();
        RECURSION_START_FUNC(RECURSION_MAX);

        res = true;
        for (i = 0; i < n; i++) {
                res = var_matches(aitems[i], bitems[i]);
                if (!res)
                        break;
        }

        RECURSION_END_FUNC();

        return res;
}

static enum result_t
tuple_cmp(Object *a, Object *b, int *result)
{
        ssize_t i, n;
        enum result_t status;
        Object **aitems, **bitems;

        RECURSION_DECLARE_FUNC();
        RECURSION_START_FUNC(RECURSION_MAX);

        aitems = tuple_get_data(a);
        bitems = tuple_get_data(b);
        n = seqvar_size(a);
        if (n > seqvar_size(b))
                n = seqvar_size(b);

        /*
         * XXX: slow policy here: We don't bail early if sizes don't
         * match, because we want to check if internals have any
         * mismatch and return that instead of length(a)-length(b).
         */
        status = RES_OK;
        for (i = 0; i < n; i++) {
                status = var_compare(aitems[i], bitems[i], result);
                if (*result != 0 || status == RES_ERROR)
                        break;
        }

        if (i == n && status == RES_OK)
                *result = seqvar_size(a) - seqvar_size(b);

        RECURSION_END_FUNC();

        return status;
}

static void
tuple_reset(Object *tup)
{
        Object **data = tuple_get_data(tup);
        if (data) {
                int i, n = seqvar_size(tup);
                for (i = 0; i < n; i++)
                        VAR_DECR_REF(data[i]);
                efree(data);
        }
}


/* **********************************************************************
 *              Operator Methods
 ***********************************************************************/

static Object *
tuple_cat(Object *a, Object *b)
{
        size_t size_a, size_b, i;
        Object **ppa, **ppb, **ppc, *c;

        if (!b)
                return tuplevar_new(0);

        size_a = seqvar_size(a);
        size_b = seqvar_size(b);
        ppa = tuple_get_data(a);
        ppb = tuple_get_data(b);
        c = tuplevar_new(size_a + size_b);
        ppc = tuple_get_data(c);
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


/* comparisons, helpers to tuple_getslice */
static bool slice_cmp_lt(ssize_t a, ssize_t b) { return a < b; }
static bool slice_cmp_gt(ssize_t a, ssize_t b) { return a > b; }

static Object *
tuple_getslice(Object *obj, ssize_t start, ssize_t stop, ssize_t step)
{
        Object *ret, **src, **dst;
        bool (*cmp)(ssize_t, ssize_t);
        ssize_t dst_i, dst_n;

        bug_on(!isvar_tuple(obj));

        dst_n = var_slice_size(start, stop, step);
        bug_on(dst_n < 0);
        ret = tuplevar_new(dst_n);

        if (start == stop)
                return ret;

        cmp = start < stop ? slice_cmp_lt : slice_cmp_gt;
        src = tuple_get_data(obj);
        dst = tuple_get_data(ret);
        dst_i = 0;
        while (cmp(start, stop)) {
                VAR_DECR_REF(dst[dst_i]);
                dst[dst_i] = VAR_NEW_REF(src[start]);

                start += step;
                dst_i++;

                bug_on(dst_i > dst_n);
        }
        bug_on(dst_i != dst_n);
        return ret;
}


static bool
tuple_hasitem(Object *tup, Object *item)
{
        size_t i, n;
        Object **data;

        bug_on(!isvar_tuple(tup));
        n = seqvar_size(tup);
        data = tuple_get_data(tup);
        for (i = 0; i < n; i++) {
                if (var_matches(data[i], item))
                        return true;
        }
        return false;
}

/* Also an API func, so not static */
Object *
tuple_getitem(Object *tup, size_t idx)
{
        Object *item;

        bug_on(!isvar_tuple(tup));
        bug_on(idx >= seqvar_size(tup));

        item = tuple_borrowitem_(tup, idx);
        return VAR_NEW_REF(item);
}

/* Like tuple_getitem, but do not produce a reference */
Object *
tuple_borrowitem(Object *tup, size_t idx)
{
        Object *ret = tuple_getitem(tup, idx);
        if (ret)
                VAR_DECR_REF(ret);
        return ret;
}

/* **********************************************************************
 *              Built-in methods
 ***********************************************************************/

static Object *
do_tuple_index(Frame *fr)
{
        Object *self, *xarg, **data;
        ssize_t i, n, start, stop;

        self = vm_get_this(fr);
        if (arg_type_check(self, &TupleType) == RES_ERROR)
                return ErrorVar;

        n = seqvar_size(self);
        start = 0;
        stop = n;

        if (vm_getargs(fr, "[<*>|zz!]{!}:index", &xarg, &start, &stop)
            == RES_ERROR) {
                return ErrorVar;
        }

        var_index_capi(n, &start, &stop);

        data = tuple_get_data(self);
        for (i = start; i < stop; i++) {
                if (var_matches(xarg, data[i]))
                        return intvar_new(i);
        }

        err_setstr(ValueError, "item not in list");
        return ErrorVar;
}

static Object *
do_tuple_count(Frame *fr)
{
        Object *self, *xarg, **data;
        int i, n, count;

        self = vm_get_this(fr);
        if (arg_type_check(self, &TupleType) == RES_ERROR)
                return ErrorVar;

        if (vm_getargs(fr, "[<*>!]{!}:count", &xarg) == RES_ERROR)
                return ErrorVar;

        n = seqvar_size(self);
        data = tuple_get_data(self);
        count = 0;
        for (i = 0; i < n; i++) {
                if (var_matches(xarg, data[i]))
                        count++;
        }
        return intvar_new(count);
}


/* **********************************************************************
 *              Properties
 ***********************************************************************/

static Object *
tuple_getprop_length(Object *self)
{
        bug_on(!isvar_tuple(self));
        return intvar_new(seqvar_size(self));
}

/* **********************************************************************
 *              API functions & helpers
 ***********************************************************************/

static Object *
tuplevar_new_common(int n_items, Object **src, bool consume, bool allocate)
{
        int i;
        Object *tup = var_new(&TupleType);
        struct tuplevar_t *th = V2TUP(tup);

        seqvar_set_size(tup, n_items);

        if (allocate) {
                bug_on(consume && src == NULL);
                th->items = emalloc(sizeof(Object *) * n_items);

                for (i = 0; i < n_items; i++) {
                        Object *item = src ? src[i] : NullVar;
                        /* effectively the same thing as consume */
                        if (!consume)
                                VAR_INCR_REF(item);
                        th->items[i] = item;
                }
        } else {
                bug_on(!src);
                th->items = src;
        }
        return tup;
}


/**
 * tuplevar_from_stack - Create a new tuple from the stack
 * @items: Pointer into the stack.  It doesn't have to be *the* stack,
 *         but the name implies that this array of pointers will be
 *         copied into a new array.
 * @n_items: Number of @items to add
 * @consume: True to consume references to @items.
 */
Object *
tuplevar_from_stack(Object **items, int n_items, bool consume)
{
        return tuplevar_new_common(n_items, items, consume, true);
}

/**
 * tuplevar_new - Create a new tuple of size @n_items
 * Return: new tuple.  Each slot is filled with NullVar.
 */
Object *
tuplevar_new(int n_items)
{
        return tuplevar_new_common(n_items, NULL, false, true);
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
 *      match.  tuple_validate() will not set any exceptions.
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
                        /* FIXME: Be consistent and call this '/' */
                        if (isvar_file(*data))
                                check = NULL;
                        else
                                goto nope;

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

static Object *
tuple_create(Frame *fr)
{
        Object *arg, **data, *item, *it;
        size_t i, n;

        arg = NULL;
        if (vm_getargs(fr, "[|<*>!]:tuple", &arg) == RES_ERROR)
                return ErrorVar;
        if (!arg)
                return tuplevar_new(0);

        if ((it = iterator_errget(arg, "tuple")) == ErrorVar)
                return ErrorVar;

        n = seqvar_size(arg);
        if (n == 0) {
                VAR_DECR_REF(it);
                return tuplevar_new(0);
        }

        data = emalloc(sizeof(Object *) * n);

        i = 0;
        ITERATOR_FOREACH(item, it) {
                bug_on(i >= n);
                data[i++] = item;
        }
        VAR_DECR_REF(it);
        if (item == ErrorVar)
                return ErrorVar;

        bug_on(i != n);
        return tuplevar_new_common(n, data, true, false);
}

static hash_t
tuple_calc_hash(Object *tup)
{
        Object **data = tuple_get_data(tup);
        size_t i, n = seqvar_size(tup);

        hash_t hash = n;
        for (i = 0; i < n; i++) {
                Object *v = data[i];
                if (!v->v_type->hash)
                        return HASH_ERROR;
                hash += v->v_type->hash(v);
        }
        return fnv_hash(&hash, sizeof(hash));
}

static hash_t
tuple_hash(Object *tup)
{
        struct tuplevar_t *tv = V2TUP(tup);
        if (!tv->hash)
                tv->hash = tuple_calc_hash(tup);
        return tv->hash;
}

struct tuple_iterator_t {
        Object base;
        Object *target;
        size_t i;
};

static Object *
tuple_iter_next(Object *it)
{
        struct tuple_iterator_t *tpit = (struct tuple_iterator_t *)it;
        if (!tpit->target) {
                return NULL;
        } else if (tpit->i < seqvar_size(tpit->target)) {
                Object **data = tuple_get_data(tpit->target);
                return VAR_NEW_REF(data[tpit->i++]);
        } else {
                VAR_DECR_REF(tpit->target);
                tpit->target = NULL;
                return NULL;
        }
}

static void
tuple_iter_reset(Object *it)
{
        struct tuple_iterator_t *tpit = (struct tuple_iterator_t *)it;
        if (tpit->target)
                VAR_DECR_REF(tpit->target);
        tpit->target = NULL;
}

struct type_t TupleIterType = {
        .name           = "tuple_iterator",
        .reset          = tuple_iter_reset,
        .size           = sizeof(struct tuple_iterator_t),
        .iter_next      = tuple_iter_next,
};

static Object *
tuple_get_iter(Object *tup)
{
        struct tuple_iterator_t *ret;

        bug_on(!isvar_tuple(tup));
        ret = (struct tuple_iterator_t *)var_new(&TupleIterType);
        ret->target = VAR_NEW_REF(tup);
        ret->i = 0;
        return (Object *)ret;
}


static const struct type_method_t tuple_cb_methods[] = {
        {"count",   do_tuple_count},
        {"index",   do_tuple_index},
        {NULL, NULL},
};

static const struct seq_methods_t tuple_seq_methods = {
        .getitem        = tuple_getitem,
        .setitem        = NULL,
        .hasitem        = tuple_hasitem,
        .getslice       = tuple_getslice,
        .setslice       = NULL,
        .cat            = tuple_cat,
        .sort           = NULL,
};

static const struct type_prop_t tuple_prop_getsets[] = {
        { .name = "length", .getprop = tuple_getprop_length, .setprop = NULL },
        { .name = NULL },
};

static const struct operator_methods_t tuple_op_methods = {
        .add            = tuple_cat,
};

struct type_t TupleType = {
        .flags  = 0,
        .name = "tuple",
        .opm = &tuple_op_methods,
        .cbm = tuple_cb_methods,
        .mpm = NULL,
        .sqm = &tuple_seq_methods,
        .size = sizeof(struct tuplevar_t),
        .str = tuple_str,
        .cmp = tuple_cmp,
        .cmpeq = tuple_cmpeq,
        .reset = tuple_reset,
        .prop_getsets = tuple_prop_getsets,
        .create = tuple_create,
        .hash = tuple_hash,
        .get_iter = tuple_get_iter,
};

