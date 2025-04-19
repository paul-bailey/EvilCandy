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
        DBUG("%s: __gbl__ refcnt:     %d",  __FILE__, GlobalObject->v_refcnt);
        DBUG("%s: 'null' refcnt:      %d",  __FILE__, NullVar->v_refcnt);
        DBUG("%s: ErrorVar refcnt:    %d",  __FILE__, NullVar->v_refcnt);
        DBUG("%s: #bytes outstanding: %lu", __FILE__, (long)var_alloc_size);
        DBUG("%s: #vars outstanding:  %lu", __FILE__, (long)var_nalloc);
}
# endif /* REPORT_VARS_ON_EXIT */
#else /* NDEBUG */
# define REGISTER_ALLOC() do { (void)0; } while (0)
# define REGISTER_FREE()  do { (void)0; } while (0)
#endif /* NDEBUG */

static struct var_t *
var_alloc(struct type_t *type)
{
        struct var_t *ret;
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
        ret = (struct var_t *)(vm + 1);
        memset(ret, 0, type->size);
        return ret;
}

static void
var_free(struct var_t *v)
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
struct var_t *
var_new(struct type_t *type)
{
        struct var_t *v;
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
var_delete__(struct var_t *v)
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
                       struct var_t *dict)
{
        const struct type_inittbl_t *t = tbl_arr;
        while (t->name != NULL) {
                struct var_t *v, *k;
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
 * to init_tbl[] below in moduleinit_var.
 */
void
var_initialize_type(struct type_t *tp)
{
        tp->methods = dictvar_new();
        if (tp->cbm)
                config_builtin_methods(tp->cbm, tp->methods);
}

/*
 * see main.c - this must be after all the typedef code
 * has had their moduleinit functions called, or it will fail.
 */
void
moduleinit_var(void)
{
        static struct type_t *const init_tbl[] = {
                &ArrayType,
                &EmptyType,
                &FloatType,
                &FunctionType,
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

        int i;
        for (i = 0; init_tbl[i] != NULL; i++)
                var_initialize_type(init_tbl[i]);

#if REPORT_VARS_ON_EXIT
        atexit(var_alloc_tell);
#endif
}

/**
 * var_bucket_delete - Hash table callback for several modules.
 * @data: Expected to be a struct var_t that was created with var_new()
 */
void
var_bucket_delete(void *data)
{
        VAR_SANITY((struct var_t *)data);
        VAR_DECR_REF((struct var_t *)data);
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
var_realindex(struct var_t *v, long long idx)
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
struct var_t *
var_getattr(struct var_t *v, struct var_t *key)
{
        if (isvar_int(key)) {
                int i;
                const struct seq_methods_t *sqm = v->v_type->sqm;
                if (!sqm || !sqm->getitem) {
                        /* not a sequential type */
                        return NULL;
                }
                i = var_realindex(v, intvar_toll(key));
                if (i < 0)
                        return NULL;
                return sqm->getitem(v, i);
        } else if (isvar_string(key)) {
                /*
                 * first check if v maps it. If failed, check the
                 * built-in methods.
                 */
                struct var_t *ret;
                const struct map_methods_t *mpm = v->v_type->mpm;
                if (mpm && mpm->getitem) {
                        ret = mpm->getitem(v, key);
                        if (ret)
                                return ret;
                }

                /* still here? try built-ins */
                ret = dict_getattr(v->v_type->methods, key);
                if (ret)
                        VAR_INCR_REF(ret);
                return ret;
        }

        return NULL;
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
var_setattr(struct var_t *v, struct var_t *key, struct var_t *attr)
{
        if (isvar_string(key)) {
                const char *ks = string_get_cstring(key);
                const struct map_methods_t *map = v->v_type->mpm;
                if (!map || !map->setitem)
                        return RES_ERROR;
                if (!ks || ks[0] == '\0')
                        return RES_ERROR;
                return map->setitem(v, key, attr);
        } else if (isvar_int(key)) {
                int i;
                const struct seq_methods_t *seq = v->v_type->sqm;
                if (!seq || !seq->setitem)
                        return RES_ERROR;

                i = var_realindex(v, intvar_toll(key));
                if (i < 0)
                        return RES_ERROR;

                return seq->setitem(v, i, attr);
        } else {
                return RES_ERROR;
        }
}

/**
 * Get the name or subscript (as text) of an attribute.
 * @key: Variable storing the name or subscript
 *
 * Return:
 * C string naming the attribute.
 * This may point to a buffer whose contents could change later.
 * Used for error reporting.
 */
const char *
attr_str(struct var_t *key)
{
        /* FIXME: No! Just no! */
        static char numbuf[64];

        memset(numbuf, 0, sizeof(numbuf));

        if (isvar_string(key)) {
                strncpy(numbuf, string_get_cstring(key),
                        sizeof(numbuf)-1);
        } else if (isvar_int(key)) {
                sprintf(numbuf, "%lld", intvar_toll(key));
        } else {
                strcpy(numbuf, "<!bug>");
        }
        return numbuf;
}

/**
 * var_compare - Compare two variables, used for sorting et al.
 * @a: First variable to compare.
 * @b: Second variable to compare.
 *
 * Return: -1 if "a < b", 1 if "a > b", and 0 if "a == b".
 * This is not (necessarily) a pointer comparison.  Each typedef
 * has their own method of comparison.
 */
int
var_compare(struct var_t *a, struct var_t *b)
{
        if (a == b)
                return 0;
        if (a == NULL)
                return -1;
        if (b == NULL)
                return 1;
        if (a->v_type != b->v_type)
                return strcmp(typestr(a), typestr(b));
        if (!a->v_type->cmp)
                return a < b ? -1 : 1;
        return a->v_type->cmp(a, b);
}

int
var_sort(struct var_t *v)
{
        if (!v->v_type->sqm || !v->v_type->sqm->sort)
                return -1;
        v->v_type->sqm->sort(v);
        return 0;
}

/* Used for built-in print function to express a variable */
struct var_t *
var_str(struct var_t *v)
{
        /* every data type should have this */
        bug_on(!v->v_type->str);
        return v->v_type->str(v);
}

ssize_t
var_len(struct var_t *v)
{
        if (!hasvar_len(v))
                return -1;
        return seqvar_size(v);
}

const char *
typestr(struct var_t *v)
{
        return v->v_type->name;
}

/**
 * var_cmpz - Compare @v to zero, NULL, or something like it
 * @status:  To be set to RES_ERROR if cmpz not permitted,
 *           RES_OK otherwise.  This may not be NULL.
 *
 * Return: if @v is...
 *      empty:          true always
 *      integer:        true if zero
 *      float:          true if 0.0 exactly
 *      string:         true if null or even if "", false otherwise
 *      object:         false always, even if empty
 *      anything else:  false or error
 */
bool
var_cmpz(struct var_t *v, enum result_t *status)
{
        if (!v->v_type->cmpz) {
                err_permit("cmpz", v);
                *status = RES_ERROR;
                return true;
        }
        *status = RES_OK;
        return v->v_type->cmpz(v);
}

struct var_t *
var_lnot(struct var_t *v)
{
        int status;
        bool cond = var_cmpz(v, &status);
        if (status)
                return NULL;
        return intvar_new((int)cond);
}


