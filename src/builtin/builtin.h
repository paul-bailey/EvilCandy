#ifndef EGQ_BUILTIN_H
#define EGQ_BUILTIN_H

#include <evilcandy.h>

#define TOFTBL(n, func, m, M) \
        { .magic = TYPE_FUNCTION, .name = n, \
                .cb = func, .minargs = m, .maxargs = M }

#define TOOTBL(n, p) \
        { .magic = TYPE_DICT, .name = n, .tbl = p }

/* the following are for consts, always */
#define TOITBL(n, iv) \
        { .magic = TYPE_INT, .name = n, .i = iv }
#define TOFLTB(n, fv) \
        { .magic = TYPE_FLOAT, .name = n, .f = fv }
#define TOSTBL(n, str) \
        { .magic = TYPE_STRING, .name = n, .s = str }

#define TBLEND { .name = NULL }

struct inittbl_t {
        int magic;
        const char *name;
        union {
                struct {
                        Object *(*cb)(Frame *);
                        int minargs;
                        int maxargs;
                };
                const struct inittbl_t *tbl;
                long long i;
                double f;
                const char *s;
        };
};

/* builtin.c */

/* string.c */
extern void bi_moduleinit_string__(void);
extern void bi_moduleinit_object__(void);

/* io.c */
extern const struct inittbl_t bi_io_inittbl__[];

/* math.c */
extern const struct inittbl_t bi_math_inittbl__[];

#endif /* EGQ_BUILTIN_H */
