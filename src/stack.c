#include "egq.h"

static struct var_t *tstack, *tsp;

static void
stack_pop_(struct var_t *to, struct var_t *stack, struct var_t **spp)
{
        struct var_t *sp = *spp;
        bug_on(sp <= stack);
        sp--;
        if (to)
                qop_mov(to, sp);

        /* Don't free name, it's in literal heaven now */
        if (sp->name)
                sp->name = NULL;

        var_reset(sp);
        *spp = sp;
}

static struct var_t *
stack_getpush_(struct var_t *stack, struct var_t **spp)
{
        struct var_t *res = *spp;
        if (res >= &stack[QSTACKMAX])
                syntax("Stack overflow");
        *spp = res + 1;
        var_init(res);
        return res;
}

/**
 * stack_pop - Pop a variable out of the stack
 * @to: Variable to qop_mov the popped variable's data into
 */
void
stack_pop(struct var_t *to)
{
        stack_pop_(to, q_.stack, &q_.sp);
}

/**
 * stack_getpush - Get next unused stack variable and advance
 *                  SP accordingly
 *
 * Use this instead of stack_push if you need a variable to be in a
 * certain location on the stack but you cannot fill it yet.
 */
struct var_t *
stack_getpush(void)
{
        return stack_getpush_(q_.stack, &q_.sp);
}

/**
 * stack_push - Push a variable onto the stack
 * @v: Variable to push.
 */
void
stack_push(struct var_t *v)
{
        struct var_t *to = stack_getpush();
        qop_mov(to, v);
}

/**
 * tstack_... Like stack_..., but for unnamed temporary variables.
 * eval() code should call this.  Theoretically, these can both use the
 * same stack.  However, by separating the stack, it keeps the stack
 * searching in symbol_seek() quicker, because it doesn't have to skip
 * over all those unnamed variables that may have built up since the last
 * change to the frame pointer.
 */

void
tstack_pop(struct var_t *to)
{
        stack_pop_(to, tstack, &tsp);
}

struct var_t *
tstack_getpush(void)
{
        return stack_getpush_(tstack, &tsp);
}

void
tstack_push(struct var_t *v)
{
        struct var_t *to = tstack_getpush();
        qop_mov(to, v);
}

void
moduleinit_stack(void)
{
        q_.stack = emalloc(QSTACKMAX);
        tstack = emalloc(QSTACKMAX);
        q_.sp = q_.stack;
        tsp = tstack;
}
