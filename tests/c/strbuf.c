/*
 * Similar in spirit to buffer.c, but for strictly static
 * buffers.
 */
#include <tests/strbuf.h>

void
sb_init(struct strbuf_t *sb, char *buffer, size_t cap)
{
        sb->buf = buffer;
        sb->len = 0;
        sb->cap = cap;
        sb->buf[0] = '\0';
}

void
sb_append(struct strbuf_t *sb, const char *s)
{
        while (*s != '\0' && sb->len + 1 < sb->cap)
                sb->buf[sb->len++] = *s++;
        sb->buf[sb->len] = '\0';
}


