#include "egq.h"

/*
 * Brute-force, dumbest-but-fastest version of a trie.  The only RAM
 * optimizations are 1) since all keywords have just lower-case alphabet
 * characters, we can reduce the per-trie array to 26 pointers; and
 * 2) there aren't that many keywords.  At a point when there were eleven
 * keywords, this was measured to consume ~9.7KB, as opposed to a bitwise
 * trie, which consumed only ~1KB.  I can live with that.  Computers are
 * no longer made of wood and sails.
 */
struct kwtrie_t {
        int value;
        struct kwtrie_t *ptrs[26];
};

static struct kwtrie_t *kw_trie;

static struct kwtrie_t *
new_kwtrie(void)
{
        struct kwtrie_t *ret = ecalloc(sizeof(*ret));
        ret->value = -1;
        return ret;
}

/**
 * keyword_seek - Get a keyword's opcode
 * @key: keyword
 *
 * Return: -1 if @key is not a keyword
 *         an OC_* enum matching @key if it is
 */
int
keyword_seek(const char *key)
{
        struct kwtrie_t *trie = kw_trie;
        while (*key) {
                int c = *key - 'a';
                if (c < 0 || c > 25)
                        return -1;
                trie = trie->ptrs[c];
                if (!trie)
                        return -1;
                key++;
        }
        return trie->value;
}

static void
keyword_insert(const char *key, int value)
{
        struct kwtrie_t *trie = kw_trie;
        while (*key) {
                struct kwtrie_t *child;
                int c = *key - 'a';
                bug_on(c < 0 || c > 25);
                child = trie->ptrs[c];
                if (!child) {
                        trie->ptrs[c] = new_kwtrie();
                        child = trie->ptrs[c];
                }
                trie = child;
                key++;
        }
        trie->value = value;
}

#if 0
/*
 * TODO: Restore this, as a diagnostic function
 */
size_t
memused(struct kwtrie_t *trie)
{
        int i;
        size_t size;
        if (!trie)
                return 0;

        size = sizeof(*trie);
        for (i = 0; i < 26; i++)
                size += memused(trie->ptrs[i]);
        return size;
}
#endif

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
                { "const",      OC_CONST },
                { "priv",       OC_PRIV },
                { NULL, 0 }
        };
        const struct kw_tbl_t *tkw;
        kw_trie = new_kwtrie();
        for (tkw = KEYWORDS; tkw->name != NULL; tkw++)
                keyword_insert(tkw->name, tkw->v);
}

