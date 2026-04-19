/*
 * var.h - definitions for the generic Object, and var.c API
 *
 * Included by evilcandy.h so you shouldn't need to include this directly
 */
#ifndef EVILCANDY_VAR_H
#define EVILCANDY_VAR_H

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h> /*< ssize_t */

#include <evilcandy/typedefs.h>
#include <evilcandy/enums.h>

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

extern void var_delete__(Object *v);
static inline void
VAR_INCR_REF(Object *v)
{
        v->v_refcnt++;
}

static inline void
VAR_DECR_REF(Object *v)
{
        v->v_refcnt--;
        if (v->v_refcnt <= 0)
                var_delete__(v);
}

static inline Object *
VAR_NEW_REF(Object *v)
{
        VAR_INCR_REF(v);
        return v;
}

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

extern enum result_t var_setattr(Frame *frame, Object *obj,
                                 Object *key, Object *value);
extern Object *var_getattr(Frame *frame, Object *obj, Object *key);
extern Object *var_getattr_or_null(Frame *frame, Object *obj, Object *key);
extern bool var_hasattr(Frame *frame, Object *obj, Object *key);
static inline enum result_t var_delattr(Frame *frame, Object *obj, Object *key)
        { return var_setattr(frame, obj, key, NULL); }

extern Object *var_getitem(Object *obj, Object *key);
extern bool var_hasitem(Object *container, Object *item);
extern enum result_t var_setitem(Object *obj, Object *key, Object *value);
static inline enum result_t var_delitem(Object *obj, Object *key)
        { return var_setitem(obj, key, NULL); }

extern enum result_t var_compare(Object *a, Object *b, int *result);
extern bool var_matches(Object *a, Object *b);
extern enum result_t var_compare_iarg(Object *a, Object *b,
                                      int iarg, bool *result);
extern int var_sort(Object *v);
extern Object *var_str(Object *v);
extern Object *var_str_swap(Object *v);
extern bool var_cmpz(Object *v);
extern bool var_all(Object *v, enum result_t *status);
extern bool var_any(Object *v, enum result_t *status);
extern Object *var_min(Object *v);
extern Object *var_max(Object *v);
extern Object *var_lnot(Object *v);
extern Object *var_logical_or(Object *a, Object *b);
extern Object *var_logical_and(Object *a, Object *b);
extern enum result_t var_index_capi(size_t size, ssize_t *a, ssize_t *b,
                                    enum errhandler_t errhandler);
extern size_t var_slice_size(ssize_t start, ssize_t stop, ssize_t step);
extern bool var_instanceof(Object *instance, Object *class);

extern void var_lock(void);
extern void var_unlock(void);
extern enum result_t var_traverse(
                        Object *sequential,
                        enum result_t (*action)(Object *, void *),
                        void *data, const char *fname);

/* var_from_format.c */
extern Object *var_from_format(const char *fmt, ...);
extern hash_t var_hash(Object *v);

/*
 * TODO: This would go in an internal/XXXX.h, except I intend
 * to swap locations of some functionality in var.c and class.c
 * so that they sit in more appropriate files.
 */
extern void var_type_clear_freelist(struct type_t *tp);

#endif /* EVILCANDY_VAR_H */
