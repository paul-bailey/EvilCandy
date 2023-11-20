#include "egq.h"

/**
 * stack_pop - Pop a variable out of the stack
 * @to: Variable to qop_mov the popped variable's data into
 */
void
stack_pop(struct var_t *to)
{
        bug_on(q_.sp <= &q_.stack[0]);
        q_.sp--;
        if (to)
                qop_mov(to, q_.sp);

        /* Don't free name, it's in literal heaven now */
        if (q_.sp->name)
                q_.sp->name = NULL;

        var_reset(q_.sp);
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
        struct var_t *res = q_.sp;
        if (res >= &q_.stack[QSTACKMAX])
                syntax("Stack overflow");
        ++q_.sp;
        var_init(res);
        return res;
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


