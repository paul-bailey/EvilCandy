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
        if (res >= &stack[STACK_MAX])
                syntax("Stack overflow");
        *spp = res + 1;
        var_init(res);
        return res;
}

/**
 * stack_pop - Pop a variable out of the stack
 * @to: Variable to qop_mov the popped variable's data into
 */
struct var_t *
stack_pop(void)
{
        bug_on(q_.sp <= 0);
        q_.sp--;
        return q_.stack[q_.sp];
}

/**
 * stack_push - Push a variable onto the stack
 * @v: Variable to push.
 */
void
stack_push(struct var_t *v)
{
        if (q_.sp >= STACK_MAX)
                syntax("Stack overflow");
        q_.stack[q_.sp++] = v;
}

void
stack_unwind_to(int idx)
{
        bug_on(q_.sp < idx);
        bug_on(idx < 0);
        while (q_.sp > idx)
                var_delete(stack_pop());
}

void
stack_unwind_to_frame(void)
{
        stack_unwind_to(q_.fp);
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
        /* q_.stack is pointer array, tstack is regular array */
        q_.stack = emalloc(STACK_MAX * sizeof(struct var_t *));
        tstack = emalloc(STACK_MAX * sizeof(struct var_t));
        q_.sp = 0;
        tsp = tstack;
}
