/* utf8.c - Helpers for UTF-8 C-string encoding/decoding */
#include <evilcandy.h>

/**
 * utf8_encode - Encode a Unicode point in UTF-8
 * @point:      A unicode point from U+0001 to U+10FFFF
 * @buf:        Buffer to store encoded result
 *
 * Behavior is undefined if @point is not valid Unicode
 */
void
utf8_encode(unsigned long point, struct buffer_t *buf)
{
        if (point < 0x7ff) {
                buffer_putc(buf, 0xc0 | (point >> 6));
                buffer_putc(buf, 0x80 | (point & 0x3f));
        } else if (point < 0xffff) {
                buffer_putc(buf, 0xe0 | (point >> 12));
                buffer_putc(buf, 0x80 | ((point >> 6) & 0x3f));
                buffer_putc(buf, 0x80 | (point & 0x3f));
        } else {
                buffer_putc(buf, 0xf0 | (point >> 18));
                buffer_putc(buf, 0x80 | ((point >> 12) & 0x3f));
                buffer_putc(buf, 0x80 | ((point >> 6) & 0x3f));
                buffer_putc(buf, 0x80 | (point & 0x3f));
        }
}

static long
decode_one_point(const unsigned char *s, unsigned char **endptr,
                 unsigned long point, int n)
{
        const unsigned char *ts = s;

        while (n-- > 0) {
                unsigned int c = *ts++;
                if ((c & 0xc0) != 0x80) {
                        --ts;
                        point = -1L;
                        break;
                }
                point = (point << 6) + (c & 0x3fu);
        }
        *endptr = (unsigned char *)ts;
        return (long)point;
}

/**
 * utf8_decode_one - Get a single unicode point
 * @src: C string containing UTF-8-encoded Unicode points.
 * @endptr: stop point
 *
 * Return value: Unicode point, or -1 if either next char in @src is
 * ASCII or @src points to an invalid UTF-8 sequence.
 */
long
utf8_decode_one(const unsigned char *src, unsigned char **endptr)
{
        unsigned int c = *src++;
        long point = -1;
        if (c < 128) {
                *endptr = (unsigned char *)(c ? src : src - 1);
                return c;
        }
        do {
                if ((c & 0xf8u) == 0xf0u) {
                        point = decode_one_point(src, endptr, c & 0x07u, 3);
                } else if ((c & 0xf0u) == 0xe0u) {
                        point = decode_one_point(src, endptr, c & 0x0fu, 2);
                } else if ((c & 0xe0u) == 0xc0u) {
                        point = decode_one_point(src, endptr, c & 0x1fu, 1);
                }
        } while (0);

        if (point >= 0L && !utf8_valid_unicode(point))
                point = -1L;

        return point;
}

/*
 * utf8_ndecode_one - like utf8_decode_one, but use max-length @n instead
 *                    of relying on @src to be nulchar terminated.
 */
long
utf8_ndecode_one(const unsigned char *src, unsigned char **endptr, size_t n)
{
        bug_on(n < 1);
        unsigned int c = *src++;
        long point = -1;
        if (c < 128) {
                *endptr = (unsigned char *)(c ? src : src - 1);
                return c;
        }
        n--;
        do {
                if ((c & 0xf8u) == 0xf0u) {
                        if (n < 3)
                                return -1;
                        point = decode_one_point(src, endptr, c & 0x07u, 3);
                } else if ((c & 0xf0u) == 0xe0u) {
                        if (n < 2)
                                return -1;
                        point = decode_one_point(src, endptr, c & 0x0fu, 2);
                } else if ((c & 0xe0u) == 0xc0u) {
                        if (n < 1)
                                return -1;
                        point = decode_one_point(src, endptr, c & 0x1fu, 1);
                }
        } while (0);

        if (point >= 0L && !utf8_valid_unicode(point))
                point = -1L;

        return point;
}

/**
 * utf8_decode - Decode a (possibly) UTF-8 encoded C-string, and return
 *               an array of its Unicode points.
 * @src:        C-string to decode
 * @width:      Variable to store the max width of the return value.
 *              Values are 1, 2, and 4
 * @len:        Variable to store array length of return value.  Total
 *              number of allocated bytes is @len x @width.
 * @ascii:      Variable to store true if @src is all ASCII, false if
 *              @src is non-ASCII.
 *
 * Return:      - @src if @ascii is true
 *              - a newly allocated buffer containing the Unicode points,
 *                even if @width is 1.
 *              - NULL if malformed UTF-8 characters exist in @src.
 */
void *
utf8_decode(const char *src, size_t *width,
            size_t *len, int *ascii)
{
        const unsigned char *s;
        unsigned int c;
        struct string_writer_t wr;

        /*
         * This makes it slower for non-ASCII strings, but
         * most strings will be all-ASCII, so over-all it will
         * probably be faster.
         */
        for (s = (unsigned char *)src; *s != '\0'; s++) {
                if (*s >= 128)
                        break;
        }

        if (*s == '\0') {
                /* ASCII-only, our likeliest fast path */
                *ascii = 1;
                *width = 1;
                *len = (size_t)(s - (unsigned char *)src);
                return (void *)src;
        }

        /*
         * We have no guaranteed way of knowing width, short of having to
         * decode every Unicode point twice; it's less time consuming in
         * most cases to assume the smallest width (Latin1) and let the
         * string_writer API grow the array width as necessary.
         */
        string_writer_init(&wr, 1);
        s = (unsigned char *)src;
        while ((c = *s++) != '\0') {
                unsigned long point;
                unsigned char *endptr;
                if (c < 128) {
                        endptr = (unsigned char *)s;
                        point = c;
                } else if ((c & 0xf8) == 0xf0) {
                        point = decode_one_point(s, &endptr, c & 0x07, 3);
                } else if ((c & 0xf0) == 0xe0) {
                        point = decode_one_point(s, &endptr, c & 0x0f, 2);
                } else if ((c & 0xe0) == 0xc0) {
                        point = decode_one_point(s, &endptr, c & 0x1f, 1);
                } else {
                        endptr = (unsigned char *)s;
                        point = c;
                }

                if ((long)point == -1L) {
                        /* Assume that malformed UTF-8 just means Latin1 */
                        string_writer_append(&wr, *s++);
                        continue;
                }

                if (!utf8_valid_unicode(point)) {
                        string_writer_destroy(&wr);
                        return NULL;
                }

                string_writer_append(&wr, point);
                s = endptr;
        }

        *ascii = 0;
        return string_writer_finish(&wr, width, len);
}

