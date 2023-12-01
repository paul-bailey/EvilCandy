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
extern ssize_t index_translate(ssize_t i, size_t size);
extern void print_escapestr(FILE *fp, const char *s, int quote);
extern int assert_array_pos(int idx, void **arr,
                        size_t *alloc_bytes, size_t type_size);
/* Why isn't this in stdlib.h? */
#define container_of(x, type, member) \
        ((type *)(((void *)(x)) - offsetof(type, member)))

#endif /* EGQ_HELPERS_H */
