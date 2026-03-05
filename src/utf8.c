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

/**
 * utf8_decode_stateful - State-machine-based way to decode text
 * @state: state machine; initialize by memset()ing to zero.
 * @c: Next character to interpret.
 * @point: Pointer to a variable to store the point
 *
 * Return:
 *      - 0 if we're in the middle of a state or @c puts us into one.
 *      - 1 if @c finishes a state; result is in state->point.
 *      - -1 if error, check state->buf[0:state->idx]
 *
 * This is for dealing with things like circular buffers or other such
 * non-contiguous buffers, where a UTF-8 encryption could straddle the
 * end of one buffer and the start of the other.  Since this is slower
 * and more involved than the above functions, best practice is to
 * use the above functions for 'strlen(s)-4', then follow up with
 * this for the straggling bytes.
 */
int
utf8_decode_stateful(struct utf8_state_t *state, unsigned int c)
{
        switch (state->state) {
        case UTF8_STATE_ASCII:
                if (c < 127) {
                        state->point = c;
                        return 1;
                }
                state->point = 0;
                state->buf[state->idx++] = c;
                if ((c & 0xf8u) == 0xf0u) {
                        state->state = UTF8_STATE_GET3;
                        state->point = c & 0x07u;
                } else if ((c & 0xf0u) == 0xe0u) {
                        state->state = UTF8_STATE_GET2;
                        state->point = c & 0x0fu;
                } else if ((c & 0xe0u) == 0xc0u) {
                        state->state = UTF8_STATE_GET1;
                        state->point = c & 0x1fu;
                } else {
                        goto err;
                }
                return 0;

        case UTF8_STATE_GET1:
        case UTF8_STATE_GET2:
        case UTF8_STATE_GET3:
                state->buf[state->idx++] = c;
                if ((c & 0xc0) != 0x80)
                        goto err;
                state->point = (state->point << 6) + (c & 0x3fu);
                state->state--;
                if (state->state == UTF8_STATE_ASCII) {
                        if (!utf8_valid_unicode(state->point))
                                goto err_ord;
                        state->idx = 0;
                        return 1;
                }
                return 0;

        case UTF8_STATE_ERR:
        case UTF8_STATE_ERR_ORD:
        default:
                return -1;
        }

err:
        state->buf[state->idx] = '\0';
        state->state = UTF8_STATE_ERR;
        return -1;

err_ord:
        state->buf[state->idx] = '\0';
        state->state = UTF8_STATE_ERR_ORD;
        return -1;
}
