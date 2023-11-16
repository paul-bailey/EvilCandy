#include "egq.h"
#include <ctype.h>
#include <stdlib.h>

/**
 * token_init - Initialize @tok
 *
 * This is NOT a reset function!
 */
void
token_init(struct token_t *tok)
{
        tok->s = NULL;
        tok->p = 0;
        tok->size = 0;
}

/**
 * token_reset - Reuse a token from start.
 */
void
token_reset(struct token_t *tok)
{
        tok->p = 0;
        if (tok->s)
                tok->s[0] = '\0';
}

/**
 * token_rstrip - Right-strip whitespace in token
 */
void
token_rstrip(struct token_t *tok)
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
 * token_free - Do all the stuff with token that makes
 *      it safe to throw away.
 *
 * @tok itself will not be freed.
 */
void
token_free(struct token_t *tok)
{
        /*
         * TODO: I want some kind of "token graveyard",
         * so I can just copy this over into that,
         * then re-fill another token on ``token_init'',
         * just so I don't malloc() and free() so much.
         */
        if (tok->s)
                free(tok->s);
        token_init(tok);
}

/**
 * token_putc - put a character in buffer
 * @tok: Buffer storage.
 * @c: Character to store
 *
 * A nulchar character will be placed after @c, so tok->s
 * can always be safely treated as a C string after the first
 * call to token_putc.
 */
void
token_putc(struct token_t *tok, int c)
{
        enum { BLKLEN = 128 };
        while ((tok->p + 1) >= tok->size) {
                char *tmp = realloc(tok->s, tok->size + BLKLEN);
                if (!tmp)
                        fail("EOM");

                tok->s = tmp;
                tok->size += BLKLEN;
        }
        tok->s[tok->p] = c;
        /* Don't allow placing nulchars except as terminations */
        if (c != '\0')
                tok->p++;

        /* Keep always nulchar terminated */
        tok->s[tok->p] = '\0';
}

/**
 * token_puts - like token_putc, but with a WHOLE STRING!!!
 */
void
token_puts(struct token_t *tok, const char *s)
{
        int c;

        if (!s)
                return;

        while ((c = *s++) != '\0')
                token_putc(tok, c);
}


