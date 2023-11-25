/*
 * buffer.c - Append something to the tail of a buffer without
 *              worrying about buffer overflow
 *
 * Use this if:
 *      1. Nothing in the buffer will need pointers to them
 *         persistently (eg. they aren't part of linked lists
 *         or such.  THIS IS IMPORTANT, because realloc may
 *         be called on the buffer at times.
 *      2. You need each new datum to be tangential to the last
 *
 * See stack.c and mempool.c for different scenarios where those
 * might be more appropriate.  This can be used in two ways:
 *
 *      binary API:     buffer_putd
 *                      buffer_size     <- amt of data stored, in bytes
 *
 *      C-string API:   buffer_puts
 *                      buffer_nputs
 *                      buffer_putc
 *                      buffer_substr
 *                      buffer_shrinkstr
 *                      buffer_lstrip
 *                      buffer_rstrip
 *                      buffer_size     <- strlen, not counting '\0'
 *
 *      common to both: buffer_init
 *                      buffer_reset
 *                      buffer_free
 *
 * Always call buffer_init before using it the first time.
 * DO NOT call buffer_init a second time until you next call
 * buffer_free.  If you want to re-init the buffer for re-use,
 * call buffer_reset instead of buffer_init.
 *
 * DO NOT mix/match the binary API and the C-string API on the
 * same buffer unless you call buffer_reset between them.
 */
#include "buffer.h"
#include "egq.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

struct bufblk_t {
        struct list_t list;
        struct buffer_t bufs[64];
        uint64_t b;
};

/*
 * Graveyard of discarded struct buffer_t's, so we don't have to keep
 * malloc'ing and free'ing whenever we push and pop strings on and off
 * the stack.
 *
 * FIXME: This looks like a DRY violation with the memory management
 * in var.c (because it sort of is), but I cannot combine the two,
 * because they are fundamentally different from each other.
 */
struct list_t bufblk_list = {
        .next = &bufblk_list,
        .prev = &bufblk_list
};

#define list2blk(li)    container_of(li, struct bufblk_t, list)

static struct buffer_t *
buffer_from_graveyard(void)
{
        struct list_t *list;
        struct bufblk_t *blk = NULL;
        uint64_t x;
        unsigned int i;
        list_foreach(list, &bufblk_list) {
                struct bufblk_t *tblk = list2blk(list);
                if (tblk->b) {
                        blk = tblk;
                        break;
                }
        }
        if (!blk)
                return NULL;
        for (i = 0, x = 1; !(blk->b & x) && i < 64; x <<= 1, i++)
                ;
        bug_on(i == 64);
        blk->b &= ~x;
        return &blk->bufs[i];
}

static void
buffer_to_graveyard(struct buffer_t *b)
{
        struct list_t *list;
        struct bufblk_t *blk = NULL;
        uint64_t x;
        unsigned int i;

        list_foreach(list, &bufblk_list) {
                struct bufblk_t *tblk = list2blk(list);
                if (~tblk->b) {
                        blk = tblk;
                        break;
                }
        }
        if (!blk) {
                blk = emalloc(sizeof(*blk));
                list_init(&blk->list);
                blk->b = 0LL;
                list_add_tail(&blk->list, &bufblk_list);
        }
        for (i = 0, x = 1; !!(blk->b & x) && i < 64; x <<= 1, i++)
                ;
        bug_on(i == 64);
        blk->b |= x;

        blk->bufs[i].s    = b->s;
        blk->bufs[i].size = b->size;
        blk->bufs[i].p    = 0;
}

static void
buffer_init_(struct buffer_t *b)
{
        memset(b, 0, sizeof(*b));
}

/**
 * buffer_init - Initialize @buf
 *
 * This is NOT a reset function!
 */
void
buffer_init(struct buffer_t *buf)
{
        struct buffer_t *old = buffer_from_graveyard();
        if (old) {
                buf->s    = old->s;
                buf->size = old->size;
                buf->p = 0;
                buffer_init_(old);
        } else {
                buffer_init_(buf);
        }
}

/**
 * buffer_free - Do all the stuff with buffer that makes
 *      it safe to throw away.
 *
 * @buf itself will not be freed.
 */
void
buffer_free(struct buffer_t *buf)
{
        buffer_to_graveyard(buf);
        buffer_init_(buf);
}

static void
buffer_maybe_realloc(struct buffer_t *buf, size_t amt)
{
        enum { BLKLEN = 128 };
        size_t needsize = buf->p + amt;
        while (needsize >= buf->size) {
                char *tmp = realloc(buf->s, buf->size + BLKLEN);
                if (!tmp)
                        fail("realloc failed");

                buf->s = tmp;
                buf->size += BLKLEN;
        }
}

/**
 * buffer_putc - put a character in buffer
 * @buf: Buffer storage.
 * @c: Character to store
 *
 * A nulchar character will be placed after @c, so buf->s
 * can always be safely treated as a C string after the first
 * call to buffer_putc.
 */
void
buffer_putc(struct buffer_t *buf, int c)
{
        /* +2 because we always want at least a null char termination */
        buffer_maybe_realloc(buf, 2);
        buf->s[buf->p] = c;
        /* Don't allow placing nulchars except as terminations */
        if (c != '\0')
                buf->p++;

        /* Keep always nulchar terminated */
        buf->s[buf->p] = '\0';
}

/**
 * buffer_puts - like buffer_putc, but with a WHOLE STRING!!!
 */
void
buffer_puts(struct buffer_t *buf, const char *s)
{
        if (s) {
                int c;
                while ((c = *s++) != '\0')
                        buffer_putc(buf, c);
        }

        /* in case s="", make sure the nul-char termination exists */
        buffer_putc(buf, '\0');
}

/**
 * buffer_nputs - like buffer_puts, but stop if @amt is reached
 *                before end of @s
 */
void
buffer_nputs(struct buffer_t *buf, const char *s, size_t amt)
{
        int c;
        const char *end;

        if (!s)
                return;
        end = s + amt;

        while (s < end && (c = *s++) != '\0')
                buffer_putc(buf, c);

        /* same reason as in buffer_putc */
        buffer_putc(buf, '\0');
}

/**
 * buffer_shrinkstr - shrink C string in buffer
 * @buf:        Buffer to shrink
 * @new_size:   New size to shrink buffer to.
 *
 * If @new_size is bigger than the curernt buffer C string, then no
 * action will occur.  Otherwise, A nulchar termination will be inserted
 * and the buffer's metadata will be updated.
 */
void
buffer_shrinkstr(struct buffer_t *buf, size_t new_size)
{
        if (new_size >= buf->p)
                return;

        buf->p = new_size;
        buffer_putc(buf, '\0');
}

static const char *const STRIP_DEFAULT_CHARSET = " \n\t\f\v\r";

/**
 * buffer_lstrip - Strip all of @charset out of the front end of the
 *                 buffer string.
 * @buf:        Buffer to strip
 * @charset:    nulchar-stopped character set to strip, or NULL to
 *              have it be the common default of whitespace characters.
 */
void
buffer_lstrip(struct buffer_t *buf, const char *charset)
{
        size_t spn;

        if (!buf->s)
                return;
        if (!charset)
                charset = STRIP_DEFAULT_CHARSET;
        spn = strspn(buf->s, charset);
        if (spn >= buf->p) {
                bug_on(spn > buf->p);
                buffer_reset(buf);
        } else if (spn != 0) {
                memmove(buf->s, buf->s + spn, buf->p - spn);
                buf->p -= spn;
                buffer_putc(buf, '\0');
        }
}

/**
 * buffer_rstrip - Strip all of @charset out of the tail end of the
 *                 buffer string.
 * @buf:        Buffer to strip
 * @charset:    nulchar-stopped character set to strip, or NULL to
 *              have it be the common default of whitespace characters.
 */
void
buffer_rstrip(struct buffer_t *buf, const char *charset)
{
        size_t spn;

        if (!buf->s)
                return;
        if (!charset)
                charset = STRIP_DEFAULT_CHARSET;
        spn = my_strrspn(buf->s, charset, buf->s + buf->p - 1);
        if (spn >= buf->p) {
                bug_on(spn > buf->p);
                buffer_reset(buf);
        } else if (spn != 0) {
                buf->p -= spn;
                buffer_putc(buf, '\0');
        }
}

/**
 * Get character from @buf with index @i, or -1 if @i out of bounds
 * @buf: buffer
 * @i:   Index.  If negative, then indexed from the end.
 */
int
buffer_substr(struct buffer_t *buf, int i)
{
        if (!buf->s)
                return -1;
        if (i < 0) {
                i = buf->p - 1 - i;
                if (i < 0)
                        return -1;
        } else if (i >= buf->p) {
                return -1;
        }
        return buf->s[i];
}

/**
 * buffer_putd - The binary version of buffer_put*()
 * @buf:        Buffer
 * @data:       Data to append to buffer
 * @datalen:    Length of @data
 *
 * DO NOT use the text-based buffer API if you are also using this!
 * This does not ensure nulchar termination at the end of the data.
 */
void
buffer_putd(struct buffer_t *buf, const void *data, size_t datalen)
{
        buffer_maybe_realloc(buf, datalen);
        memcpy(&buf->s[buf->p], data, datalen);
        buf->p += datalen;
}


