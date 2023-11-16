/* q-builtin.c - Built-in callbacks for script */
#include "egq.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static struct qvar_t *
getarg(int n)
{
        if (n < 0 || n >= (q_.sp - 1 - q_.fp))
                return NULL;
        return (struct qvar_t *)(q_.fp + 1 + n);
}

static void
qb_typeof(struct qvar_t *ret)
{
        struct qvar_t *p = getarg(0);
        qop_assign_cstring(ret, q_typestr(p->magic));
}

static bool
qb_print_helper(struct qvar_t *v)
{
        switch (v->magic) {
        case QINT_MAGIC:
                printf("%lld", v->i);
                break;
        case QFLOAT_MAGIC:
                printf("%g", v->f);
                break;
        case QEMPTY_MAGIC:
                printf("(null)");
                break;
        case QSTRING_MAGIC:
                printf("%s", v->s.s);
                break;
        default:
                return false;
        }
        return true;
}

static void
qb_print(struct qvar_t *ret)
{
        struct qvar_t *p = getarg(0);
        int lastarg = 0;
        if (p->magic == QSTRING_MAGIC) {
                char *s = p->s.s;
                while (*s != '\0') {
                        if (*s == '{') {
                                struct qvar_t *q = NULL;
                                ++s;
                                if (*s == '}') {
                                        lastarg++;
                                        q = getarg(lastarg);
                                        ++s;
                                } else if (isdigit(*s)) {
                                        char *endptr;
                                        int i = strtoul(s, &endptr, 10);
                                        if (*endptr == '}') {
                                                q = getarg(i);
                                                lastarg = i + 1;
                                                s = endptr + 1;
                                        }
                                }

                                if (q) {
                                        if (qb_print_helper(q))
                                                continue;
                                }

                        }
                        putchar((int)*s);
                        s++;
                }
        } else {
                qb_print_helper(p);
        }
        /* return empty */
}

struct qb_tbl_t {
        struct qvar_t v;
        const struct qfunc_intl_t h;
        const char *name;
};

#define TOTBL(n, cb, m, M) \
        { .name = n, .h = { .fn = cb, .minargs = m, .maxargs = M }}

static struct qb_tbl_t BUILTIN_LUT[] = {
        TOTBL("PRINT", qb_print, 1, -1),
        TOTBL("typeof", qb_typeof, 1, 1),
        { .name = NULL },
};

void
q_builtin_initlib(void)
{
        struct qb_tbl_t *t;
        for (t = BUILTIN_LUT; t->name != NULL; t++) {
                qvar_init(&t->v);
                t->v.name = q_literal(t->name);
                t->v.magic = QINTL_MAGIC;
                t->v.fni = &t->h;
        }
}

struct qvar_t *
q_builtin_seek(const char *key)
{
        struct qb_tbl_t *t;
        for (t = BUILTIN_LUT; t->name != NULL; t++) {
                if (!strcmp(key, t->name))
                        return &t->v;
        }
        return NULL;
}
