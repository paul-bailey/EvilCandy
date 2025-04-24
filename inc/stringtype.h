/*
 * stringtype.h - StringType handle
 *
 * This would be kept private to types/string.c,
 * but it's actually handy for dictionary lookups
 * to have access to .s_info.ascii_len and .s_hash
 */
#ifndef EVILCANDY_STRINGTYPE_H
#define EVILCANDY_STRINGTYPE_H

#include <evilcandy.h>

struct stringvar_t {
        struct seqvar_t base;
        char *s;        /* the actual C string */
        struct utf8_info_t s_info;
        hash_t s_hash;
};

static inline bool
string_hash(Object *v)
{
        return ((struct stringvar_t *)v)->s_hash;
}

/* may be different from seqvar_size if not entirely ASCII */
static inline size_t
string_nbytes(Object *v)
{
        return ((struct stringvar_t *)v)->s_info.ascii_len;
}

/**
 * string_update_hash - Update string var with hash calculation.
 *
 * This doesn't truly affect the string, so it's not considered a
 * violation of immutability.  The only reason it doesn't happen at
 * stringvar_new() time is because we don't know yet if we're going to
 * need it.  It could be something getting added to .rodata, in which
 * calculating hash right at startup should be no big deal.  But it
 * could also be some rando stack variable that gets created and
 * destroyed every time a certain function is called and returns,
 * and which is never used in a way that requires the hash.  So we
 * let the calling code decide whether to update the hash or not.
 */
static inline hash_t
string_update_hash(Object *v)
{
        struct stringvar_t *vs = (struct stringvar_t *)v;
        if (vs->s_hash == (hash_t)0)
                vs->s_hash = calc_string_hash(v);
        return vs->s_hash;
}

#endif /* EVILCANDY_STRINGTYPE_H */
