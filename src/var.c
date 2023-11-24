#include "egq.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static struct mempool_t *var_mempool = NULL;

static struct var_t *
var_alloc(void)
{
        if (!var_mempool)
                var_mempool = mempool_new(sizeof(struct var_t));

        return mempool_alloc(var_mempool);
}

static void
var_free(struct var_t *v)
{
        mempool_free(var_mempool, v);
}

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

static void
string_reset(struct var_t *str)
{
        buffer_free(&str->s);
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

        void (*rst)(struct var_t *) = TYPEDEFS[v->magic].reset;
        if (rst)
                rst(v);

        v->magic = QEMPTY_MAGIC;
        v->flags = 0;
}

struct type_t TYPEDEFS[Q_NMAGIC] = {
        { .name = "empty" },
        { .name = "object",     .reset = object_reset__, },
        { .name = "function" },
        { .name = "float" },
        { .name = "int" },
        { .name = "string",     .reset = string_reset, },
        { .name = "pointer" },
        { .name = "built_in_function" },
        { .name = "array",      .reset = array_reset__,},
};


