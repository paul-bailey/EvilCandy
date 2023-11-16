#include "egq.h"

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

void
qstack_push(struct qvar_t *v)
{
        struct qvar_t *to = qstack_getpush();
        qop_mov(to, v);
}


