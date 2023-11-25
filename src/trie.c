/*
 * trie.c - Code for managing a bitwise trie
 *
 * This is an insert-only trie, there are no deletions.
 *
 * This conserves memory in two ways:
 *
 * 1. It operates on every nybble instead of byte, therefore the
 *    maximum number of pointers per node will be 16.
 *
 * 2. Each node has only as many pointers as are set; a bitmap determines
 *    the pointers' "intended" index in what would otherwise be a
 *    16-pointer array.
 *
 * Example search case:
 * --------------------
 * Checking character 'c' (0x63), upper nybble (0x6) at a node.
 *
 * First, check bitmap: if 1<<6 is not set in the node's bitmap, fail.
 * Otherwise continue.
 *
 * Second, count how many bits below 1<<6 are also set in the node's
 * bitmap, so we know which index of the node's array to continue down.
 * The method used here gets a mask out the lower bits in the nodes
 * bitmap... (bitmap & ((1<<6)-1)) for this example...  uses bit_count16
 * to count the number of set bits in the result, and the answer is the
 * index.
 *
 * For insertions, the index array has to be re-allocated.  In fact, a
 * separte one is allocated altogether, and the old one is freed when
 * done.  The old ones are copied into the new array, taking into account
 * that their own indexes may have changed if the new insertion falls
 * before then.
 *      [FIXME: since in egq, insertions will happen throughout execution
 *       time, this is inefficient, especially since it happens for
 *       *every insertion*]
 *
 * When not to use this
 * --------------------
 * This does not hash the key.  That makes it collisionless (good), but
 * it also means that long lines of text will explode the number of nodes
 * (very bad).  So it's best used to store dictionaries of one-word keys.
 */
#include "egq.h" /* for prog wrappers like emalloc */
#include "trie.h"
#include <stdlib.h>

static struct trie_t *
insert_helper(struct trie_t *trie, unsigned char c)
{
        uint16_t bit = 1u << (c & 0xfu);
        if (!trie->bitmap) {
                /*
                 * Like "!(trie->bitmap & bit)" below, but less
                 * calculating is required, since we know we're
                 * going to be index zero.
                 */
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

                /*
                 * Need to re-shuffle the array,
                 * we may be in the middle somewhere
                 */
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

/**
 * trie_new - Get an initial node to a bitwise trie.
 */
struct trie_t *
trie_new(void)
{
        return ecalloc(sizeof(struct trie_t));
}

/**
 * trie_insert - Insert a value into a trie
 * @trie: top node of the trie
 * @key:  Key for retrieving the data
 * @data: Data, which may also be the same pointer as key
 * @clobber: If true, ignore collision by replacing old data with new
 *
 * Return: zero if success, -1 if a collision occured and @clobber was
 * not set.
 */
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

/**
 * trie_get - Get data from a trie
 * @trie: top node of the trie
 * @key: Key matching the one used to store the data
 *
 * Return: data, or NULL if not found
 */
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

/**
 * trie_size - Return the cumulative size of all the nodes of a trie, not
 *              counting the data being stored itself.
 *
 * This is used for debugging.  Tries take up large amounts of RAM for
 * even small dictionaries, so it's good to monitor.
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

