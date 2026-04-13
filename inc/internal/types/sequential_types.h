#ifndef EVC_INC_INTERNAL_TYPES_SEQUENTIAL_TYPES_H
#define EVC_INC_INTERNAL_TYPES_SEQUENTIAL_TYPES_H

#include <internal/type_protocol.h>

struct arrayvar_t {
        struct seqvar_t base;
        Object **items;
        int lock;
        size_t alloc_size;
};

struct tuplevar_t {
        struct seqvar_t base;
        hash_t hash;
        Object **items;
};

/* XXX: Putting bytesvar in "sequential_types.h" may be misleading */
struct bytesvar_t {
        struct seqvar_t base;
        hash_t hash;
        unsigned char *b_buf;
};

/* Warning!! Only call these if you already type-checked @v */
static inline Object **array_get_data(Object *v)
        { return ((struct arrayvar_t *)v)->items; }
static inline Object **tuple_get_data(Object *v)
        { return ((struct tuplevar_t *)v)->items; }
static inline unsigned char *bytes_get_data(Object *v)
        { return ((struct bytesvar_t *)v)->b_buf; }

/* only call if isvar_seq_readable() is true */
static inline Object *seqvar_getitem(Object *v, size_t i)
        { return v->v_type->sqm->getitem(v, i); }

/* only call if index has been checked */
static inline Object *tuple_borrowitem_(Object *v, size_t i)
        { return ((struct tuplevar_t *)v)->items[i]; }


#endif /* EVC_INC_INTERNAL_TYPES_SEQUENTIAL_TYPES_H */
