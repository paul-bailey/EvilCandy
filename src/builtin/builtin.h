#ifndef EGQ_BUILTIN_H
#define EGQ_BUILTIN_H

#include <egq.h>

#define TOFTBL(n, cb, m, M) \
        { .magic = QPTRXI_MAGIC, .name = n, \
                .h = { .fn = cb, .minargs = m, .maxargs = M }}

#define TOOTBL(n, p) \
        { .magic = QOBJECT_MAGIC, .name = n, .tbl = p }

/* the following are for consts, always */
#define TOITBL(n, iv) \
        { .magic = QINT_MAGIC, .name = n, .i = iv }
#define TOFLTB(n, fv) \
        { .magic = QFLOAT_MAGIC, .name = n, .f = fv }
#define TOSTBL(n, str) \
        { .magic = QSTRING_MAGIC, .name = n, .s = str }

#define TBLEND { .name = NULL }

struct inittbl_t {
        int magic;
        const char *name;
        union {
                struct func_intl_t h;
                const struct inittbl_t *tbl;
                long long i;
                double f;
                const char *s;
        };
};

struct var_wrapper_t {
        struct list_t siblings;
        struct var_t *v;
};

static inline struct var_t *
getarg(int n)
{
        if (n < 0 || n >= (q_.sp - 1 - q_.fp))
                return NULL;
        return q_.fp + 1 + n;
}

#define arg_type_err(v, want) do { \
        syntax("Argument is type '%s' but '%s' is expected", \
                typestr((v)->magic), typestr(want)); \
} while (0)

#define arg_type_check(v, want) do { \
        if ((v)->magic != (want)) \
                arg_type_err(v, want); \
} while (0)

/* builtin.c */
extern void bi_init_type_methods__(const struct inittbl_t *tbl,
                                   int magic);
extern void bi_build_internal_object__(struct var_t *parent,
                                       const struct inittbl_t *tbl);

/* string.c */
extern void bi_moduleinit_string__(void);
extern void bi_moduleinit_object__(void);

/* io.c */
extern const struct inittbl_t bi_io_inittbl__[];

/* math.c */
extern const struct inittbl_t bi_math_inittbl__[];

#endif /* EGQ_BUILTIN_H */
