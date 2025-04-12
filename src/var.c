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
#  define REGISTER_FREE()  do { var_nalloc--; } while (0)
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
        struct var_t var;
};

#define list2memvar(li) container_of(li, struct var_mem_t, list)
#define var2memvar(v) container_of(v, struct var_mem_t, var)

static void
var_more_(void)
{
        enum { NPERBLK = 64 };
        int i;
        struct var_mem_t *blk = emalloc(sizeof(*blk) * NPERBLK);
        for (i = 0; i < NPERBLK; i++) {
                blk[i].list = var_freelist;
                var_freelist = &blk[i];
        }
}

static struct var_t *
var_alloc(void)
{
        struct var_mem_t *vm;
        REGISTER_ALLOC();
        if (!var_freelist)
                var_more_();

        vm = list2memvar(var_freelist);
        var_freelist = vm->list;
        vm->var.refcount = 1;
        return &vm->var;
}

static void
var_free(struct var_t *v)
{
        struct var_mem_t *vm;
        REGISTER_FREE();
#ifndef NDEBUG
        if (v->refcount != 0) {
                DBUG("expected refcount=0 but it's %d\n", v->refcount);
                bug();
        }
#endif
        vm = var2memvar(v);
        vm->list = var_freelist;
        var_freelist = vm;
}

/**
 * var_new - Get a new empty variable
 */
struct var_t *
var_new(void)
{
        struct var_t *v = var_alloc();
        /* var_alloc took care of refcount already */
        v->v_type = &EmptyType;
        return v;
}

/**
 * var_delete - Delete a variable.
 * @v: variable to delete.
 */
void
var_delete__(struct var_t *v)
{
        bug_on(v == NullVar);
        var_reset(v);
        var_free(v);
}

/**
 * var_reset - Empty a variable
 * @v: variable to empty.
 *
 * This does not delete the variable.
 */
void
var_reset(struct var_t *v)
{
        bug_on(v == NullVar);
        if (v->v_type->opm->reset)
                v->v_type->opm->reset(v);

        v->v_type = &EmptyType;
        /* don't touch refcount here */
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

static struct var_t *
builtin_method(struct var_t *v, const char *method_name)
{
        if (!method_name)
                return NULL;

        return hashtable_get(&v->v_type->methods, method_name);
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
                &StrptrType,
                &XptrType,
                &ObjectType,
                &StringType,
                NULL,
        };

        int i;
        for (i = 0; init_tbl[i] != NULL; i++) {
                struct type_t *tp = init_tbl[i];
                hashtable_init(&tp->methods, fnv_hash,
                               str_key_match, var_bucket_delete);
                if (tp->cbm)
                        config_builtin_methods(tp->cbm, &tp->methods);
        }

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

static struct var_t *
attr_by_string(struct var_t *v, const char *s)
{
        if (!s)
                return NULL;
        if (isvar_dict(v)) {
                struct var_t *res;
                if ((res = object_getattr(v, s)) != NULL)
                        return res;
        }
        return builtin_method(v, s);
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
        if (isvar_strptr(key)) {
                return attr_by_string(v, key->strptr);
        } else if (isvar_int(key)) {
                /* XXX: return ErrorVar if bound error? */
                /* idx stores long long, but ii.i is int */
                if (key->i < INT_MIN || key->i > INT_MAX)
                        return NULL;

                if (isvar_array(v))
                        return array_child(v, key->i);
                else if (isvar_string(v))
                        return string_nth_child(v, key->i);
        } else if (isvar_string(key)) {
                return attr_by_string(v, string_get_cstring(key));
        }

        return NULL;
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

        if (isvar_strptr(key)) {
                strncpy(numbuf, key->strptr, sizeof(numbuf)-1);
        } else if (isvar_string(key)) {
                strncpy(numbuf, string_get_cstring(key),
                        sizeof(numbuf)-1);
        } else if (isvar_int(key)) {
                sprintf(numbuf, "%lld", key->i);
        } else {
                strcpy(numbuf, "<!bug>");
        }
        return numbuf;
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
        if (isvar_dict(v))
                return object_setattr(v, key, attr);
        else if (isvar_array(v))
                return array_insert(v, key, attr);
        else
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
        if (a->v_type->opm->cmp == NULL)
                return a < b ? -1 : 1;
        return a->v_type->opm->cmp(a, b);
}

const char *
typestr(struct var_t *v)
{
        return v->v_type->name;
}

