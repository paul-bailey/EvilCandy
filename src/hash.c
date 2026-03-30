#include <evilcandy.h>

/* 64-bit version */
#define FNV_PRIME      0x00000100000001B3LL
#define FNV_OFFSET     0xCBF29CE484222325LL

/**
 * fnv_hash - The FNV-1a hash algorithm
 * See Wikipedia article "Fowler-Noll-Vo hash function"
 */
hash_t
fnv_hash(const void *ptr, size_t size)
{
        const unsigned char *s = (unsigned char *)ptr;
        const unsigned char *end = s + size;
        hash_t hash = FNV_PRIME;

        /* FIXME: config.h should wrap 32-bit vs 64-bit hash algos */
        bug_on(sizeof(hash_t) != 8);

        /*
         * XXX: s could have embedded zeros,
         * I need to worry about sticky state.
         */
        while (s < end) {
                unsigned int c = *s++;
                hash = (hash * FNV_OFFSET) ^ c;
        }

        return good_hash(hash);
}

/*
 * More involved algorithms are located with their type sources
 * in types/XXX.c
 */

/* Do not use for objects with embedded pointers */
hash_t
calc_object_hash_generic(Object *key)
{
        /* Do not hash refcnt etc */
        void *ptr = (void *)((char *)key + sizeof(Object));
        size_t size = key->v_type->size - sizeof(Object);
        return fnv_hash(ptr, size);
}

/**
 * calculate hash of a double-precision floating point number.
 * Result will be same as with integer if d has no fractional component
 */
hash_t
double_hash(double d)
{
        double ival;
        if (modf(d, &ival) == 0.0)
                return good_hash((hash_t)ival);
        /*
         * XXX: lots of zeros in bitmap of d, we need a better hash
         * algo than this.
         */
        return fnv_hash(&d, sizeof(d));
}

