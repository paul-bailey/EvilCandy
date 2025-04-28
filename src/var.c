#include <evilcandy.h>
#include <stdlib.h> /* for atexit */

/*
 * If 1 and !NDEBUG, splash some debug data about var allocation
 * to stderr upon exit.
 */
#define REPORT_VARS_ON_EXIT 1

#ifdef NDEBUG
# undef REPORT_VARS_ON_EXIT
# define REPORT_VARS_ON_EXIT 0
#endif /* NDEBUG */

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

#ifndef NDEBUG
static size_t var_alloc_size = 0;
static size_t var_nalloc = 0;
# define REGISTER_ALLOC(n_) do {        \
        var_nalloc++;                   \
        var_alloc_size += (n_);         \
} while (0)
# define REGISTER_FREE(n_)  do {        \
        bug_on((int)var_nalloc <= 0);   \
        var_alloc_size -= (n_);         \
        var_nalloc--;                   \
} while (0)
# if REPORT_VARS_ON_EXIT
static void
var_alloc_tell(void)
{
        DBUG("%s: #bytes outstanding: %lu", __FILE__, (long)var_alloc_size);
        DBUG("%s: #vars outstanding:  %lu", __FILE__, (long)var_nalloc);
}
# endif /* REPORT_VARS_ON_EXIT */
#else /* NDEBUG */
# define REGISTER_ALLOC(x) do { (void)0; } while (0)
# define REGISTER_FREE(x)  do { (void)0; } while (0)
#endif /* NDEBUG */

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
        bug_on(v == NullVar);
        bug_on(v->v_refcnt != 0);
        bug_on(!v->v_type);
        if (v->v_type->reset)
                v->v_type->reset(v);

        var_free(v);
}

static void
config_builtin_methods(const struct type_inittbl_t *tbl_arr,
                       Object *dict)
{
        const struct type_inittbl_t *t = tbl_arr;
        while (t->name != NULL) {
                Object *v, *k;
                enum result_t res;

                v = funcvar_new_intl(t->fn, t->minargs, t->maxargs);
                k = stringvar_new(t->name);
                res = dict_setattr_exclusive(dict, k, v);
                VAR_DECR_REF(k);

                bug_on(res != RES_OK);
                (void)res;

                t++;
        }
}

/*
 * Given extern linkage so it can be called for modules that have
 * data types which don't need to be visible outside their little
 * corner of the interpreter.
 * The major players are forward-declared in typedefs.h and added
 * to VAR_TYPES_TBL[] below in moduleinit_var.
 */
void
var_initialize_type(struct type_t *tp)
{
        tp->methods = dictvar_new();
        if (tp->cbm)
                config_builtin_methods(tp->cbm, tp->methods);
}

static struct type_t *const VAR_TYPES_TBL[] = {
        &ArrayType,
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
        &RangeType,
        &UuidptrType,
        &FileType,
        NULL,
};

/*
 * see main.c - this must be after all the typedef code
 * has had their moduleinit functions called, or it will fail.
 */
void
moduleinit_var(void)
{
        int i;
        for (i = 0; VAR_TYPES_TBL[i] != NULL; i++)
                var_initialize_type(VAR_TYPES_TBL[i]);

#if REPORT_VARS_ON_EXIT
        atexit(var_alloc_tell);
#endif
}

void
moduledeinit_var(void)
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

/**
 * var_bucket_delete - Hash table callback for several modules.
 * @data: Expected to be a Object that was created with var_new()
 */
void
var_bucket_delete(void *data)
{
        VAR_SANITY((Object *)data);
        VAR_DECR_REF((Object *)data);
}

/*
 * Helper to var_setattr/var_getattr
 *
 * Convert @idx, where something like '[-3]' means '[size-3]',
 * and make sure the result is within range for @v.
 *
 * Return: converted index or -1 if out of range.
 */
static int
var_realindex(Object *v, long long idx)
{
        int i;
        size_t n;

        bug_on(!hasvar_len(v));

        /* idx stores long long, but ii.i is int */
        if (idx < INT_MIN || idx > INT_MAX)
                return -1;

        i = (int)idx;
        n = seqvar_size(v);

        /* convert '[-i]' to '[size-i]' */
        if (i < 0)
                i += n;
        return i < n ? i : -1;
}

/**
 * var_getattr - Generalized get-attribute
 * @v:  Variable whose attribute we're seeking
 * @key: Variable storing the key, either the name or an index number
 *
 * Return: Attribute of @v, or NULL if not found.  This is the actual
 * attribute, not a copy, so be careful what you do with it.
 *
 * This gets the equivalent to the EvilCandy expression: v[key]
 *
 * ONLY vm.c SHOULD USE THIS!  It accesses a dictionary which may not
 * yet exist during initialization, but which will be available by the
 * time the VM is running.
 */
Object *
var_getattr(Object *v, Object *key)
{
        if (isvar_int(key)) {
                Object *ret;
                int i;
                const struct seq_methods_t *sqm = v->v_type->sqm;
                if (!sqm || !sqm->getitem)
                        goto badtype;

                i = var_realindex(v, intvar_toll(key));
                if (i < 0) {
                        err_index(key);
                        return ErrorVar;
                }
                ret = sqm->getitem(v, i);
                bug_on(!ret);
                return ret;
        } else if (isvar_string(key)) {
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

                /* still here? try built-ins */
                ret = dict_getattr(v->v_type->methods, key);
                if (ret)
                        goto found;

                if (v->v_type->getattr)
                        ret = v->v_type->getattr(v, string_get_cstring(key));

                if (!ret) {
                        err_setstr(KeyError, "Object has no attribute %s",
                                   string_get_cstring(key));
                        return ErrorVar;
                }

found:
                /*
                 * Save "owner" with function, so when it's called it
                 * knows who its "this" is.
                 *
                 * FIXME: Lots of Object creation/destruction going on
                 * here.  Maybe I'm not too cool to create a class syntax
                 * after all.  There's like less than a 50/50 chance the
                 * function will use "this" anyway.
                 */
                if (isvar_function(ret)) {
                        Object *tmp = ret;
                        ret = methodvar_new(tmp, v);
                        VAR_DECR_REF(tmp);
                }
                return ret;
        }

        /* else, invalid key, fall through */
badtype:
        err_attribute("get", key, v);
        return ErrorVar;
}

bool
var_hasattr(Object *haystack, Object *needle)
{
        const struct seq_methods_t *sqm = haystack->v_type->sqm;
        const struct map_methods_t *mpm = haystack->v_type->mpm;
        if (sqm && sqm->hasitem)
                return sqm->hasitem(haystack, needle);
        if (mpm && mpm->hasitem)
                return mpm->hasitem(haystack, needle);
        return false;
}

/**
 * var_set_attr - Generalized set-attribute
 * @v:          Variable whose attribute we're setting
 * @key:      Variable storing the index number or name
 * @attr:       Variable storing the attribute to set.  This will be
 *              copied, so calling function still must handle GC for this
 * Return:      RES_OK if success, RES_ERROR if failure does not exist.
 *
 * This implements x[key] = attr;
 */
enum result_t
var_setattr(Object *v, Object *key, Object *attr)
{
        if (isvar_string(key)) {
                const char *ks = string_get_cstring(key);
                const struct map_methods_t *map = v->v_type->mpm;
                if (!map || !map->setitem)
                        goto badtype;
                if (!ks || ks[0] == '\0') {
                        err_setstr(KeyError, "key may not be empty");
                        return RES_ERROR;
                }
                return map->setitem(v, key, attr);
        } else if (isvar_int(key)) {
                int i;
                const struct seq_methods_t *seq = v->v_type->sqm;
                if (!seq || !seq->setitem)
                        goto badtype;

                i = var_realindex(v, intvar_toll(key));
                if (i < 0) {
                        err_index(key);
                        return RES_ERROR;
                }

                return seq->setitem(v, i, attr);
        } else {
                goto badtype;
        }

badtype:
        err_attribute("set", key, v);
        return RES_ERROR;
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
            && !(isnumvar(a) && isnumvar(b))) {
                return strcmp(typestr(a), typestr(b));
        }
        if (!a->v_type->cmp)
                return a < b ? -1 : 1;
        return a->v_type->cmp(a, b);
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

ssize_t
var_len(Object *v)
{
        if (!hasvar_len(v))
                return -1;
        return seqvar_size(v);
}

const char *
typestr(Object *v)
{
        return v->v_type->name;
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

Object *
var_lnot(Object *v)
{
        int status;
        bool cond = var_cmpz(v, &status);
        if (status)
                return NULL;
        return intvar_new((int)cond);
}


