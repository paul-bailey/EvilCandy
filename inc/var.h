/*
 * var.h - definitions for the generic struct var_t, and var.c API
 *
 * Included by evilcandy.h so you shouldn't need to include this directly
 */
#ifndef EVILCANDY_VAR_H
#define EVILCANDY_VAR_H

/*
 * Keep this zero unless you are trying to debug an excess VAR_DECR_REF
 * somewhere.  Twice now, seeing if any .rodata were getting freed before
 * their parent struct was helped narrow down the bug quickly.
 */
#define DEBUG_MISSING_RODATA 0

/**
 * struct var_t - User variable type
 * @v_type:   Pointer to class methods et al for this data type.
 * @v_refcnt: DON'T TOUCH THIS! Use VAR_INCR_REF and VAR_DECR_REF instead
 *
 * built-in types have their own XXXXvar_t struct, which embeds this at
 * the very top, so they can be de-referenced with a simple cast.
 *
 * These are allocated with var_new.  After that, VAR_INCR/DECR_REF
 * is used to produce or consume a reference.
 */
struct var_t {
        struct type_t *v_type;
        union {
                void *v_dummy; /* keep v_refcnt same size as v_type */
#if DEBUG_MISSING_RODATA
                struct {
                        short v_rodata;
                        short v_refcnt;
                };
#else
                /* signed for easier bug trapping */
                int v_refcnt;
#endif
        };
};

/**
 * struct seqvar_t - User variable type for sequential types
 * @v_size:  Number of sequential elements.
 *              (Do not confuse this with @base.v_type->size,
 *              which is the typedef's fixed allocation size in bytes.)
 *
 * IMPORTANT!
 *      See typedef.h - If a type's struct type_t has either of its
 *      .sqm or .mpm fields set, then:
 *        1. it MUST embed this struct at the top of its internal-use
 *           struct instead of just struct var_t.
 *        2. it must maintain this v_size field and keep it updated
 */
struct seqvar_t {
        struct var_t base;
        size_t v_size;
};

/* only call these if you already know @v's type */
static inline size_t seqvar_size(struct var_t *v)
        { return ((struct seqvar_t *)v)->v_size; }
static inline void seqvar_set_size(struct var_t *v, size_t size)
        { ((struct seqvar_t *)v)->v_size = size; }

#define VAR_INCR_REF(v) do { (v)->v_refcnt++; } while (0)
#define VAR_DECR_REF(v) do {      \
        struct var_t *v_ = (v);   \
        v_->v_refcnt--;           \
        if (v_->v_refcnt <= 0)    \
                var_delete__(v_); \
} while (0)

/*
 * VAR_SANITY - keep this a macro so I can tell where the bug was
 * trapped I'd like to also sanity-check v_->v_type, but that's
 * probably too many checks for even debug mode.
 */
#ifndef NDEBUG
# define VAR_SANITY(v_) do {                            \
        struct var_t *v__ = (v_);                       \
        if (!v__) {                                     \
                DBUG("unexpected NULL var");            \
                bug();                                  \
        }                                               \
        if (v__->v_refcnt <= 0) {                       \
                DBUG("v_refcnt=%d", v__->v_refcnt);     \
                bug();                                  \
        }                                               \
} while (0)
#else
# define VAR_SANITY(v_) do { (void)0; } while (0)
#endif

extern struct var_t *var_new(struct type_t *type);
extern void var_initialize_type(struct type_t *tp);
extern struct var_t *var_getattr(struct var_t *v,
                                 struct var_t *deref);
extern enum result_t var_setattr(struct var_t *v,
                                 struct var_t *deref,
                                 struct var_t *attr);
extern int var_compare(struct var_t *a, struct var_t *b);
extern int var_sort(struct var_t *v);
extern struct var_t *var_str(struct var_t *v);
extern ssize_t var_len(struct var_t *v);
extern bool var_cmpz(struct var_t *v, enum result_t *status);
extern struct var_t *var_lnot(struct var_t *v);
extern const char *typestr(struct var_t *v);
extern const char *typestr_(int magic);
extern const char *attr_str(struct var_t *deref);
/* common hashtable callback for var-storing hashtables */
extern void var_bucket_delete(void *data);
/* note: v only evaluated once in VAR_*_REF() */
extern void var_delete__(struct var_t *v);


#endif /* EVILCANDY_VAR_H */
