/* utf8.c - Helpers for UTF-8 C-string encoding/decoding */
#include <lib/utf8.h>

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

