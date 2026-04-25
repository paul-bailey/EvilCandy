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

/*
 * returns -1 if not all the string was inserted because @sb
 *         reached its size limit; 0 otherwise.
 */
int
sb_append(struct strbuf_t *sb, const char *s)
{
        while (*s != '\0' && sb->len + 1 < sb->cap)
                sb->buf[sb->len++] = *s++;
        sb->buf[sb->len] = '\0';
        return *s == '\0' ? 0 : -1;
}

void
sb_reset(struct strbuf_t *sb)
{
        sb->len = 0;
        if (sb->cap)
                sb->buf[0] = '\0';
}


