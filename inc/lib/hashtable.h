#ifndef EGQ_HASHTABLE_H
#define EGQ_HASHTABLE_H

#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>

typedef uint64_t hash_t;

struct bucket_t {
        hash_t hash;
        void *key;
        void *data;
};

struct hashtable_t {
        size_t size;    /* always a power of 2 */
        size_t used;    /* active entries */
        size_t count;   /* active + removed entries */
        size_t grow_size;
        size_t shrink_size;
        struct bucket_t **bucket;
        hash_t (*calc_hash)(const void *);
        bool (*key_match)(const void *, const void *);
        void (*delete_data)(void *);
};

extern int hashtable_put(struct hashtable_t *htbl,
                                void *key, void *data);
extern void *hashtable_get(struct hashtable_t *htbl, const void *key);
extern void *hashtable_remove(struct hashtable_t *htbl, const void *key);
extern void hashtable_init(struct hashtable_t *htbl,
                           hash_t (*calc_hash)(const void *),
                           bool (*key_match)(const void *, const void *),
                           void (*delete_data)(void *));
extern void hashtable_clear_entries(struct hashtable_t *htbl);
extern void hashtable_destroy(struct hashtable_t *htbl);
extern int hashtable_iterate(struct hashtable_t *htbl, void **key,
                             void **val, unsigned int *idx);

/* EGQ's hash algos */
extern hash_t ptr_hash(const void *key);
extern hash_t idx_hash(const void *key);
extern bool ptr_key_match(const void *k1, const void *k2);

/* just for literal.c */
char *hashtable_put_literal(struct hashtable_t *htbl, const char *key);

#endif /* EGQ_HASHTABLE_H */

