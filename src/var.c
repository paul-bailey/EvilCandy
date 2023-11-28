#include "egq.h"
#include "types/var.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Note: using mempool.c for allocation of vars does seem
 * to be about 25% faster.
 */
#define SIMPLE_ALLOC 0
#if SIMPLE_ALLOC
static struct var_t *
var_alloc(void)
{
        struct var_t *ret = emalloc(sizeof(struct var_t));
        return ret;
}

static void
var_free(struct var_t *v)
{
        free(v);
}
#else
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

        return mempool_alloc(var_mempool);
}

static void
var_free(struct var_t *v)
{
        mempool_free(var_mempool, v);
}
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
        v->name = NULL;
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
 *
 * Note: Calling code should deal with v->name before calling this.
 * var_new didn't set the name, so it won't free it either.
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
        bug_on(v->magic >= Q_NMAGIC);

        void (*rst)(struct var_t *) = TYPEDEFS[v->magic].opm->reset;
        if (rst)
                rst(v);

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
                v->name = literal_put(t->name);
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

#define list2wvar(li) \
        (container_of(li, struct var_wrapper_t, siblings)->v)

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
        struct var_t *w;

        bug_on(magic < 0 || magic > Q_NMAGIC);

        methods = &TYPEDEFS[magic].methods;
        list_foreach(m, methods) {
                w = list2wvar(m);
                if (w->name == method_name)
                        return w;
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

