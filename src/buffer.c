#include "egq.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
 * buffer_init - Initialize @tok
 *
 * This is NOT a reset function!
 */
void
buffer_init(struct buffer_t *tok)
{
        tok->s = NULL;
        tok->p = 0;
        tok->size = 0;
}

/**
 * buffer_reset - Reuse a buffer from start.
 */
void
buffer_reset(struct buffer_t *tok)
{
        tok->p = 0;
        if (tok->s)
                tok->s[0] = '\0';
}

/**
 * buffer_rstrip - Right-strip whitespace in buffer
 */
void
buffer_rstrip(struct buffer_t *tok)
{
        char *s;
        if (!tok->s || !tok->p)
                return;
        s = tok->s + tok->p - 1;
        while (s >= tok->s && isspace((int)*s))
                *s-- = '\0';
        s++;
        bug_on(s < tok->s);
        tok->p = s - tok->s;
}

/**
 * buffer_free - Do all the stuff with buffer that makes
 *      it safe to throw away.
 *
 * @tok itself will not be freed.
 */
void
buffer_free(struct buffer_t *tok)
{
        /*
         * TODO: I want some kind of "buffer graveyard",
         * so I can just copy this over into that,
         * then re-fill another buffer on ``buffer_init'',
         * just so I don't malloc() and free() so much.
         */
        if (tok->s)
                free(tok->s);
        buffer_init(tok);
}

static void
buffer_maybe_realloc(struct buffer_t *tok, size_t amt)
{
        /* if amt == sizeof opcode, we expect a lot of these */
        size_t blklen = amt == 2 ? 128 : 1024;
        size_t needsize = tok->p * amt + amt;
        while (needsize >= tok->size) {
                char *tmp = realloc(tok->s, tok->size + blklen);
                if (!tmp)
                        fail("EOM");

                tok->s = tmp;
                tok->size += blklen;
        }
}

/**
 * buffer_putc - put a character in buffer
 * @tok: Buffer storage.
 * @c: Character to store
 *
 * A nulchar character will be placed after @c, so tok->s
 * can always be safely treated as a C string after the first
 * call to buffer_putc.
 */
void
buffer_putc(struct buffer_t *tok, int c)
{
        /* +2 because we always want at least a null char termination */
        buffer_maybe_realloc(tok, 2);
        tok->s[tok->p] = c;
        /* Don't allow placing nulchars except as terminations */
        if (c != '\0')
                tok->p++;

        /* Keep always nulchar terminated */
        tok->s[tok->p] = '\0';
}

void
buffer_putcode(struct buffer_t *tok, struct opcode_t *oc)
{
        buffer_maybe_realloc(tok, sizeof(*oc));
        memcpy(&tok->oc[tok->p], oc, sizeof(*oc));
        tok->p++;
}

/**
 * buffer_puts - like buffer_putc, but with a WHOLE STRING!!!
 */
void
buffer_puts(struct buffer_t *tok, const char *s)
{
        int c;

        if (!s)
                return;

        while ((c = *s++) != '\0')
                buffer_putc(tok, c);
}

/**
 * Get character from @tok with index @i, or -1 if @i out of bounds
 * @tok: Token buffer
 * @i:   Index.  If negative, then indexed from the end.
 */
int
buffer_substr(struct buffer_t *tok, int i)
{
        if (!tok->s)
                return -1;
        if (i < 0) {
                i = tok->p - 1 - i;
                if (i < 0)
                        return -1;
        } else if (i >= tok->p) {
                return -1;
        }
        return tok->s[i];
}


