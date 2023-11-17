#include "egq.h"

/**
 * qstack_pop - Pop a variable out of the stack
 * @to: Variable to qop_mov the popped variable's data into
 */
void
qstack_pop(struct qvar_t *to)
{
        bug_on(q_.sp <= &q_.stack[0]);
        q_.sp--;
        if (to)
                qop_mov(to, q_.sp);

        /* Don't free name, it's in literal heaven now */
        if (q_.sp->name)
                q_.sp->name = NULL;

        qvar_reset(q_.sp);
}

/**
 * qstack_getpush - Get next unused stack variable and advance
 *                  SP accordingly
 *
 * Use this instead of qstack_push if you need a variable to be in a
 * certain location on the stack but you cannot fill it yet.
 */
struct qvar_t *
qstack_getpush(void)
{
        struct qvar_t *res = q_.sp;
        if (res >= &q_.stack[QSTACKMAX])
                qsyntax("Stack overflow");
        ++q_.sp;
        qvar_init(res);
        return res;
}

/**
 * qstack_push - Push a variable onto the stack
 * @v: Variable to push.
 */
void
qstack_push(struct qvar_t *v)
{
        struct qvar_t *to = qstack_getpush();
        qop_mov(to, v);
}


