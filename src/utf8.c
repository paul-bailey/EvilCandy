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

static void
utf8_decode_write_point(struct buffer_t *b,
                        unsigned long point, unsigned int maxwidth)
{
        bug_on(point > 0xfffful && maxwidth < 4);
        bug_on(point > 0xff && maxwidth < 2);
        union {
                uint8_t u8;
                uint16_t u16;
                uint32_t u32;
        } x;
        void *p;

        switch (maxwidth) {
        case 1:
                x.u8 = point;
                p = &x.u8;
                break;
        case 2:
                x.u16 = point;
                p = &x.u16;
                break;
        case 4:
                x.u32 = point;
                p = &x.u32;
                break;
        default:
                bug();
                return;
        }
        buffer_putd(b, p, maxwidth);
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
        size_t maxwidth;
        unsigned int c;
        struct buffer_t b;

        /*
         * This makes it slower for non-ASCII strings, but
         * most strings will be all-ASCII, so over-all it will
         * probably be faster.
         */
        for (s = (unsigned char *)src; *s != '\0'; s++) {
                if (((unsigned)(*s) & 0xffu) >= 128)
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
         * XXX: Some corner cases exist where I set maxwidth > 1
         * but the encoding is Latin1, causing me to waste RAM.
         */
        maxwidth = 1;
        while ((c = *s++) != '\0') {
                if (c < 0xc0)
                        continue;

                if ((c & 0xf8) == 0xf0) {
                        maxwidth = 4;
                } else if ((c & 0xf0) == 0xe0) {
                        if (maxwidth < 2)
                                maxwidth = 2;
                } else if ((c & 0xe0) == 0xc0) {
                        /*
                         * 110aaabb 10bbcccc: if the 'aaa' bits are zero,
                         * then this is range 0x80...0xff (width 1).
                         * Otherwise, the range is 0x100...0x7ff (width 2).
                         */
                        if (c > 0xc3 && maxwidth < 2)
                                maxwidth = 2;
                }
        }

        /*
         * If maxwidth is still 1, then we have some non-ASCII,
         * non-UTF-8 chars in our string.  They are easy enough
         * to parse amidst the UTF-8 characters so long as they
         * are not in the middle of a malformed UTF-8 sequence,
         * so add them to the points array below and tell caller
         * it was UTF-8 all along.
         */

        buffer_init(&b);
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
                        while (s < endptr && *s != '\0') {
                                utf8_decode_write_point(&b, *s, maxwidth);
                                s++;
                        }
                        continue;
                }

                if (!utf8_valid_unicode(point))
                        goto err;

                utf8_decode_write_point(&b, point, maxwidth);
                s = endptr;
        }

        utf8_decode_write_point(&b, 0, maxwidth);

        *ascii = 0;
        *width = maxwidth;
        *len = buffer_size(&b) / maxwidth;
        return buffer_trim(&b);

err:
        buffer_free(&b);
        return NULL;
}

