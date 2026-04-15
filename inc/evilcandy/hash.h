#ifndef EVC_HASH_H
#define EVC_HASH_H

#include <evilcandy/typedefs.h>
#include <math.h>
#include <stddef.h>

#define HASH_ERROR      ((hash_t)-1)
#define HASH_NONE       ((hash_t)0)

static inline hash_t
good_hash(hash_t hash)
{
        return hash ? (hash == HASH_ERROR ? -2 : hash) : 1;
}

static inline hash_t
calc_ptr_hash(void *ptr)
{
        /* Don't return zero */
        if (ptr == NULL)
                return (hash_t)1;

        /*
         * Idea from EMACS source code. A pointer's hash is itself. This
         * is useful for objects which are defined to never match if they
         * are not the same instance.  But to reduce the chance of hash
         * table collisions, shift up the lowest bits, which are usually
         * zeros.
         */
        hash_t u = (hash_t)((uintptr_t)ptr);
        return good_hash(u << 4 | ((u & 15) >> (sizeof(uintptr_t)-4)));
}

/* hash.c */
extern hash_t fnv_hash(const void *ptr, size_t size);
extern hash_t calc_object_hash_generic(Object *key);
extern hash_t double_hash(double d);

#endif /* EVC_HASH_H */
