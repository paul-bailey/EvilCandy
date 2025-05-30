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
extern char *strchr_nonnull(const char *charset, int c);
extern int memcount(const void *haystack, size_t hlen,
                    const void *needle, size_t nlen);
extern int x2bin(int c);
static inline bool isodigit(int c) { return c >= '0' && c <= '7'; }
static inline bool isquote(int c) { return c == '"' || c == '\''; }
extern int bit_count64(uint64_t v);
extern const char *notdir(const char *path);
extern char *slide(const char *s, const char *sep);

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

#endif /* EGQ_HELPERS_H */
