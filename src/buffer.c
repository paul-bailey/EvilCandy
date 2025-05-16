/*
 * buffer.c - Append something to the tail of a buffer without worrying
 *            about buffer overflow
 *
 * Use this if:
 *      1. Nothing in the buffer will need pointers to them persistently
 *         (eg. they aren't part of linked lists or such).
 *                      THIS IS IMPORTANT
 *         because realloc may be called on the buffer at times.
 *      2. You need each new datum to be tangential to the last
 *
 * Do not use this if:
 *      1. Amortization is badly needed in the case of array growth.
 *         In this module, the array always grows by the same constant
 *         amount.  If amortization is badly needed, see assert_array_pos
 *         in helpers.c
 *
 * This module can be used in two ways:
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
 *                      buffer_printf
 *                      buffer_size     <- strlen, not counting '\0'
 *
 *      common to both: buffer_init
 *                      buffer_reset
 *                      buffer_free
 *                      buffer_trim
 *
 * Always call buffer_init before using it the first time.
 * DO NOT call buffer_init a second time until you next call
 * buffer_free.  If you want to re-init the buffer for re-use,
 * call buffer_reset instead of buffer_init.
 *
 * DO NOT mix/match the binary API and the C-string API on the
 * same buffer unless you call buffer_reset between them.
 */
#include <lib/buffer.h>
#include <evilcandy.h>
#include <stdarg.h>

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
        buffer_init_(buf);
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
        if (buf->s)
                efree(buf->s);
        buffer_init_(buf);
}

static void
buffer_maybe_realloc(struct buffer_t *buf, size_t amt)
{
        enum { BLKLEN = 128 };
        size_t needsize = buf->p + amt;
        while (needsize >= buf->size) {
                buf->s = erealloc(buf->s, buf->size + BLKLEN);
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

/**
 * buffer_vprintf - Do a formatted printf into a buffer
 * @buf: Buffer to print to
 * @msg: Formatted message
 * @ap:  Variable-arg list already initialized with va_start
 *
 * This makes a double-call to standard library snprintf/sprintf,
 * so only use this if you don't need to be super quick.
 */
void
buffer_vprintf(struct buffer_t *buf, const char *msg, va_list ap)
{
        va_list ap2;
        ssize_t need_size;

        va_copy(ap2, ap);
        need_size = vsnprintf(NULL, 0, msg, ap2);
        va_end(ap2);

        if (need_size >= 0) {
                buffer_maybe_realloc(buf, need_size + 1);
                vsnprintf(&buf->s[buf->p], need_size + 1, msg, ap);
        }
        buf->p += need_size;
}

void
buffer_printf(struct buffer_t *buf, const char *msg, ...)
{
        va_list ap;
        va_start(ap, msg);
        buffer_vprintf(buf, msg, ap);
        va_end(ap);
}

/**
 * buffer_trim - Resize, if necessary, a buffer to its contained
 *               data, and re-initialize the buffer.
 * @buf: Buffer to resize.
 *
 * Return: Pointer to the resized data, which will no longer be in @buf.
 *
 * This is intended as a finalizing step when filling a buffer with
 * text or data.  The buffer grows in chunks to reduce the number of
 * realloc calls, and this shrinks it back down to size at the end.
 */
void *
buffer_trim(struct buffer_t *buf)
{
        void *ret;
        if (buf->s == NULL) {
                bug_on(buf->size != 0);
                ret = estrdup("");
        } else {
                /*
                 * '+1' in case it's size zero, or it's a char-based buf,
                 * which has a nullchar at the end.  Add the nullchar
                 * explicitly, in case @buf had been using buffer_putd.
                 * It's only a matter of time before a binary buffer
                 * result is accidentally passed to a string.h function.
                 */
                buf->s = erealloc(buf->s, buf->p + 1);
                buf->s[buf->p] = 0;
                ret = buf->s;
        }
        buffer_init_(buf);
        return ret;
}

