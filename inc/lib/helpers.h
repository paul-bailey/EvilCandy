#ifndef EGQ_HELPERS_H
#define EGQ_HELPERS_H

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdbool.h>

/* helpers.c */
extern int x2bin(int c);
static inline bool isodigit(int c) { return c >= '0' && c <= '7'; }
static inline bool isquote(int c) { return c == '"' || c == '\''; }
extern char *my_strrchrnul(const char *s, int c);
extern ssize_t match(const char *needle, const char *haystack);
extern size_t my_strrspn(const char *s,
                         const char *charset, const char *end);
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

size_t utf8_strlen(const char *s);
extern void utf8_scan(const char *s, struct utf8_info_t *info);
extern int utf8_subscr_str(const char *src, size_t idx, char *dest);
extern size_t utf8_strgetc(const char *s, char *dst);
extern size_t utf8_encode(uint32_t point, char *buf);


#endif /* EGQ_HELPERS_H */
