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
# if LIST_ALLOC
static struct var_mem_t *var_freelist = NULL;
struct var_mem_t {
        struct var_mem_t *list;
#  ifndef NDEBUG
        bool freed;
#  endif
        struct var_t var;
};

#ifndef NDEBUG
# define MARK_FREE(vm) do { bug_on((vm)->freed); (vm)->freed = true; } while (0)
# define MARK_USED(vm) do { bug_on(!(vm)->freed); (vm)->freed = false; } while (0)
#else
# define MARK_FREE(vm) do { (void)0; } while (0)
# define MARK_USED(vm) do { (void)0; } while (0)
#endif

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
#ifndef NDEBUG
                blk[i].freed = true;
#endif
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
        MARK_USED(vm);
        return &vm->var;
}

static void
var_free(struct var_t *v)
{
        REGISTER_FREE();
        struct var_mem_t *vm = var2memvar(v);
        vm->list = var_freelist;
        MARK_FREE(vm);
        var_freelist = vm;
}

# else
static struct mempool_t *var_mempool = NULL;
static struct var_t *
var_alloc(void)
{
        /*
         * check this here rather than do at modulinit time,
         * because some vars will need to have been allocated
         * already by the time our moduleinit has been called.
         */
        if (!var_mempool)
                var_mempool = mempool_new(sizeof(struct var_t));

        struct var_t *ret = mempool_alloc(var_mempool);
        REGISTER_ALLOC();
        return ret;
}

static void
var_free(struct var_t *v)
{
        REGISTER_FREE();
        mempool_free(var_mempool, v);
}
# endif
#endif

/**
 * var_init - Initialize a variable
 * @v: Variable to initialize.
 *
 * DO NOT call this on a struct you got from var_new(),
 * or you might clobber and zombify data.  Instead, call
 * it for a newly-declared struct on the stack.
 *
 * return: @v
 */
static struct var_t *
var_init(struct var_t *v)
{
        v->magic = TYPE_EMPTY;
        v->flags = 0;
        return v;
}

/**
 * var_new - Get a new initialized, empty, and unattached variable
 */
struct var_t *
var_new(void)
{
        return var_init(var_alloc());
}

/**
 * var_delete - Delete a variable.
 * @v: variable to delete.
 */
void
var_delete(struct var_t *v)
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

/* tbl may be NULL, the others may not */
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

#define list2wrapper(li) container_of(li, struct var_wrapper_t, siblings)

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
        var_delete((struct var_t *)data);
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
 */
struct var_t *
var_get_attr(struct var_t *v, struct var_t *deref)
{
        if (v->magic == TYPE_VARPTR)
                v = v->vptr;

        /* we should not have double pointers */
        bug_on(v->magic == TYPE_VARPTR);

        switch (deref->magic) {
        case TYPE_STRPTR:
                return attr_by_string_l(v, deref->strptr);
        case TYPE_INT:
                /* because idx stores long long, but ii.i is int */
                if (deref->i < INT_MIN || deref->i > INT_MAX)
                        return NULL;
                if (v->magic == TYPE_LIST)
                        return array_child(v, deref->i);
                else if (v->magic == TYPE_DICT)
                        return object_nth_child(v, deref->i);
        case TYPE_STRING:
                return attr_by_string(v, string_get_cstring(deref));
        }

        return NULL;
}

/**
 * var_set_attr - Generalized set-attribute
 * @v:          Variable whose attribute we're setting
 * @deref:      Variable storing the index number or name
 * @attr:       Variable storing the attribute to set.  This will be
 *              copied, so calling function still must handle GC for this
 * Return:
 * 0 if success, -1 if failure.  A syntax error may be thrown if the
 * MOV operation is not permitted due to string typing.
 */
int
var_set_attr(struct var_t *v, struct var_t *deref, struct var_t *attr)
{
        struct var_t *child = var_get_attr(v, deref);
        if (!child)
                return -1;
        qop_mov(child, attr);
        return 0;
}

/* for debugging and builtin functions */
const char *
typestr(int magic)
{
        if (magic < 0 || magic >= NTYPES)
                return "[bug]";
        return TYPEDEFS[magic].name;
}


