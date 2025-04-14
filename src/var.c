#include <evilcandy.h>
#include <typedefs.h>
#include "types/types_priv.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef NDEBUG
   static size_t var_nalloc = 0;
#  define REGISTER_ALLOC() do { var_nalloc++; } while (0)
#  define REGISTER_FREE()  do { \
        bug_on((int)var_nalloc <= 0); \
        var_nalloc--;  \
} while (0)
   static void
   var_alloc_tell(void)
   {
           DBUG("#vars outstanding: %lu", (long)var_nalloc);
   }
#else /* NDEBUG */
#  define REGISTER_ALLOC() do { (void)0; } while (0)
#  define REGISTER_FREE()  do { (void)0; } while (0)
#endif /* NDEBUG */

static struct var_mem_t *var_freelist = NULL;
struct var_mem_t {
        struct var_mem_t *list;
        union {
                struct var_t var;
                unsigned char sizeassert[24];
        };
};

#define var2memvar(v) container_of(v, struct var_mem_t, var)

/*
 * TODO: this only speeds things up for integers and floats,
 * but not strings and functions, which are the other most
 * commonly alloc'd and freed variables.
 */
#define SMALL_VAR_SIZE 24

static struct var_t *
var_alloc(size_t size)
{
        struct var_t *ret;
        REGISTER_ALLOC();

        if (size <= SMALL_VAR_SIZE) {
                /* empty, int, float */
                struct var_mem_t *vm = var_freelist;
                if (!vm) {
                        vm = ecalloc(sizeof(*vm));
                } else {
                        var_freelist = vm->list;
                        vm->list = NULL;
                        memset(&vm->var, 0, sizeof(vm->var));
                }
                ret = &vm->var;
        } else {
                /* strings, dictionaries, functions... */
                ret = ecalloc(size);
        }
        return ret;
}

static void
var_free(struct var_t *v, size_t size)
{
        REGISTER_FREE();
        if (size <= SMALL_VAR_SIZE) {
                struct var_mem_t *vm = container_of(v, struct var_mem_t, var);
                vm->list = var_freelist;
                var_freelist = vm;
        } else {
                free(v);
        }
}

/*
 * TODO: var_new and var_delete__: when v->v_type->size <= 24
 *       use a linked-list allocation method.
 */
/**
 * var_new - Get a new empty variable
 */
struct var_t *
var_new(struct type_t *type)
{
        struct var_t *v;
        bug_on(type->size == 0);

        v = var_alloc(type->size);
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
#warning "remove DBUG splash"
if (v->v_refcnt!=0)DBUG("type %s ref=%d", typestr(v), v->v_refcnt);
        bug_on(v->v_refcnt != 0);
        bug_on(!v->v_type);
        if (v->v_type->reset)
                v->v_type->reset(v);

        var_free(v, v->v_type->size);
}

static void
config_builtin_methods(const struct type_inittbl_t *tbl,
                       struct hashtable_t *htbl)
{
        const struct type_inittbl_t *t = tbl;
        while (t->name != NULL) {
                struct var_t *v;

                v = funcvar_new_intl(t->fn, t->minargs, t->maxargs);
                hashtable_put(htbl, literal_put(t->name), v);
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
        hashtable_init(&tp->methods, fnv_hash,
                       str_key_match, var_bucket_delete);
        if (tp->cbm)
                config_builtin_methods(tp->cbm, &tp->methods);
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
                &ObjectType,
                &StringType,
                &RangeType,
                &UuidptrType,
                NULL,
        };

        int i;
        for (i = 0; init_tbl[i] != NULL; i++)
                var_initialize_type(init_tbl[i]);

#ifndef NDEBUG
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
        n = ((struct seqvar_t *)v)->v_size;

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
                const char *ks = string_get_cstring(key);
                struct var_t *ret;
                const struct map_methods_t *mpm = v->v_type->mpm;
                if (!ks || ks[0] == '\0')
                        return NULL;
                if (mpm && mpm->getitem) {
                        ret = mpm->getitem(v, ks);
                        if (ret)
                                return ret;
                }

                /* still here? try built-ins */
                ret = hashtable_get(&v->v_type->methods, ks);
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
                return map->setitem(v, ks, attr);
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

ssize_t
var_len(struct var_t *v)
{
        if (!hasvar_len(v))
                return -1;
        return ((struct seqvar_t *)v)->v_size;
}

const char *
typestr(struct var_t *v)
{
        return v->v_type->name;
}

