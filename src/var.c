#include "egq.h"
#include "types/var.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
static struct list_t var_freelist = LIST_INIT(&var_freelist);
struct var_mem_t {
        struct list_t list;
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
                list_init(&blk[i].list);
                list_add_tail(&blk[i].list, &var_freelist);
        }
}

static struct var_t *
var_alloc(void)
{
        struct var_mem_t *vm;
        REGISTER_ALLOC();
        if (list_is_empty(&var_freelist)) {
                var_more_();
        }
        vm = list2memvar(var_freelist.next);
        list_remove(&vm->list);
        return &vm->var;
}

static void
var_free(struct var_t *v)
{
        REGISTER_FREE();
        list_add_tail(&(var2memvar(v)->list), &var_freelist);
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
struct var_t *
var_init(struct var_t *v)
{
        v->magic = QEMPTY_MAGIC;
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
 * @v: variable to delete.  If @v was just a temporary struct declared
 *      on the stack, call var_reset() only, not this.
 */
void
var_delete(struct var_t *v)
{
        var_reset(v);
        /* XXX REVISIT: we didn't set v->name, so we're not freeing it */
        var_free(v);
}

/**
 * var_copy - like qop_mov, but in the case of an object,
 *              all of @from's elements will be re-instantiated
 *              into @to
 */
void
var_copy(struct var_t *to, struct var_t *from)
{
        warning("%s not supported yet", __FUNCTION__);
        bug();
}

/**
 * var_reset - Empty a variable
 * @v: variable to empty.
 *
 * This does not change @v's name
 */
void
var_reset(struct var_t *v)
{
        if ((unsigned)v->magic < Q_NMAGIC) {
                void (*rst)(struct var_t *) = TYPEDEFS[v->magic].opm->reset;
                if (rst)
                        rst(v);
        }

        v->magic = QEMPTY_MAGIC;
        v->flags = 0;
}

struct type_t TYPEDEFS[Q_NMAGIC];

static void
config_builtin_methods(const struct type_inittbl_t *tbl,
                       struct list_t *parent_list)
{
        const struct type_inittbl_t *t = tbl;
        while (t->name != NULL) {
                /*
                 * raw malloc, sure, we only need a few of these,
                 * once at startup
                 */
                struct var_wrapper_t *w = emalloc(sizeof(*w));
                struct var_t *v = var_new();

                function_init_internal(v, t->fn, t->minargs, t->maxargs);
                w->name = literal_put(t->name);
                w->v = v;
                list_init(&w->siblings);
                list_add_tail(&w->siblings, parent_list);
                t++;
        }
}

/* tbl may be NULL, the others may not */
void
var_config_type(int magic, const char *name,
                const struct operator_methods_t *opm,
                const struct type_inittbl_t *tbl)
{
        TYPEDEFS[magic].opm = opm;
        TYPEDEFS[magic].name = name;
        if (tbl)
                config_builtin_methods(tbl, &TYPEDEFS[magic].methods);
}

#define list2wrapper(li) container_of(li, struct var_wrapper_t, siblings)

/**
 * builtin_method - Return a built-in method for a variable's type
 * @v: Variable to check
 * @method_name: Name of method, which MUST have been a return value
 *              of literal().
 *
 * Return: Built-in method matching @name for @v, or NULL otherwise.
 *      This is the actual reference, not a copy. DO NOT CLOBBER IT.
 */
struct var_t *
builtin_method(struct var_t *v, const char *method_name)
{
        int magic = v->magic;
        struct list_t *methods, *m;

        if ((unsigned)magic >= Q_NMAGIC)
                return NULL;

        methods = &TYPEDEFS[magic].methods;
        list_foreach(m, methods) {
                struct var_wrapper_t *w = list2wrapper(m);
                if (w->name == method_name)
                        return w->v;
        }
        return NULL;
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
                { NULL },
        };
        const struct initfn_tbl_t *t;
        int i;

        for (i = QEMPTY_MAGIC; i < Q_NMAGIC; i++)
                list_init(&TYPEDEFS[i].methods);
        for (t = INIT_TBL; t->cb != NULL; t++)
                t->cb();

        for (i = 0; i < Q_NMAGIC; i++) {
                if (TYPEDEFS[i].name == NULL)
                        bug();
        }
#ifndef NDEBUG
        atexit(var_alloc_tell);
#endif
}

/* copy of the handle, not the whole shebang */
struct var_t *
var_copy_of(struct var_t *v)
{
        struct var_t *cp = var_new();
        if (v)
                qop_mov(cp, v);
        return cp;
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

