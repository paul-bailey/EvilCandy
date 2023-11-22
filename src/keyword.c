#include "egq.h"
#include "hashtable.h"

static struct hashtable_t *kw_htbl = NULL;

void
moduleinit_keyword(void)
{
        static const struct kw_tbl_t {
                const char *name;
                int v;
        } KEYWORDS[] = {
                { "function",   OC_FUNC },
                { "let",        OC_LET },
                { "return",     OC_RETURN },
                { "this",       OC_THIS },
                { "break",      OC_BREAK },
                { "if",         OC_IF },
                { "while",      OC_WHILE },
                { "else",       OC_ELSE },
                { "do",         OC_DO },
                { "for",        OC_FOR },
                { "load",       OC_LOAD },
                { NULL, 0 }
        };
        const struct kw_tbl_t *tkw;

        /*
         * XXX REVISIT: Not as many keywords as I thought, maybe a linear
         * search would be quicker.
         */
        kw_htbl = hashtable_create(HTBL_COPY_KEY|HTBL_COPY_DATA, NULL);
        if (!kw_htbl)
                fail("hashtable_create failed");

        for (tkw = KEYWORDS; tkw->name != NULL; tkw++) {
                int res = hashtable_put(kw_htbl, tkw->name,
                                        (void *)&tkw->v, sizeof(tkw->v), 0);
                bug_on(res < 0);
        }
}

/**
 * keyword_seek - Get a keyword matching @s
 *
 * Return: an OC_* enum or -1 if not found
 */
int
keyword_seek(const char *s)
{
        int *p = hashtable_get(kw_htbl, s, NULL);
        return p ? *p : -1;
}


