#ifndef EGQ_VAR_H
#define EGQ_VAR_H

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
struct type_inittbl_t {
        const char *name;
        int (*fn)(struct var_t *);
        int minargs;
        int maxargs;
};

/*
 * PRIVATE STRUCT
 *
 * preheader to return values of
 * type_handle_new, TYPE_HANDLE_INCR_REF, TYPE_HANDLE_DECR_REF
 */
struct type_handle_preheader_t_ {
        void (*destructor)(void *);
        int nref;
};

/* array.c */
extern void typedefinit_array(void);

/* empty.c */
extern void typedefinit_empty(void);

/* float.c */
extern void typedefinit_float(void);

/* function.c */
extern void typedefinit_function(void);

/* integer.c */
extern void typedefinit_integer(void);

/* intl.c */
extern void typedefinit_intl(void);

/* object.c */
extern void typedefinit_object(void);

/* string.c */
extern void typedefinit_string(void);

/* ../var.c */
extern void var_config_type(int magic, const char *name,
                            const struct operator_methods_t *opm,
                            const struct type_inittbl_t *tbl);

/* typehandle.c */
extern void *type_handle_new(size_t size, void (*destructor)(void *));

/*
 * Call this for MOV operations, but not after type_handle_new,
 * because it was already incremented to one there.
 */
#define TYPE_HANDLE_INCR_REF(h) \
        do { TYPE_HANDLE_PREHEADER(h)->nref++; } while (0)

/*
 * To be called from a struct var_t's destructor method
 */
#define TYPE_HANDLE_DECR_REF(h) do { \
        struct type_handle_preheader_t_ *ph = TYPE_HANDLE_PREHEADER(h); \
        ph->nref--; \
        if (ph->nref <= 0) \
                type_handle_destroy__(ph); \
} while (0)

/* private */
#define TYPE_HANDLE_PREHEADER(h) \
        (&((struct type_handle_preheader_t_ *)(h))[-1])
extern void type_handle_destroy__(struct type_handle_preheader_t_ *h);


#endif /* EGQ_VAR_H */
