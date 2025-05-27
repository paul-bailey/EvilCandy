#ifndef EGQ_HELPERS_H
#define EGQ_HELPERS_H

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdbool.h>
#include "config.h"

/* helpers.c */
#ifndef HAVE_STRRSTR
extern char *strrstr(const char *haystack, const char *needle);
#endif
#ifndef HAVE_STRRSPN
extern size_t strrspn(const char *s, const char *charset, const char *end);
#endif
#ifndef HAVE_STRRCHRNUL
extern char *strrchrnul(const char *s, int c);
#endif
#ifndef HAVE_MEMMEM
extern void *memmem(const void *haystack, size_t hlen,
                    const void *needle, size_t nlen);
#endif
#ifndef HAVE_MEMRMEM
extern void *memrmem(const void *haystack, size_t hlen,
                     const void *needle, size_t nlen);
#endif

#define ASCII_WS_CHARS " \t\r\n\v\f"
#define ASCII_NWS_CHARS 6

/* helpers.c */
extern int memcount(const void *haystack, size_t hlen,
                    const void *needle, size_t nlen);
extern int x2bin(int c);
static inline bool isodigit(int c) { return c >= '0' && c <= '7'; }
static inline bool isquote(int c) { return c == '"' || c == '\''; }
extern ssize_t match(const char *needle, const char *haystack);
extern int bit_count64(uint64_t v);
extern int bit_count32(uint32_t v);
extern int bit_count16(uint16_t v);
extern int ctz32(uint32_t x);
extern int ctz64(uint64_t x);
extern void print_escapestr(FILE *fp, const char *s, int quote);
extern int assert_array_pos(int idx, void **arr,
                        size_t *alloc_bytes, size_t type_size);
extern const char *notdir(const char *path);
/* Why isn't this in stdlib.h? */
#define container_of(x, type, member) \
        ((type *)((uintptr_t)((void *)(x)) - offsetof(type, member)))
#ifndef ARRAY_SIZE
# define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/*
 * can't just be a-b, because if they're floats, a non-zero result
 * might cast to 0.
 * Warning, evaluates args twice each.
 */
#define OP_CMP(a_, b_) (a_ == b_ ? 0 : (a_ < b_ ? -1 : 1))


/**
 * utf8_info_t - Output of utf8_scan
 * @enc:        If STRING_ENC_ASCII, string is ASCII only.
 *              If STRING_ENC_UTF8, string only has valid UTF-8 chars,
 *              including some non-ASCII Unicode chars
 *              If STRING_ENC_UNK, string contains non-UTF-8 chars.
 * @ascii_len:  Length of C string in bytes
 * @enc_len:    Length of string in number of Unicode characters; this
 *              will not be reduced for any non-UFT-8 characters.
 */
struct utf8_info_t {
        enum {
                STRING_ENC_ASCII = 0,
                STRING_ENC_UTF8,
                STRING_ENC_UNK,
        } enc;
        size_t ascii_len;
        size_t enc_len;
};

struct buffer_t;

extern int utf8_subscr_str(const char *src, size_t idx, char *dest);
extern size_t utf8_strgetc(const char *s, char *dst);
extern void utf8_encode(unsigned long point, struct buffer_t *buf);
extern long utf8_decode_one(const unsigned char *src,
                            unsigned char **endptr);
extern void *utf8_decode(const char *src, size_t *width,
                         size_t *len, int *ascii);
static inline bool
utf8_valid_unicode(unsigned long point)
{
        /* Check out of range or invalid surrogate pairs */
        return point < 0x10fffful &&
               !(point >= 0xd800ul && point <= 0xdffful);
}

#endif /* EGQ_HELPERS_H */
