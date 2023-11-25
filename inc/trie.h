/* lib described in trie.c */
#ifndef EGQ_TRIE_H
#define EGQ_TRIE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * struct trie_t - Node for bitwise trie
 * @bitmap: Bitmap of hits for this node.
 * @value: Value attached to this node, or NULL if this is purely a
 *      pass-through node
 * @ptrs: Array of pointers to the next nodes down.
 *
 * See comment header in trie.c for how it operates.
 */
struct trie_t {
        uint32_t bitmap;
        void *value;
        struct trie_t **ptrs;
};

extern struct trie_t *trie_new(void);
extern int trie_insert(struct trie_t *trie, const char *key,
                       void *data, bool clobber);
extern void *trie_get(struct trie_t *trie, const char *key);
extern size_t trie_size(struct trie_t *trie);

#endif /* EGQ_TRIE_H */

