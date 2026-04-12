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

#define VM2VAR(x_)      ((Object *)((x_) + 1))
#define VAR2VM(x_)      (((struct var_mem_t *)(x_)) - 1)

static struct var_mem_t *var_pending_free = NULL;
static long var_locked = 0;

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
        ret = VM2VAR(vm);
        memset(ret, 0, type->size);
        return ret;
}

static void
var_free(Object *v)
{
        struct var_mem_t *vm;
        struct type_t *type = v->v_type;

        REGISTER_FREE(type->size);

        vm = VAR2VM(v);
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
 * var_lock - Prevent variables from being freed.
 *
 * Used to prevent improper re-entrance into vm such that stack state
 * is not coherent (eg. some .reset() callbacks call a UAPI function).
 *
 * This must have a parallel call to var_unlock().
 */
void
var_lock(void)
{
        var_locked++;
}

/**
 * var_unlock - Call this in parallel to var_lock().
 */
void
var_unlock(void)
{
        var_locked--;
        bug_on(var_locked < 0L);
        if (!var_locked) {
                struct var_mem_t *vm = var_pending_free;
                while (vm && !var_locked) {
                        /*
                         * Keep this order! var_delete__ could re-enter
                         * in a way that adds new vars to var_pending_free
                         *
                         * XXX better to change vm->list from single to
                         * double, then?
                         */
                        var_pending_free = vm->list;
                        var_delete__(VM2VAR(vm));
                        vm = var_pending_free;
                }
        }
}

/**
 * var_delete - Delete a variable.
 * @v: variable to delete.
 */
void
var_delete__(Object *v)
{
        /*
         * XXX: var_lock() is meant to prevent re-entrance at certain
         * moments, but that only matters for less than 1% of the time,
         * specifically objects whose .reset() will trigger a UAPI
         * function.  We should check it (maybe something kind of like
         * "v->v_type->isreentrant(v)") so we don't add like a bajillion
         * objects to be freed all at once.
         * (Test: Does it cause noticeable program stuttering?)
         */
        if (var_locked) {
                struct var_mem_t *vm;

                vm = VAR2VM(v);
                vm->list = var_pending_free;
                var_pending_free = vm;
        } else {
                bug_on(!v);
                bug_on(v->v_refcnt != 0);
                bug_on(!v->v_type);
                if (v->v_type->reset) {
                        /*
                         * Nudge refcnt back up temporily while callback is
                         * operating on the object.
                         */
                        v->v_refcnt++;
                        v->v_type->reset(v);
                        v->v_refcnt = 0;
                }

                var_free(v);
        }
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

        Object *dict = tp->methods;
        const struct type_method_t *t = tp->cbm;
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

                v = propertyvar_new_intl(p);
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
        &SetType,
        &UuidptrType,
        &IdType,
        &ClassType,
        &InstanceType,
        &CellType,

        /* the iterators */
        &ArrayIterType,
        &BytesIterType,
        &DictIterType,
        &TupleIterType,
        &SetIterType,
        &RangeIterType,
        &StringIterType,

        /* special extra iters */
        &DictItemsType,
        &DictItemsIterType,
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
 * Helper to var_setitem/var_getitem
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
tup2slice(Object *obj, Object *tup,
          ssize_t *start, ssize_t *stop, ssize_t *step)
{
        Object *jarg;
        ssize_t i, j, k;
        size_t size;

        size = seqvar_size(obj);
        if (vm_getargs_sv(tup, "(z<*>z!):slice", &i, &jarg, &k) != RES_OK)
                return RES_ERROR;
        if (jarg == NullVar) {
                j = size;
        } else {
                long long jll = intvar_toll(jarg);
                if ((jll < 0 && -jll != (size_t)(-jll)) ||
                    (jll > 0 && jll != (size_t)jll)) {
                        err_setstr(RangeError,
                                "slice '[i:j:k]' j index out of bounds");
                        return RES_ERROR;
                }
                j = (ssize_t)jll;
        }

        if (i < 0) {
                i += size;
                if (i < 0)
                        i = k < 0 ? -1 : 0;
        } else if (i >= size) {
                i = k < 0 ? size - 1 : size;
        }

        if (j < 0) {
                j += size;
                if (j < 0)
                        j = k < 0 ? -1 : 0;
        } else if (j >= size) {
                j = k < 0 ? size - 1 : size;
        }

        if (k == 0) {
                err_setstr(ValueError, "Slice step may not be zero");
                return RES_ERROR;
        }

        if ((i < j && k < 0)
            || (i > j && k > 0)) {
                i = j;
        }

        *start = i;
        *stop  = j;
        *step  = k;

        return RES_OK;
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
 * var_index_capi - Transform a UAPI index number to a CAPI one.
 * @size:       Size of indexable item
 * @a:          Pointer to one size to fix
 * @b:          Pointer to another size to fix
 * @errhandler: ERRH_EXCEPTION to raise an exception if out of bounds, or
 *              ERRH_RETURN to quietly return an error if out of bounds.
 *
 * This turns something like x[-1] to x[length(x)-1].
 * One of @a an @b may be NULL.
 * @a and @b are assumed to be "start" and "stop", eg. for search limits.
 * @a (@b=NULL) is assumed to "at this index", eg. for getitem
 *
 * Final @a and @b may not be < 0.  Final @b is assumed to be a "stop"
 * index, so it may be == size, but not > size.  Final @a must be < size.
 *
 * Return: RES_OK or RES_ERROR, depending whether @a and @b fit within
 * range according to above description.
 */
enum result_t
var_index_capi(size_t size, ssize_t *a, ssize_t *b,
               enum errhandler_t errhandler)
{
        ssize_t x;
        if (a) {
                x = *a;
                if (x < 0)
                        x += size;
                if (x < 0 || x >= size)
                        goto err;
                *a = x;
        }
        if (b) {
                x = *b;
                if (x < 0)
                        x += size;
                if (x < 0 || x > size)
                        goto err;
                *b = x;
        }
        return RES_OK;

err:
        if (errhandler == ERRH_EXCEPTION)
                err_setstr(RangeError, "index out of bounds");
        return RES_ERROR;
}

static Object *
var_getitem_map(Object *v, Object *key)
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
                        return ret;
        }
        err_subscript("get", key, v);
        return ErrorVar;
}

static Object *
var_getitem_seq(Object *v, Object *key)
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
                ssize_t start, stop, step;
                if (!sqm->getslice)
                        goto badtype;
                if (tup2slice(v, key, &start, &stop, &step) == RES_ERROR) {
                        bug_on(!err_occurred());
                        return ErrorVar;
                }

                /* Cannot slice an empty sequence */
                if ((seqsize = seqvar_size(v)) == 0)
                        return VAR_NEW_REF(v);

                return sqm->getslice(v, start, stop, step);
        } else {
                goto badkey;
        }

badtype:
        err_subscript("get", key, v);
        return ErrorVar;

badkey:
        err_setstr(TypeError,
                   "Invalid key type '%s' type for sequence type '%s'",
                   typestr(key), typestr(v));
        return ErrorVar;
}

Object *
var_getitem(Object *v, Object *key)
{
        if (isvar_map(v) || isvar_string(key)) {
                return var_getitem_map(v, key);
        } else if (isvar_seq(v)) {
                return var_getitem_seq(v, key);
        } else {
                err_subscript("get", key, v);
                return ErrorVar;
        }
}

/**
 * var_getattr - Generalized get-attribute
 * @v:  Variable whose attribute we're seeking
 * @key: Variable storing the key, either the name or an index number
 *
 * Return: Attribute of @v, or ErrorVar if not found.
 *
 * This implements the EvilCandy expression: v.key
 */
Object *
var_getattr(Object *v, Object *key)
{
        Object *ret;
        if (isvar_instance(v)) {
                ret = instance_getattr(v, key);
                if (!ret) {
                        err_attribute("get", key, v);
                        return ErrorVar;
                }
                return ret;
        }
        ret = dict_getitem(v->v_type->methods, key);
        if (!ret) {
                err_attribute("get", key, v);
                return ErrorVar;
        }
        if (isvar_property(ret)) {
                Object *tmp = ret;
                ret = property_get(ret, v, key);
                VAR_DECR_REF(tmp);
        } else if (isvar_function(ret)) {
                Object *tmp = ret;
                ret = methodvar_new(tmp, v);
                VAR_DECR_REF(tmp);
        }
        return ret;
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
var_hasitem(Object *haystack, Object *needle)
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
var_setitem_map(Object *v, Object *key, Object *attr)
{
        const struct map_methods_t *map = v->v_type->mpm;
        if (!map || !map->setitem) {
                err_subscript("set", key, v);
                return RES_ERROR;
        }

        if (isvar_string(key) && seqvar_size(key) == 0) {
                err_setstr(KeyError, "key may not be empty");
                return RES_ERROR;
        }
        return map->setitem(v, key, attr);
}

static enum result_t
var_setitem_seq(Object *v, Object *key, Object *attr)
{
        const struct seq_methods_t *seq = v->v_type->sqm;
        bug_on(!seq);
        if (isvar_tuple(key)) {
                ssize_t start, stop, step;
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
 * var_setitem - Generalized set-item
 * @v:          Variable whose item we're setting
 * @key:        index number, slice, name, etc.
 * @value:      Value to set item to
 *
 * Return: RES_OK or RES_ERROR
 *
 * This implements
 *              v[key] = value;       # value != NULL
 *              delete v[key];        # value == NULL
 */
enum result_t
var_setitem(Object *v, Object *key, Object *value)
{
        if (isvar_map(v) || isvar_string(key)) {
                return var_setitem_map(v, key, value);
        } else if (isvar_seq(v)) {
                return var_setitem_seq(v, key, value);
        } else {
                err_subscript("set", key, v);
                return RES_ERROR;
        }
}

/**
 * var_setattr - Generalized set-attribute
 * @v:          Variable whose attribute we're setting
 * @key:        Variable storing the index number or name
 * @attr:       Variable storing the attribute to set.
 * Return:      RES_OK if success, RES_ERROR if failure does not exist.
 *
 * This implements
 *              v.key = attr;   # attr != NULL
 *              delete v.key;   # attr == NULL
 */
enum result_t
var_setattr(Object *v, Object *key, Object *attr)
{
        if (isvar_instance(v)) {
                if (instance_setattr(v, key, attr) == RES_OK)
                        return RES_OK;
        }

        /* GitHub issue #26: All properties are read-only now */
        err_attribute(attr ? "set" : "delete", key, v);
        return RES_ERROR;
}

static bool
var_number_must_swap(Object *a, Object *b)
{
        if (isvar_complex(a))
                return false;
        if (isvar_complex(b))
                return true;
        if (isvar_float(a))
                return false;
        if (isvar_float(b))
                return true;
        return false;
}

static enum result_t
var_compare_numbers(Object *alice, Object *bob, int *result)
{
        enum result_t ret;
        bug_on(!result);
        if (var_number_must_swap(alice, bob)) {
                ret = bob->v_type->cmp(bob, alice, result);
                if (ret == RES_OK)
                        *result = -(*result);
        } else {
                ret = alice->v_type->cmp(alice, bob, result);
        }
        return ret;
}

static bool
var_number_matches(Object *alice, Object *bob)
{
        if (var_number_must_swap(alice, bob))
                return bob->v_type->cmpeq(bob, alice);
        else
                return alice->v_type->cmpeq(alice, bob);
}

/*
 * XXX REVISIT: Cf. cpython's .tp_richcompare methods, in which
 * var_compare_iarg() is effectively done at the class-specific
 * object level.  There's more points of code that way, but at
 * least it means there are fewer weird corner cases where, for
 * example, a class has .cmp but not .cmpz or vice-versa, so some
 * "match" tests pass and others fail for the same instances.
 */

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
enum result_t
var_compare(Object *a, Object *b, int *result)
{
        bug_on(!a || !b);

        if (a == b) {
                *result = 0;
                return RES_OK;
        }
        if (a->v_type != b->v_type) {
                if (isvar_number(a) && isvar_number(b))
                        return var_compare_numbers(a, b, result);
                err_setstr(TypeError,
                           "Comparison not permitted between %s and %s",
                           typestr(a), typestr(b));
                return RES_ERROR;
        }
        if (!a->v_type->cmp) {
                err_setstr(TypeError,
                           "Comparison operation not permitted for type '%s'",
                           typestr(a));
                return RES_ERROR;
        }
        return a->v_type->cmp(a, b, result);
}

/*
 * var_eq - Return true if alice and bob match, false if not.
 *
 * This falls back on '===' if no .cmpeq method exists for type.
 */
bool
var_matches(Object *alice, Object *bob)
{
        if (alice == bob)
                return true;
        if (isvar_number(alice) && isvar_number(bob))
                return var_number_matches(alice, bob);
        if (alice->v_type != bob->v_type)
                return false;
        if (!alice->v_type->cmpeq) {
                /* no .cmpeq() method means "!== implies !=" */
                return false;
        }
        return alice->v_type->cmpeq(alice, bob);
}

/**
 * var_compare_iarg - compare two variables, taking an instruction arg
 * @a:          Left operand
 * @b:          Right operand
 * @iarg:       One of the IARG_xxx enums defined in instruction.h
 *
 * Return: True if "@a @iarg @b" is true, false otherwise.
 */
enum result_t
var_compare_iarg(Object *a, Object *b, int iarg, bool *result)
{
        int cmp;
        switch (iarg) {
        case IARG_EQ3:
                cmp = (b == a);
                break;
        case IARG_NEQ3:
                cmp = (b != a);
                break;
        case IARG_EQ:
                cmp = var_matches(a, b);
                break;
        case IARG_NEQ:
                cmp = !var_matches(a, b);
                break;
        case IARG_HAS:
                cmp = var_hasitem(a, b);
                break;
        case IARG_IN:
                cmp = var_hasitem(b, a);
                break;
        default:
                if (var_compare(a, b, &cmp) == RES_ERROR)
                        return RES_ERROR;

                switch (iarg) {
                case IARG_LEQ:
                        cmp = cmp <= 0;
                        break;
                case IARG_GEQ:
                        cmp = cmp >= 0;
                        break;
                case IARG_LT:
                        cmp = cmp < 0;
                        break;
                case IARG_GT:
                        cmp = cmp > 0;
                        break;
                default:
                        bug();
                        return RES_ERROR;
                }
        }
        *result = !!cmp;
        return RES_OK;
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
        if (v->v_type->str) {
                Object *ret = v->v_type->str(v);
                if (ret && !isvar_string(ret)) {
                        VAR_DECR_REF(ret);
                        err_clear();
                        ret = NULL;
                } else if (ret == ErrorVar) {
                        err_clear();
                        ret = NULL;
                }

                if (ret)
                        return ret;
                /* else, fall through, use default implementation */
        }

        return stringvar_from_format("<%s at %p>", v->v_type->name, v);
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
 * var_cmpz - Bool-ify an object
 *
 * Return: true if @v is some kind of 'false', which depends on type.
 *
 * If @v lacks a .cmpz method, return true, unless @v is sequential
 * and its length is non zero.
 */
bool
var_cmpz(Object *v)
{
        if (!v->v_type->cmpz) {
                if (hasvar_len(v))
                        return seqvar_size(v) == 0;
                return true;
        }
        return v->v_type->cmpz(v);
}

enum {
        V_ANY, V_ALL
};

/*
 * all(x) - return true if every element in x is true.
 */
static bool
var_all_or_any(Object *v, enum result_t *status, int which)
{
        Object *it, *child;
        bool res;

        it = iterator_get(v);
        if (!it) {
                err_iterable(v, which == V_ALL ? "all" : "any");
                *status = RES_ERROR;
                return false;
        }
        res = false;
        ITERATOR_FOREACH(child, it) {
                res = !var_cmpz(child);
                VAR_DECR_REF(child);
                if (res && which == V_ANY)
                        break;
                if (!res && which == V_ALL)
                        break;
        }
        VAR_DECR_REF(it);
        *status = child == ErrorVar ? RES_ERROR : RES_OK;
        return res;
}

bool
var_all(Object *v, enum result_t *status)
{
        return var_all_or_any(v, status, V_ALL);
}

bool
var_any(Object *v, enum result_t *status)
{
        return var_all_or_any(v, status, V_ANY);
}

enum { V_MIN, V_MAX };

static Object *
var_min_or_max(Object *v, int minmax)
{
        Object *it, *child, *res;

        it = iterator_get(v);
        if (!it) {
                err_iterable(v, minmax == V_MIN ? "min" : "max");
                return ErrorVar;
        }

        res = NULL;
        ITERATOR_FOREACH(child, it) {
                int cmp;
                if (!res) {
                        res = child;
                        continue;
                }
                if (var_compare(child, res, &cmp) == RES_ERROR) {
                        VAR_DECR_REF(child);
                        VAR_DECR_REF(res);
                        res = ErrorVar;
                        break;
                }
                if ((minmax == V_MIN && cmp < 0)
                    || (minmax == V_MAX && cmp > 0)) {
                        VAR_DECR_REF(res);
                        res = child;
                        continue;
                }
                VAR_DECR_REF(child);
        }
        VAR_DECR_REF(it);
        if (child == ErrorVar)
                return ErrorVar;
        if (!res) {
                err_setstr(ValueError, "%s(): object is empty",
                           minmax == V_MIN ? "min" : "max");
                return ErrorVar;
        }
        return res;
}

Object *
var_min(Object *v)
{
        return var_min_or_max(v, V_MIN);
}

Object *
var_max(Object *v)
{
        return var_min_or_max(v, V_MAX);
}

Object *
var_lnot(Object *v)
{
        bool cond = var_cmpz(v);
        return intvar_new((int)cond);
}

Object *
var_logical_or(Object *a, Object *b)
{
        bool res = !var_cmpz(a);
        res = res || !var_cmpz(b);
        return intvar_new((int)res);
}

Object *
var_logical_and(Object *a, Object *b)
{
        bool res = !var_cmpz(a);
        res = res && !var_cmpz(b);
        return intvar_new((int)res);
}

/* return true if @class is a class or base class of @instance */
bool
var_instanceof(Object *instance, Object *class)
{
        return isvar_instance(instance) &&
               instance_instanceof(instance, class);
}

/**
 * var_traverse - Common wrapper to iterator functions
 * @sequential: An iterable object to traverse
 * @action:     Action to perform for each item in @sequential
 * @data:       Local data to pass as argument to @action
 * @fname:      UAPI function name, if applicable.  Used for
 *              reporting error on iterator_errget()
 *
 * Return: RES_OK or RES_ERROR.  If @action returns RES_ERROR, then the
 *      traversal will quit early and return RES_ERROR back to the caller.
 */
enum result_t
var_traverse(Object *sequential,
             enum result_t (*action)(Object *, void *),
             void *data,
             const char *fname)
{
        Object *it;
        enum result_t ret;
        if ((it = iterator_errget(sequential, fname)) == ErrorVar)
                return RES_ERROR;
        ret = iterator_foreach(it, action, data);
        VAR_DECR_REF(it);
        return ret;
}

