#include <evilcandy.h>
#include <typedefs.h>
#include "types/var.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/*
 * Variable allocation:
 *      SIMPLE_ALLOC = 1        Use stdlib's malloc() and free().
 *                              LIST_ALLOC = don't-care.
 *
 *      SIMPLE_ALLOC = 0
 *              LIST_ALLOC = 1  Use a linked list, and whenever I run out
 *                              allocate a block of them (don't keep track
 *                              of the base pointers, I'll just keep them
 *                              forever), and put them in a linked list of
 *                              available variable structs.
 *
 *              LIST_ALLOC = 0  Use the memblk.c API
 *
 * 12/2023 update: With -x option set, the list method clearly is the
 * winner.  The memblk code, which seemed to do great when total vars
 * was less than 64, does NOT perform well under pressure.
 */
#define SIMPLE_ALLOC 0
#define LIST_ALLOC 1

#ifndef NDEBUG
static size_t var_nalloc = 0;
# define REGISTER_ALLOC() do { \
        var_nalloc += sizeof(struct var_t); \
} while (0)
# define REGISTER_FREE() do { \
        var_nalloc -= sizeof(struct var_t); \
} while (0)
static void
var_alloc_tell(void)
{
        fprintf(stderr, "var_alloc_size = %lu\n", (long)var_nalloc);
}
#else /* NDEBUG */
# define REGISTER_ALLOC() do { (void)0; } while (0)
# define REGISTER_FREE() do { (void)0; } while (0)
#endif /* NDEBUG */

#if SIMPLE_ALLOC
static struct var_t *
var_alloc(void)
{
        struct var_t *ret = emalloc(sizeof(struct var_t));
        REGISTER_ALLOC();
        return ret;
}

static void
var_free(struct var_t *v)
{
        REGISTER_FREE();
        free(v);
}
#else
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
        bug_on(v->refcount != 0);
        vm = var2memvar(v);
        vm->list = var_freelist;
        var_freelist = vm;
}

#endif /* !SIMPLE_ALLOC */

/**
 * var_new - Get a new empty variable
 */
struct var_t *
var_new(void)
{
        struct var_t *v = var_alloc();
        /* var_alloc took care of refcount already */
        v->magic = TYPE_EMPTY;
        v->flags = 0;
        return v;
}

/**
 * var_delete - Delete a variable.
 * @v: variable to delete.
 */
void
var_delete__(struct var_t *v)
{
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
        if ((unsigned)v->magic < NTYPES_USER) {
                void (*rst)(struct var_t *) = TYPEDEFS[v->magic].opm->reset;
                if (rst)
                        rst(v);
        }

        v->magic = TYPE_EMPTY;
        v->flags = 0;
        /* don't touch refcount here */
}

struct type_t TYPEDEFS[NTYPES];

static void
config_builtin_methods(const struct type_inittbl_t *tbl,
                       struct hashtable_t *htbl)
{
        const struct type_inittbl_t *t = tbl;
        while (t->name != NULL) {
                struct var_t *v = var_new();

                function_init_internal(v, t->fn, t->minargs, t->maxargs);
                hashtable_put(htbl, literal_put(t->name), v);
                t++;
        }
}

/**
 * var_config_type - Initialization-time function to set up a
 *                   built-in type's metadata
 * @magic:      A TYPE_* enum
 * @name:       Name of the type, useful for things like error reporting
 * @opm:        Operator methods, ie. what to do when encountering things
 *              like '+', '-', '%'...
 * @tbl:        Table of additional built-in methods that can be called
 *              by name from a user script.
 *
 * This ic called once for each built-in type, see the typedefinit_*()
 * functions in types/...c
 *
 * tbl may be NULL, the other arguments may not
 */
void
var_config_type(int magic, const char *name,
                const struct operator_methods_t *opm,
                const struct type_inittbl_t *tbl)
{
        bug_on(magic >= NTYPES);
        bug_on(TYPEDEFS[magic].opm || TYPEDEFS[magic].name);
        TYPEDEFS[magic].opm = opm;
        TYPEDEFS[magic].name = name;
        if (tbl)
                config_builtin_methods(tbl, &TYPEDEFS[magic].methods);
}

static struct var_t *
builtin_method_l(struct var_t *v, const char *method_name)
{
        int magic = v->magic;
        if ((unsigned)magic >= NTYPES_USER || !method_name)
                return NULL;

        return hashtable_get(&TYPEDEFS[magic].methods, method_name);
}

/*
 * see main.c - this must be after all the typedef code
 * has had their moduleinit functions called, or it will fail.
 */
void
moduleinit_var(void)
{
        static const struct initfn_tbl_t {
                void (*cb)(void);
        } INIT_TBL[] = {
                { typedefinit_array },
                { typedefinit_empty },
                { typedefinit_float },
                { typedefinit_function },
                { typedefinit_integer },
                { typedefinit_object },
                { typedefinit_string },
                { typedefinit_intl },
                { NULL },
        };
        const struct initfn_tbl_t *t;
        int i;

        for (i = TYPE_EMPTY; i < NTYPES; i++) {
                hashtable_init(&TYPEDEFS[i].methods, ptr_hash,
                                ptr_key_match, var_bucket_delete);
        }
        for (t = INIT_TBL; t->cb != NULL; t++)
                t->cb();

        /*
         * Make sure we didn't miss anything, we don't want
         * blanks in TYPEDEFS[]
         */
        for (i = 0; i < NTYPES; i++) {
                if (TYPEDEFS[i].name == NULL)
                        bug();
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
        VAR_DECR_REF((struct var_t *)data);
}

static struct var_t *
attr_by_string_l(struct var_t *v, const char *s)
{
        if (!s)
                return NULL;
        if (v->magic == TYPE_DICT) {
                struct var_t *res;
                if ((res = object_child_l(v, s)) != NULL)
                        return res;
        }
        return builtin_method_l(v, s);
}

static struct var_t *
attr_by_string(struct var_t *v, const char *s)
{
        return attr_by_string_l(v, literal(s));
}

/**
 * var_get_attr_by_string_l - Get attribte by string literal
 * @v: Variable whose attribute we're seeking
 * @s: Name of attribute, which must have been a return value of literal
 *
 * Return: Attribute @s of @v or NULL if not found.
 */
struct var_t *
var_get_attr_by_string_l(struct var_t *v, const char *s)
{
        return attr_by_string_l(v, s);
}

/**
 * var_get_attr - Generalized get-attribute
 * @v:  Variable whose attribute we're seeking
 * @deref: Variable storing the key, either the name or an index number
 *
 * Return: Attribute of @v, or NULL if not found.  This is the actual
 * attribute, not a copy, so be careful what you do with it.
 */
struct var_t *
var_get_attr(struct var_t *v, struct var_t *deref)
{
        switch (deref->magic) {
        case TYPE_STRPTR:
                return attr_by_string_l(v, deref->strptr);
        case TYPE_INT:
                /* because idx stores long long, but ii.i is int */
                if (deref->i < INT_MIN || deref->i > INT_MAX)
                        return NULL;
                switch (v->magic) {
                case TYPE_LIST:
                        return array_child(v, deref->i);
                case TYPE_DICT:
                        return object_nth_child(v, deref->i);
                case TYPE_STRING:
                        return string_nth_child(v, deref->i);
                }
        case TYPE_STRING:
                return attr_by_string(v, string_get_cstring(deref));
        }

        return NULL;
}

/**
 * Get the name or subscript (as text) of an attribute.
 * @deref: Variable storing the name or subscript
 *
 * Return:
 * C string naming the attribute.  This may point to a buffer whose
 * contents could change later.  This function is just for error
 * reporting.
 */
const char *
attr_str(struct var_t *deref)
{
        static char numbuf[64];

        memset(numbuf, 0, sizeof(numbuf));

        switch (deref->magic) {
        case TYPE_STRPTR:
                strncpy(numbuf, deref->strptr, sizeof(numbuf)-1);
                break;
        case TYPE_STRING:
                strncpy(numbuf, string_get_cstring(deref),
                        sizeof(numbuf)-1);
                break;
        case TYPE_INT:
                sprintf(numbuf, "%lld", deref->i);
                break;
        default:
                strcpy(numbuf, "<!bug>");
                break;
        }
        return numbuf;
}

/**
 * var_set_attr - Generalized set-attribute
 * @v:          Variable whose attribute we're setting
 * @deref:      Variable storing the index number or name
 * @attr:       Variable storing the attribute to set.  This will be
 *              copied, so calling function still must handle GC for this
 * Return:
 * 0 if success, -1 if failure does not exist.  A syntax error will be
 * thrown if the attribute exists but the MOV operation is not permitted
 * for the type.
 */
int
var_set_attr(struct var_t *v, struct var_t *deref, struct var_t *attr)
{
        char *attrstr = NULL;
        int idx = 0;
        switch (deref->magic) {
        case TYPE_STRPTR:
                attrstr = deref->strptr;
                goto set_by_str;
        case TYPE_INT:
                if (deref->i < INT_MIN || deref->i > INT_MAX)
                        return -1;
                idx = deref->i;
                goto set_by_idx;
        case TYPE_STRING:
                attrstr = string_get_cstring(deref);
                if (attrstr)
                        attrstr = literal(attrstr);
                if (!attrstr)
                        return -1;
                goto set_by_str;
        }
        return -1;

set_by_str:
        if (v->magic == TYPE_DICT) {
                struct var_t *child = object_child_l(v, attrstr);
                if (child)
                        qop_mov(child, attr);
                else
                        object_add_child(v, attr, attrstr);
                return 0;
        }
        /*
         * currently no other native types have attributes that are
         * settable by name.
         */
        return -1;


set_by_idx:
        if (v->magic == TYPE_LIST) {
                array_set_child(v, idx, attr);
                return 0;
        }
        /*
         * currently no other native types have attributes that are
         * settable by subscript.  (TODO: allow this for strings)
         */
        return -1;
}

/* for debugging and builtin functions */
const char *
typestr(struct var_t *v)
{
        int magic = v->magic;
        if (magic < 0 || magic >= NTYPES)
                return "[bug]";
        return TYPEDEFS[magic].name;
}


