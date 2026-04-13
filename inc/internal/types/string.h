#ifndef EVC_INC_INTERNAL_TYPES_STRING_H
#define EVC_INC_INTERNAL_TYPES_STRING_H

#include <internal/type_protocol.h>
#include <internal/type_registry.h>

struct stringvar_t {
        struct seqvar_t base;
        char *s;                /* the UTF8-encoded C string */
        size_t s_ascii_len;     /* (misleading) # of bytes in .s */
        void *s_unicode;        /* == .s if .s_ascii is true */
        size_t s_width;         /* width of .s_unicode */
        int s_ascii;            /* true if ASCII */
        hash_t s_hash;          /* 0 until string_hash() call */
};

/*
 * string helpers - Only call these if you already type-checked @v
 */
/* may be different from seqvar_size if not entirely ASCII */
static inline size_t string_nbytes(Object *v)
        { return ((struct stringvar_t *)v)->s_ascii_len; }
static inline bool string_isascii(Object *v)
        { return !!((struct stringvar_t *)v)->s_ascii; }
static inline size_t string_width(Object *v)
        { return ((struct stringvar_t *)v)->s_width; }
static inline void *string_data(Object *v)
        { return ((struct stringvar_t *)v)->s_unicode; }


static inline const char *
string_cstring(Object *v)
{
        bug_on(!isvar_string(v));
        return ((struct stringvar_t *)v)->s;
}

/*
 * string_eq - similar to string's .cmp, but for dictionary/set lookups.
 * Things like a == b, a->v_type == b->v_type, & hash values have already
 * been checked upstream by calling code.
 */
static inline bool
string_eq(Object *a, Object *b)
{
        size_t len, width;
        len = seqvar_size(a);
        if (len != seqvar_size(b))
                return false;
        width = string_width(a);
        if (width != string_width(b))
                return false;
        return memcmp(string_data(a), string_data(b), len * width) == 0;
}

extern hash_t string_update_hash__(Object *v);
static inline hash_t
string_hash(Object *v)
{
        struct stringvar_t *vs = (struct stringvar_t *)v;
        return vs->s_hash ? vs->s_hash : string_update_hash__(v);
}

static inline void
buffer_put_strobj(struct buffer_t *buf, Object *v)
{
        bug_on(!isvar_string(v));
        buffer_nputs_all(buf, string_cstring(v), string_nbytes(v));
}

#endif /* EVC_INC_INTERNAL_TYPES_STRING_H */
