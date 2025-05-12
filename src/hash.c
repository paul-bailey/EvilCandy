#include <evilcandy.h>

static hash_t
fnv_cstring_hash(const char *key)
{
        /* 64-bit version */
#define FNV_PRIME      0x00000100000001B3LL
#define FNV_OFFSET     0xCBF29CE484222325LL

        const unsigned char *s = (unsigned char *)key;
        unsigned int c;
        unsigned long hash = FNV_PRIME;

        bug_on(sizeof(hash_t) != 8);

        /*
         * since C string has no zeros in the part that gets
         * hashed, don't worry about sticky state.
         */
        while ((c = *s++) != '\0')
                hash = (hash * FNV_OFFSET) ^ c;

        /* interpret zero as 'not calculated' */
        if (hash == 0)
                hash++;

        return (hash_t)hash;
}

/*
 * calc_string_hash - The FNV-1a hash algorithm
 *
 * See Wikipedia article "Fowler-Noll-Vo hash function"
 */
hash_t
calc_string_hash(Object *key)
{
        bug_on(!isvar_string(key));
        return fnv_cstring_hash(string_cstring(key));
}


