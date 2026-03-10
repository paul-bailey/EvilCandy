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

static int
tuple_cmp(Object *a, Object *b)
{
        int i, res, n;
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
static bool slice_cmp_lt(int a, int b) { return a < b; }
static bool slice_cmp_gt(int a, int b) { return a > b; }

static Object *
tuple_getslice(Object *obj, int start, int stop, int step)
{
        Object *ret, **src, **dst;
        bool (*cmp)(int, int);
        int dst_i, dst_n;

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
                if (var_compare(data[i], item) == 0)
                        return true;
        }
        return false;
}

/* Also an API func, so not static */
Object *
tuple_getitem(Object *tup, int idx)
{
        struct tuplevar_t *va = V2TUP(tup);

        bug_on(!isvar_tuple(tup));
        bug_on(idx >= seqvar_size(tup));
        VAR_INCR_REF(va->items[idx]);
        return va->items[idx];
}

Object *
tuple_borrowitem(Object *tup, int idx)
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
        Object *self, *xarg, *startarg, **data;
        int i, start, stop;

        self = vm_get_this(fr);
        if (arg_type_check(self, &TupleType) == RES_ERROR)
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

        data = tuple_get_data(self);
        for (i = start; i < stop; i++) {
                if (var_compare(xarg, data[i]) == 0)
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

        xarg = vm_get_arg(fr, 0);
        bug_on(!xarg);

        n = seqvar_size(self);
        data = tuple_get_data(self);
        count = 0;
        for (i = 0; i < n; i++) {
                if (var_compare(xarg, data[i]) == 0)
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
tuplevar_new_common(int n_items, Object **src, bool consume)
{
        int i;
        Object *tup = var_new(&TupleType);
        struct tuplevar_t *th = V2TUP(tup);
        th->items = emalloc(sizeof(Object *) * n_items);

        bug_on(consume && src == NULL);

        seqvar_set_size(tup, n_items);
        for (i = 0; i < n_items; i++) {
                Object *item = src ? src[i] : NullVar;
                /* effectively the same thing as consume */
                if (!consume)
                        VAR_INCR_REF(item);
                th->items[i] = item;
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
        return tuplevar_new_common(n_items, items, consume);
}

/**
 * tuplevar_new - Create a new tuple of size @n_items
 * Return: new tuple.  Each slot is filled with NullVar.
 */
Object *
tuplevar_new(int n_items)
{
        return tuplevar_new_common(n_items, NULL, false);
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
        Object *arg, *args;

        args = vm_get_arg(fr, 0);
        bug_on(!args || !isvar_array(args));
        if (seqvar_size(args) != 1) {
                err_setstr(TypeError,
                           "Expected exactly one argument but got %d",
                           seqvar_size(args));
                return ErrorVar;
        }
        arg = array_borrowitem(args, 0);
        if (!isvar_seq(arg) && !isvar_dict(arg)) {
                err_setstr(TypeError, "Invalid type '%s' for list()",
                           typestr(arg));
                return ErrorVar;
        }
        return var_tuplify(arg);
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
        struct iterator_t base;
        Object *target;
        size_t i;
};

static Object *
tuple_iter_next(struct iterator_t *it)
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

static struct iterator_t *
tuple_get_iter(Object *tup)
{
        struct tuple_iterator_t *ret;
        bug_on(!isvar_tuple(tup));
        ret = emalloc(sizeof(*ret));
        memset(ret, 0, sizeof(*ret));
        ret->base.next = tuple_iter_next;
        ret->target = VAR_NEW_REF(tup);
        ret->i = 0;
        return (struct iterator_t *)ret;
}


static const struct type_inittbl_t tuple_cb_methods[] = {
        V_INITTBL("foreach", var_foreach_generic, 1, 2, -1, -1),
        V_INITTBL("count",   do_tuple_count,      1, 1, -1, -1),
        V_INITTBL("index",   do_tuple_index,      1, 3, -1, -1),
        TBLEND,
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
        .reset = tuple_reset,
        .prop_getsets = tuple_prop_getsets,
        .create = tuple_create,
        .hash = tuple_hash,
        .get_iter = tuple_get_iter,
};

