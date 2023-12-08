/* see buffer.c for description of this lib */
#ifndef EGQ_BUFFER_H
#define EGQ_BUFFER_H

#include <sys/types.h> /* for ssize_t */

/**
 * struct buffer_t - Handle to metadata about a dynamically allocated
 *                  string
 * @s:  Pointer to the C string.  After buffer_init(), it's always either
 *      NULL or nulchar-terminated, unless you use the binary API
 * @p:  Current index into @s following last character in this struct.
 * @size: Size of allocated data for @s.
 *
 * WARNING!  @s is NOT a constant pointer!  A call to buffer_put{s|c}()
 *      may invoke a reallocation function that moves it.  So do not
 *      store the @s pointer unless you are finished filling it in.
 *
 * ANOTHER WARNING!!!!   Do not use the string-related functions
 *                       on the same buffer where you use buffer_putd
 */
struct buffer_t {
        char *s;
        ssize_t p;
        ssize_t size;
};

extern void buffer_init(struct buffer_t *buf);
extern void buffer_putc(struct buffer_t *buf, int c);
extern void buffer_nputs(struct buffer_t *buf, const char *s, size_t amt);
extern void buffer_puts(struct buffer_t *buf, const char *s);
extern void buffer_free(struct buffer_t *buf);
extern int buffer_substr(struct buffer_t *buf, int i);
extern void buffer_shrinkstr(struct buffer_t *buf, size_t new_size);
extern void buffer_lstrip(struct buffer_t *buf, const char *charset);
extern void buffer_rstrip(struct buffer_t *buf, const char *charset);
extern void buffer_putd(struct buffer_t *buf,
                        const void *data, size_t datalen);
extern void buffer_init_from(struct buffer_t *buf,
                        char *line, size_t size);
static inline size_t buffer_size(struct buffer_t *buf) { return buf->p; }
static inline void
buffer_reset(struct buffer_t *buf)
{
        buf->p = '\0';
        if (buf->s)
                buf->s[0] = '\0';
}

#endif /* EGQ_BUFFER_H */

