#include <evilcandy.h>
#include <stdlib.h> /* for atexit */

/**
 * DOC: Variable malloc/free wrappers.
 *
 * Calling malloc and free can be slow, but attempts to avoid calling
 * them can be slower, especially if not every data type is the same
 * size.  The best compromise I could think of is to have a per-type
 * singly linked list of freed variables in the type_t struct, since
 * each type has its own fixed size.  (The variable-length data, such
 * as for arrays and strings, is allocated separately.)
 *
 * We don't want our freelist to build up a million fake-freed
 * variables, either.  The idea behind it is to deal with a frequent
 * scenario where a function is called repetitively, and therefore
 * allocating, deallocating, reallocating, etc., the same variables
 * off the user stack.  It's handy to have a small pool of recently-
 * freed variables of the same type which can be quickly repurposed.
 * A few dozen is enough.
 *
 * MAX_FREELIST_SIZE is the maximum size that each type's freelist
 * may grow before we start calling efree().  Struct var_mem_t is the
 * preheader on top of each allocated object; it's the actual pointer
 * getting allocated each time.  It contains only a pointer to the
 * next var_mem_t struct, possibly padded due to a union with an
 * alignment variable.
 */
#define MAX_FREELIST_SIZE       64

struct var_mem_t {
        union {
                max_align_t dummy_align;
                struct var_mem_t *list;
        };
};

#if DBUG_REPORT_VARS_ON_EXIT

static size_t var_nalloc = 0;

#define REGISTER_ALLOC(n_) do { \
        var_nalloc++;           \
} while (0)

#define REGISTER_FREE(n_)  do {         \
        bug_on((int)var_nalloc <= 0);   \
        var_nalloc--;                   \
} while (0)

static void
var_alloc_tell(void)
{
        DBUG("%s: #vars outstanding:  %lu", __FILE__, (long)var_nalloc);
}

#else /* !DBUG_REPORT_VARS_ON_EXIT */

# define REGISTER_ALLOC(x) do { (void)0; } while (0)
# define REGISTER_FREE(x)  do { (void)0; } while (0)

#endif /* !DBUG_REPORT_VARS_ON_EXIT */

static Object *
var_alloc(struct type_t *type)
{
        Object *ret;
        struct var_mem_t *vm;

        REGISTER_ALLOC(type->size);

        vm = type->freelist;
        if (!vm) {
                vm = emalloc(sizeof(*vm) + type->size);
        } else {
                type->n_freelist--;
                type->freelist = vm->list;
        }
        vm->list = NULL;
        ret = (Object *)(vm + 1);
        memset(ret, 0, type->size);
        return ret;
}

static void
var_free(Object *v)
{
        struct var_mem_t *vm;
        struct type_t *type = v->v_type;

        REGISTER_FREE(type->size);

        vm = ((struct var_mem_t *)v) - 1;
        if (type->n_freelist < MAX_FREELIST_SIZE) {
                type->n_freelist++;
                vm->list = type->freelist;
                type->freelist = vm;
        } else {
                efree(vm);
        }
}

/**
 * var_new - Get a new empty variable
 */
Object *
var_new(struct type_t *type)
{
        Object *v;
        bug_on(type->size == 0);

        v = var_alloc(type);
        v->v_refcnt = 1;
        v->v_type = type;
        return v;
}

/**
 * var_delete - Delete a variable.
 * @v: variable to delete.
 */
void
var_delete__(Object *v)
{
        bug_on(!v);
        bug_on(v->v_refcnt != 0);
        bug_on(!v->v_type);
        if (v->v_type->reset)
                v->v_type->reset(v);

        var_free(v);
}

/*
 * Given extern linkage so it can be called for modules that have
 * data types which don't need to be visible outside their little
 * corner of the interpreter.
 * The major players are forward-declared in typedefs.h and added
 * to VAR_TYPES_TBL[] below in cfile_init_var.
 */
void
var_initialize_type(struct type_t *tp)
{
        tp->methods = dictvar_new();

        /*
         * Sanity-check this, if a sequential var is readable,
         * that means both its getitem AND getslice methods are
         * non-NULL.
         */
        /* TODO: Enable this when I make it true */
#if 0
        bug_on(tp->sqm && (!!tp->sqm->getitem  != !!tp->sqm->getslice));
#endif

        Object *dict = tp->methods;
        const struct type_inittbl_t *t = tp->cbm;
        if (t) while (t->name != NULL) {
                Object *v, *k;
                enum result_t res;

                v = funcvar_from_lut(t);
                k = stringvar_new(t->name);
                res = dict_setitem_exclusive(dict, k, v);
                VAR_DECR_REF(k);
                VAR_DECR_REF(v);

                bug_on(res != RES_OK);
                (void)res;

                t++;
        }

        const struct type_prop_t *p = tp->prop_getsets;
        if (p) while (p->name != NULL) {
                Object *v, *k;
                enum result_t res;

                v = propertyvar_new(p);
                k = stringvar_new(p->name);
                res = dict_setitem_exclusive(dict, k, v);
                VAR_DECR_REF(k);
                VAR_DECR_REF(v);

                bug_on(res != RES_OK);
                (void)res;

                p++;
        }

        if (tp->create) {
                Object *v, *k;
                k = stringvar_new(tp->name);
                v = var_from_format("<xMmok>", tp->create, 2, 2, 0, 1);
                vm_add_global(k, v);
                VAR_DECR_REF(k);
                VAR_DECR_REF(v);
        }
}

static struct type_t *const VAR_TYPES_TBL[] = {
        &ArrayType,
        &TupleType,
        &EmptyType,
        &FloatType,
        &ComplexType,
        &FunctionType,
        &MethodType,
        &IntType,
        &XptrType,
        &DictType,
        &StringType,
        &BytesType,
        &PropertyType,
        &RangeType,
        &UuidptrType,
        &IdType,
        &FileType,
        &FloatsType,
        &StarType,
        NULL,
};

#ifndef NDEBUG
static void
var_sanity_check_types_tbl(void)
{
        /*
         * Bug traps call _Exit, not exit, so bug_on() is safe to call
         * during an atexit() callback.
         */
        int i;
        for (i = 0; VAR_TYPES_TBL[i] != NULL; i++) {
                struct type_t *tp = VAR_TYPES_TBL[i];
                bug_on(tp->freelist != NULL);
                bug_on(tp->n_freelist != 0);
        }
}
#endif /* !NDEBUG */

/*
 * see main.c - this must be after all the typedef code
 * has had their cfile_init functions called, or it will fail.
 */
void
cfile_init_var(void)
{
        int i;
        for (i = 0; VAR_TYPES_TBL[i] != NULL; i++)
                var_initialize_type(VAR_TYPES_TBL[i]);

#if DBUG_REPORT_VARS_ON_EXIT
        atexit(var_alloc_tell);
#endif
#ifndef NDEBUG
        atexit(var_sanity_check_types_tbl);
#endif
}

void
cfile_deinit_var(void)
{
        int i;
        for (i = 0; VAR_TYPES_TBL[i] != NULL; i++) {
                struct type_t *tp = VAR_TYPES_TBL[i];
                VAR_DECR_REF(tp->methods);
                tp->methods = NULL;
        }

        /*
         * Do this outside of above pass, since some vars may
         * get added to freelist on later 'i'
         */
        for (i = 0; VAR_TYPES_TBL[i] != NULL; i++) {
                struct type_t *tp = VAR_TYPES_TBL[i];
                while (tp->freelist != NULL) {
                        struct var_mem_t *vm = tp->freelist;
                        tp->freelist = vm->list;
                        efree(vm);
                        tp->n_freelist--;
                }
                bug_on(tp->n_freelist != 0);
        }
}

/*
 * Helper to var_setattr/var_getattr
 *
 * Convert @idx, where something like '[-3]' means '[size-3]',
 * and make sure the result is within range for @v.
 *
 * Return: converted index or -1 if out of range.
 */
static ssize_t
var_realindex(Object *v, long long idx)
{
        ssize_t i;
        size_t n;

        bug_on(!hasvar_len(v));

        /* idx stores long long, but ii.i is int */
        if (idx < INT_MIN || idx > INT_MAX)
                return -1;

        i = (ssize_t)idx;
        n = seqvar_size(v);

        /* convert '[-i]' to '[size-i]' */
        if (i < 0)
                i += n;
        return i < n ? i : -1;
}

static enum result_t
tup2slice(Object *obj, Object *tup, int *start, int *stop, int *step)
{
        Object **src;
        int i, j, k;
        size_t size;
        if (tuple_validate(tup, "i*i", false) != RES_OK)
                goto malformed;
        src = tuple_get_data(tup);
        if (src[1] != NullVar && !isvar_int(src[1]))
                goto malformed;

        size = seqvar_size(obj);

        i = intvar_toi(src[0]);
        j = src[1] == NullVar ? size : intvar_toi(src[1]);
        k = intvar_toi(src[2]);
        if (err_occurred())
                return RES_ERROR;

        if (i < 0) {
                i += size;
                if (i < 0)
                        i = 0;
        } else if (i >= size) {
                i = size;
        }

        if (j < 0) {
                j += size;
                if (j < -1)
                        j = -1;
        } else if (j > size) {
                j = size;
        }

        if (k == 0) {
                err_setstr(ValueError, "Slice step may not be zero");
                return RES_ERROR;
        }

        if ((i < j && k < 0)
            || (i > j && k > 0)) {
                /* Length zero.  Still pass to callback, since
                 * only it knows what zero-length object to create.
                 */
                i = j = 0;
        }

        *start = i;
        *stop  = j;
        *step  = k;

        return RES_OK;

malformed:
        err_setstr(TypeError, "Slice has invalid tuple format");
        return RES_ERROR;
}

/**
 * var_slice_size - Get size of a slice
 * @start: start index (inclusive)
 * @stop:  stop index (exclusive)
 * @step:  steps between indices
 *
 * Return: size of slice
 */
size_t
var_slice_size(ssize_t start, ssize_t stop, ssize_t step)
{
        ssize_t res;
        if (step > 0)
                res = (stop - start + step - 1) / step;
        else
                res = (stop - start + step + 1) / step;
        return res < 0 ? 0 : res;
}

/**
 * seqvar_arg2idx - Helper functions for sequential objects to translate
 *                  an index argument (which may be <0) into an index >= 0
 * @obj:  Object being de-referenced
 * @iarg: Argument containing an integer index number
 * @idx:  Pointer to a variable to store index
 *
 * Return: RES_OK or RES_ERROR.  If result is RES_ERROR, an exceptin has
 *         been thrown.
 */
enum result_t
seqvar_arg2idx(Object *obj, Object *iarg, int *idx)
{
        /*
         * XXX: Slight DRY violation with var_realindex, tup2slice.
         * Review policy and merge duplicate parts of these functions.
         */
        int i;
        bug_on(!isvar_seq(obj));
        if (arg_type_check(iarg, &IntType) == RES_ERROR)
                return RES_ERROR;
        i = intvar_toi(iarg);
        if (err_occurred())
                return RES_ERROR;

        if (i < 0) {
                i += seqvar_size(obj);
                if (i < 0)
                        i = 0;
        }
        *idx = i;
        return RES_OK;
}

/*
 * Either @v is a dictionary or @key is a string, which could be for a
 * built-in method or property of @v, whatever @v's type.
 */
static Object *
var_getattr_map(Object *v, Object *key)
{
        /*
         * first check if v maps it. If failed, check the
         * built-in methods.
         */
        Object *ret;
        const struct map_methods_t *mpm = v->v_type->mpm;
        if (mpm && mpm->getitem) {
                ret = mpm->getitem(v, key);
                if (ret)
                        goto found;
        }

        /* try built-in methods or properties */

        /* ...whose key must be a string */
        if (!isvar_string(key))
                goto badtype;

        ret = dict_getitem(v->v_type->methods, key);
        if (ret) {
                if (isvar_property(ret)) {
                        Object *tmp = property_get(ret, v);
                        VAR_DECR_REF(ret);
                        /* XXX if prop is func, swap below? */
                        return tmp;
                }
                goto found;
        }

        err_setstr(KeyError, "%s object has no attribute %s",
                   typestr(v), string_cstring(key));
        return ErrorVar;

found:
        /*
         * Save "owner" with function, so when it's called it
         * knows who its "this" is.
         */
        if (isvar_function(ret)) {
                Object *tmp = ret;
                ret = methodvar_new(tmp, v);
                VAR_DECR_REF(tmp);
        }
        return ret;

        /* else, invalid key, fall through */
badtype:
        err_attribute("get", key, v);
        return ErrorVar;
}

static Object *
var_getattr_seq(Object *v, Object *key)
{
        const struct seq_methods_t *sqm = v->v_type->sqm;
        bug_on(!sqm);
        if (isvar_int(key)) {
                Object *ret;
                ssize_t i;
                if (!sqm->getitem)
                        goto badtype;

                i = var_realindex(v, intvar_toll(key));
                if (i < 0) {
                        err_index(key);
                        return ErrorVar;
                }
                ret = sqm->getitem(v, i);
                bug_on(!ret);
                return ret;
        } else if (isvar_tuple(key)) {
                size_t seqsize;
                int start, stop, step;
                if (!sqm->getslice)
                        goto badtype;
                if (tup2slice(v, key, &start, &stop, &step) == RES_ERROR) {
                        bug_on(!err_occurred());
                        return ErrorVar;
                }

                /* Cannot slice an empty sequence */
                if ((seqsize = seqvar_size(v)) == 0)
                        return VAR_NEW_REF(v);

                /*
                 * For setslice this could == size, but for getslice we
                 * need this to be safely in range.
                 */
                if (start >= seqsize)
                        start = seqsize - 1;

                return sqm->getslice(v, start, stop, step);
        } else {
                goto badkey;
        }

badtype:
        err_attribute("get", key, v);
        return ErrorVar;

badkey:
        err_setstr(TypeError,
                   "Invalid key type '%s' type for sequence type '%s'",
                   typestr(key), typestr(v));
        return ErrorVar;
}

/**
 * var_getattr - Generalized get-attribute
 * @v:  Variable whose attribute we're seeking
 * @key: Variable storing the key, either the name or an index number
 *
 * Return: Attribute of @v, or ErrorVar if not found.
 *
 * This implements the EvilCandy expression: v[key]
 *
 * If @key is an integer, it gets the indexed item @v.
 * If @key is a string, it first checks if @v is a dictionary storing
 * @key, then it checks if @v is any type with a property or built-in
 * method named @key.
 *
 * ONLY vm.c SHOULD USE THIS!  It accesses a dictionary which may not
 * yet exist during initialization, but which will be available by the
 * time the VM is running.  It also does some interpolating to make
 * dictionaries behave more like instances rather than pure dictionaries,
 * something internal code has no need for.
 */
Object *
var_getattr(Object *v, Object *key)
{
        if (isvar_map(v) || isvar_string(key)) {
                return var_getattr_map(v, key);
        } else if (isvar_seq(v)) {
                return var_getattr_seq(v, key);
        } else {
                err_attribute("get", key, v);
                return ErrorVar;
        }
}

/**
 * var_hasattr - Implement the has keyword
 * @haystack: the lval 'a' of 'a has b'
 * @needle:   the rval 'b' of 'a has b'
 *
 * Return: true if @needle is an item stored by @haystack, false if not.
 *      Unlike with var_get, built-in attributes and properties are not
 *      a consideration.
 */
bool
var_hasattr(Object *haystack, Object *needle)
{
        const struct seq_methods_t *sqm = haystack->v_type->sqm;
        const struct map_methods_t *mpm = haystack->v_type->mpm;
        if (sqm && sqm->hasitem)
                return sqm->hasitem(haystack, needle);
        if (mpm && mpm->hasitem) {
                if (!isvar_string(needle))
                        return false;
                return mpm->hasitem(haystack, needle);
        }
        return false;
}

/*
 * Either @v is a dictionary or @key is a string, which could mean to
 * set a property in @v, regardless of @v's type.
 */
static enum result_t
var_setattr_map(Object *v, Object *key, Object *attr)
{
        Object *prop;
        enum result_t ret;
        const struct map_methods_t *map = v->v_type->mpm;
        if (!map || !map->setitem)
                goto badtype;

        if (isvar_string(key) && seqvar_size(key) == 0) {
                err_setstr(KeyError, "key may not be empty");
                return RES_ERROR;
        }
        ret = map->setitem(v, key, attr);
        if (ret == RES_OK)
                return ret;

        /* try property, whose key must be string */
        if (!isvar_string(key))
                goto badtype;

        /* may not delete built-in properties */
        if (!attr)
                goto badtype;

        err_clear();
        prop = dict_getitem(v->v_type->methods, key);
        if (prop && isvar_property(prop)) {
                ret = property_set(prop, v, attr);
                VAR_DECR_REF(prop);
                return ret;
        }

badtype:
        err_attribute("set", key, v);
        return RES_ERROR;
}

static enum result_t
var_setattr_seq(Object *v, Object *key, Object *attr)
{
        const struct seq_methods_t *seq = v->v_type->sqm;
        bug_on(!seq);
        if (isvar_tuple(key)) {
                int start, stop, step;
                if (!seq->setslice)
                        goto badtype;
                if (tup2slice(v, key, &start, &stop, &step) == RES_ERROR) {
                        bug_on(!err_occurred());
                        return RES_ERROR;
                }
                return seq->setslice(v, start, stop, step, attr);
        } else if (isvar_int(key)) {
                int i;
                if (!seq->setitem)
                        goto badtype;

                i = var_realindex(v, intvar_toll(key));
                if (i < 0) {
                        err_index(key);
                        return RES_ERROR;
                }

                return seq->setitem(v, i, attr);
        } else {
                goto badkey;
        }

badtype:
        err_attribute("set", key, v);
        return RES_ERROR;

badkey:
        err_setstr(TypeError,
                   "Invalid key type '%s' type for sequence type '%s'",
                   typestr(key), typestr(v));
        return RES_ERROR;
}

/**
 * var_setattr - Generalized set-attribute
 * @v:          Variable whose attribute we're setting
 * @key:        Variable storing the index number or name
 * @attr:       Variable storing the attribute to set.  This will be
 *              copied, so calling function still must handle GC for this
 * Return:      RES_OK if success, RES_ERROR if failure does not exist.
 *
 * This implements v[key] = attr;
 */
enum result_t
var_setattr(Object *v, Object *key, Object *attr)
{
        if (isvar_map(v) || isvar_string(key)) {
                return var_setattr_map(v, key, attr);
        } else if (isvar_seq(v)) {
                return var_setattr_seq(v, key, attr);
        } else {
                err_attribute("set", key, v);
                return RES_ERROR;
        }
}

/**
 * var_compare - Compare two variables, used for sorting et al.
 * @a: First variable to compare.
 * @b: Second variable to compare.
 *
 * Return: -1 if "a < b", 1 if "a > b", and 0 if "a == b".
 * This is not (necessarily) a pointer comparison.  Each typedef
 * has their own method of comparison.
 *
 * Note: this is not strict w/r/t ints and floats.  1.0 == 1 in this
 * function.
 */
int
var_compare(Object *a, Object *b)
{
        if (a == b)
                return 0;
        if (a == NULL)
                return -1;
        if (b == NULL)
                return 1;
        if (a->v_type != b->v_type
            && !(isvar_real(a) && isvar_real(b))) {
                return strcmp(typestr(a), typestr(b));
        }
        if (!a->v_type->cmp)
                return a < b ? -1 : 1;
        return a->v_type->cmp(a, b);
}

/**
 * var_compare_iarg - compare two variables, taking an instruction arg
 * @a:          Left operand
 * @b:          Right operand
 * @iarg:       One of the IARG_xxx enums
 *
 * Return: True if "@a @iarg @b" is true, false otherwise.
 */
bool
var_compare_iarg(Object *a, Object *b, int iarg)
{
        int cmp;
        if (iarg == IARG_EQ3 || iarg == IARG_NEQ3) {
                /* strict compare */
                cmp = (b == a);
                if (iarg == IARG_NEQ3)
                        cmp = !cmp;
        } else if (iarg == IARG_HAS) {
                cmp = var_hasattr(a, b);
        } else {
                cmp = var_compare(a, b);
                switch (iarg) {
                case IARG_EQ:
                        cmp = cmp == 0;
                        break;
                case IARG_LEQ:
                        cmp = cmp <= 0;
                        break;
                case IARG_GEQ:
                        cmp = cmp >= 0;
                        break;
                case IARG_NEQ:
                        cmp = cmp != 0;
                        break;
                case IARG_LT:
                        cmp = cmp < 0;
                        break;
                case IARG_GT:
                        cmp = cmp > 0;
                        break;
                default:
                        bug();
                }
        }
        return !!cmp;
}

int
var_sort(Object *v)
{
        if (!v->v_type->sqm || !v->v_type->sqm->sort)
                return -1;
        v->v_type->sqm->sort(v);
        return 0;
}

/* Used for built-in print function to express a variable */
Object *
var_str(Object *v)
{
        /* every data type should have this */
        bug_on(!v->v_type->str);
        return v->v_type->str(v);
}

/**
 * var_str_swap - Return @v if it's a string, otherwise consume its
 *                reference and return its string representation.
 */
Object *
var_str_swap(Object *v)
{
        if (!isvar_string(v)) {
                Object *s = var_str(v);
                VAR_DECR_REF(v);
                v = s;
        }
        return v;
}

/**
 * var_cmpz - Compare @v to zero, NULL, or something like it
 * @status:  To be set to RES_ERROR if cmpz not permitted,
 *           RES_OK otherwise.  This may not be NULL.
 *
 * Return: true if @v is some kind of 'false', which depends on
 *         type.  Generally, return true if...
 *               @v is numerical and its value is zero.
 *               @v is sequential and its length is zero.
 */
bool
var_cmpz(Object *v, enum result_t *status)
{
        if (!v->v_type->cmpz) {
                err_permit("cmpz", v);
                *status = RES_ERROR;
                return true;
        }
        *status = RES_OK;
        return v->v_type->cmpz(v);
}

enum {
        V_ANY, V_ALL
};

struct iterable_t {
        Object *v;
        Object *dict;
        Object *(*get)(Object *, int);
        size_t i;
        size_t n;
};

static enum result_t
iterable_setup(Object *v, struct iterable_t *iter)
{
        VAR_INCR_REF(v);
        if (isvar_dict(v)) {
                Object *keys = dict_keys(v, false);
                bug_on(!keys);
                iter->dict = v;
                iter->v = keys;
        } else {
                iter->dict = NULL;
                if (!isvar_seq(v))
                        return RES_ERROR;
                iter->v = v;
        }
        iter->get = iter->v->v_type->sqm->getitem;
        if (!iter->get) {
                bug_on(iter->dict);
                return RES_ERROR;
        }
        iter->i = 0;
        iter->n = seqvar_size(iter->v);
        return RES_OK;
}

static void
iterable_cleanup(struct iterable_t *iter)
{
        VAR_DECR_REF(iter->v);
        if (iter->dict)
                VAR_DECR_REF(iter->dict);
        memset(iter, 0, sizeof(*iter));
}

/* reference is produced if return value is valid */
static Object *
iterable_next(struct iterable_t *iter)
{
        Object *ret;
        if (iter->i >= iter->n)
                return NULL;
        ret = iter->get(iter->v, iter->i);
        if (!ret)
                return ErrorVar;
        iter->i++;
        return ret;
}

#define foreach_iterable(e_, iter_) \
        for (e_ = iterable_next(iter_); \
             e_ != NULL && e_ != ErrorVar; \
             e_ = iterable_next(iter_))

Object *
var_tuplify(Object *obj)
{
        struct iterable_t iter;
        Object *ret, **data;
        int i, n;

        if (iterable_setup(obj, &iter) == RES_ERROR)
                return ErrorVar;

        n = seqvar_size(obj);
        ret = tuplevar_new(n);
        data = tuple_get_data(ret);
        for (i = 0; i < n; i++) {
                VAR_DECR_REF(data[i]);
                /* iterable_next() already produced ref */
                data[i] = iterable_next(&iter);
                bug_on(!data[i]);
        }
        iterable_cleanup(&iter);
        return ret;
}

Object *
var_listify(Object *obj)
{
        struct iterable_t iter;
        Object *ret, **data;
        int i, n;

        if (iterable_setup(obj, &iter) == RES_ERROR)
                return ErrorVar;

        n = seqvar_size(obj);
        ret = arrayvar_new(n);
        data = array_get_data(ret);
        for (i = 0; i < n; i++) {
                VAR_DECR_REF(data[i]);
                /* iterable_next() already produced ref */
                data[i] = iterable_next(&iter);
                bug_on(!data[i]);
        }
        iterable_cleanup(&iter);
        return ret;
}

/*
 * all(x) - return true if every element in x is true.
 */
static bool
var_all_or_any(Object *v, enum result_t *status, int which)
{
        struct iterable_t iter;
        Object *e;
        bool res;

        if (iterable_setup(v, &iter) == RES_ERROR)
                goto err;

        res = false;
        foreach_iterable(e, &iter) {
                res = !var_cmpz(e, status);
                VAR_DECR_REF(e);
                if (*status != RES_OK)
                        goto err;
                if (res && which == V_ANY)
                        break;
                if (!res && which == V_ALL)
                        break;
        }
        if (e == ErrorVar)
                goto err;
        iterable_cleanup(&iter);
        *status = RES_OK;
        return res;

err:
        if (!err_occurred()) {
                err_setstr(TypeError, "Invalid type '%s' for %s()",
                           typestr(v), which == V_ALL ? "all" : "any");
        }
        iterable_cleanup(&iter);
        *status = RES_ERROR;
        return false;
}

#define FAST_ITER(v_) ((v_)->v_type->sqm->fast_iter)
#define MAY_FAST_ITER(v_) (isvar_seq(v_) && FAST_ITER(v_) != NULL)
bool
var_all(Object *v, enum result_t *status)
{
        if (MAY_FAST_ITER(v))
                return FAST_ITER(v)->all(v);
        return var_all_or_any(v, status, V_ALL);
}

bool
var_any(Object *v, enum result_t *status)
{
        if (MAY_FAST_ITER(v))
                return FAST_ITER(v)->any(v);
        return var_all_or_any(v, status, V_ANY);
}

enum { V_MIN, V_MAX };

static Object *
var_min_or_max(Object *v, int minmax)
{
        struct iterable_t iter;
        Object *e;
        Object *res;

        if (iterable_setup(v, &iter) == RES_ERROR)
                goto err;

        res = NULL;
        foreach_iterable(e, &iter) {
                int cmp;

                if (res == NULL) {
                        res = e;
                        continue;
                }

                cmp = var_compare(e, res);
                if ((minmax == V_MIN && cmp < 0)
                    || (minmax == V_MAX && cmp > 0)) {
                        VAR_DECR_REF(res);
                        res = e;
                        continue;
                }
                VAR_DECR_REF(e);
        }
        iterable_cleanup(&iter);
        if (e == ErrorVar)
                goto err;
        if (!res) {
                err_setstr(ValueError, "Object is empty");
                goto err;
        }
        return res;

err:
        if (!err_occurred()) {
                err_setstr(TypeError, "Invalid type '%s' for %s()",
                           typestr(v), minmax == V_MIN ? "min" : "max");
        }
        return ErrorVar;
}

Object *
var_min(Object *v)
{
        if (MAY_FAST_ITER(v))
                return FAST_ITER(v)->min(v);
        return var_min_or_max(v, V_MIN);
}

Object *
var_max(Object *v)
{
        if (MAY_FAST_ITER(v))
                return FAST_ITER(v)->max(v);
        return var_min_or_max(v, V_MAX);
}

Object *
var_lnot(Object *v)
{
        int status;
        bool cond = var_cmpz(v, &status);
        if (status)
                return NULL;
        return intvar_new((int)cond);
}

Object *
var_logical_or(Object *a, Object *b)
{
        int status;
        bool res = !var_cmpz(a, &status);
        if (status)
                return NULL;
        res = res || !var_cmpz(b, &status);
        if (status)
                return NULL;
        return intvar_new((int)res);
}

Object *
var_logical_and(Object *a, Object *b)
{
        int status;
        bool res = !var_cmpz(a, &status);
        if (status)
                return NULL;
        res = res && !var_cmpz(b, &status);
        if (status)
                return NULL;
        return intvar_new((int)res);
}

/**
 * var_foreach_generic - Common .foreach method for sequential objects.
 */
Object *
var_foreach_generic(Frame *fr)
{
        Object *self, *func, *priv, *dict;
        int i, status, argc;
        Object *(*get)(Object *, int);

        self = vm_get_this(fr);
        bug_on(!isvar_seq(self) && !isvar_dict(self));

        func = vm_get_arg(fr, 0);
        priv = vm_get_arg(fr, 1);

        /*
         * If 'func' is not a function, it could still be callable, so do
         * not use arg_type_check() for it.
         */
        if (!func) {
                err_argtype("function");
                return ErrorVar;
        }

        if (isvar_dict(self)) {
                dict = self;
                self = dict_keys(dict, true);
        } else {
                dict = NULL;
        }

        get = self->v_type->sqm->getitem;
        bug_on(!get);

        status = RES_OK;
        if (!seqvar_size(self))
                goto out;

        argc = priv ? 3 : 2;
        for (i = 0; i < seqvar_size(self); i++) {
                Object *ret, *key, *value, *argv[3];
                value = get(self, i);
                bug_on(!value);
                if (dict) {
                        key = value;
                        value = dict_getitem(dict, key);
                        if (!value) {
                                VAR_DECR_REF(key);
                                continue;
                        }
                } else {
                        key = intvar_new(i);
                }

                argv[0] = value;
                argv[1] = key;
                if (priv)
                        argv[2] = priv;

                ret = vm_exec_func(fr, func, argc, argv, false);
                VAR_DECR_REF(argv[0]);
                VAR_DECR_REF(argv[1]);

                if (ret) {
                        if (ret == ErrorVar) {
                                status = RES_ERROR;
                                break;
                        }

                        VAR_DECR_REF(ret);
                }
        }

out:
        if (dict) /* keys were generated here */
                VAR_DECR_REF(self);

        return status == RES_OK ? NULL : ErrorVar;
}

