#include "egq.h"
#include <stdlib.h>

static struct trie_t *
insert_helper(struct trie_t *trie, unsigned char c)
{
        uint16_t bit = 1u << (c & 0xfu);
        if (!trie->bitmap) {
                bug_on(trie->ptrs != NULL);
                trie->ptrs = emalloc(sizeof(void *));
                trie->bitmap |= bit;
                trie->ptrs[0] = trie_new();
                trie = trie->ptrs[0];
        } else if (!(trie->bitmap & bit)) {
                uint16_t ones;
                int n, i, thisidx;
                struct trie_t **newarr;

                trie->bitmap |= bit;
                ones = trie->bitmap & (bit - 1);
                thisidx = bit_count16(ones);

                n = bit_count16(trie->bitmap) + 1;
                newarr = emalloc(sizeof(void *) * n);
                for (i = 0; i < thisidx; i++)
                        newarr[i] = trie->ptrs[i];
                newarr[thisidx] = trie_new();
                for (i = thisidx + 1; i < n; i++)
                        newarr[i] = trie->ptrs[i - 1];
                free(trie->ptrs);
                trie->ptrs = newarr;
                trie = trie->ptrs[thisidx];
        } else {
                uint16_t ones = trie->bitmap & (bit - 1);
                trie = trie->ptrs[bit_count16(ones)];
        }
        return trie;
}

struct trie_t *
trie_new(void)
{
        return ecalloc(sizeof(struct trie_t));
}

int
trie_insert(struct trie_t *trie, const char *key,
            void *data, bool clobber)
{
        while (*key) {
                unsigned char c = *key++;
                while (c) {
                        trie = insert_helper(trie, c);
                        c >>= 4;
                }
        }

        if (trie->value && !clobber)
                return -1;
        trie->value = data;
        return 0;
}

void *
trie_get(struct trie_t *trie, const char *key)
{
        while (*key) {
                unsigned char c = *key++;
                while (c) {
                        uint16_t ones;
                        uint16_t bit = 1u << (c & 0xfu);
                        if (!(trie->bitmap & bit))
                                return NULL;
                        ones = trie->bitmap & (bit - 1);
                        trie = trie->ptrs[bit_count16(ones)];
                        c >>= 4;
                }
        }
        return trie->value;
}

/*
 * used for debugging
 * does not include size of all the .value's
 */
size_t
trie_size(struct trie_t *trie)
{
        int i, n;
        size_t size;
        n = bit_count16(trie->bitmap);
        size = sizeof(*trie) + sizeof(void *) * n;
        for (i = 0; i < n; i++)
                size += trie_size(trie->ptrs[i]);
        return size;
}

