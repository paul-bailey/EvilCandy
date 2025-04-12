#ifndef TYPES_PRIV_H
#define TYPES_PRIV_H

#include <evilcandy.h>
#include <typedefs.h>
#include <uarg.h>

/*
 * can't just be a-b, because if they're floats, a non-zero result
 * might cast to 0
 */
#define OP_CMP(a_, b_) (a_ == b_ ? 0 : (a_ < b_ ? -1 : 1))

#define V_INITTBL(n, cb, m, M) \
        { .name = n, .fn = cb, .minargs = m, .maxargs = M }

#define TBLEND { .name = NULL }

/* XXX: should be called type_methods_t or something */
/* XXX: should be in typedefs.h */
struct type_inittbl_t {
        const char *name;
        struct var_t *(*fn)(struct vmframe_t *);
        int minargs;
        int maxargs;
};



#endif /* TYPES_PRIV_H */
