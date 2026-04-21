#ifndef EVC_INC_TESTS_STRBUF_H
#define EVC_INC_TESTS_STRBUF_H

#include <stddef.h>

struct strbuf_t {
        char *buf;
        size_t len;
        size_t cap;
};

extern void sb_init(struct strbuf_t *sb, char *buffer, size_t cap);
extern void sb_append(struct strbuf_t *sb, const char *s);

#endif /* EVC_INC_TESTS_STRBUF_H */
