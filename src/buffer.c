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
        blk->b &= ~x;

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
 * buffer_init - Initialize @tok
 *
 * This is NOT a reset function!
 */
void
buffer_init(struct buffer_t *tok)
{
        struct buffer_t *old = buffer_from_graveyard();
        if (old) {
                tok->s    = old->s;
                tok->size = old->size;
                tok->p = 0;
                buffer_init_(old);
        } else {
                buffer_init_(tok);
        }
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
        buffer_to_graveyard(tok);
        buffer_init_(tok);
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


