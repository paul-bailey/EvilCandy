/*
 * var.h - definitions for the generic Object, and var.c API
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
 *      See objtypes.h - If a type's struct type_t has either of its
 *      .sqm or .mpm fields set, then:
 *        1. it MUST embed this struct at the top of its internal-use
 *           struct instead of just Object.
 *        2. it must maintain this v_size field and keep it updated
 */
struct seqvar_t {
        Object base;
        size_t v_size;
};

/* only call these if you already know @v's type */
static inline size_t seqvar_size(Object *v)
        { return ((struct seqvar_t *)v)->v_size; }
static inline void seqvar_set_size(Object *v, size_t size)
        { ((struct seqvar_t *)v)->v_size = size; }

/* note: v only evaluated once in VAR_*_REF() */
#define VAR_INCR_REF(v) do { (v)->v_refcnt++; } while (0)
#define VAR_DECR_REF(v) do {      \
        Object *v_ = (v);         \
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
        Object *v__ = (v_);                             \
        if (!v__) {                                     \
                DBUG1("unexpected NULL var");           \
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

extern Object *var_new(struct type_t *type);
extern void var_initialize_type(struct type_t *tp);
extern Object *var_getattr(Object *v, Object *deref);
extern bool var_hasattr(Object *haystack, Object *needle);
extern enum result_t var_setattr(Object *v, Object *deref, Object *attr);
extern int var_compare(Object *a, Object *b);
extern bool var_compare_iarg(Object *a, Object *b, int iarg);
extern int var_sort(Object *v);
extern Object *var_str(Object *v);
extern Object *var_str_swap(Object *v);
extern bool var_cmpz(Object *v, enum result_t *status);
extern bool var_all(Object *v, enum result_t *status);
extern bool var_any(Object *v, enum result_t *status);
extern Object *var_listify(Object *v);
extern Object *var_tuplify(Object *v);
extern Object *var_min(Object *v);
extern Object *var_max(Object *v);
extern Object *var_lnot(Object *v);
extern Object *var_logical_or(Object *a, Object *b);
extern Object *var_logical_and(Object *a, Object *b);
extern enum result_t seqvar_arg2idx(Object *obj, Object *iarg, int *idx);
extern Object *var_foreach_generic(Frame *fr);
extern void var_delete__(Object *v);

/* var_from_format.c */
extern Object *var_from_format(const char *fmt, ...);


#endif /* EVILCANDY_VAR_H */
