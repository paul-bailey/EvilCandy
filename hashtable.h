/*
 * hashtable.h - API for hashtable.c
 *
 * Copyright (C) 2016 Paul Bailey <baileyp2012@gmail.com>
 */
#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stddef.h>
#include <stdbool.h>

enum {
        /* parameters for hashtable_create */
        HTBL_COPY_DATA  = 0x0001,
        HTBL_COPY_KEY   = 0x0002,
        HTBL_UBUCKET    = 0x0004,

        /* parameter for hashtable_put */
        HTBL_CLOBBER    = 0x0004,
};

/**
 * struct bucket_t - A single entry for a hashtable
 * @key: Pointer to the key
 * @hash: Hash number of key
 * @next: Pointer to next entry in this array index, in case of collision
 * @datalen: Length of @data
 * @data: Pointer to the data corresponding to @key
 *
 * If creating hashtable with HTBL_UBUCKET, you manage these in memory,
 * though the hashtable code still sets its fields.  This may be useful
 * for the sake of memory efficiency, because you can nest this inside
 * your private struct and use container_of() to get the data.
 */
struct bucket_t {
        char *key;
        unsigned long hash;
        struct bucket_t *next;
        unsigned int datalen;
        union {
                unsigned long long _align;
                void *data;
        };
};

struct hashtable_t;

/* Functions documented with their implementation in hashtable.c */

extern struct hashtable_t *hashtable_create(unsigned int flags,
                                unsigned long (*algo)(const char *));

extern void hashtable_free(struct hashtable_t *tbl,
                           void (*cleanup)(char *, void *));

extern int hashtable_put(struct hashtable_t *tbl, const char *key,
                void *data, size_t len, unsigned int flags);

extern int hashtable_put_bucket(struct hashtable_t *tbl,
                                struct bucket_t *bucket);

extern void *hashtable_get(struct hashtable_t *tbl,
                        const char *key, size_t *len);

extern struct bucket_t *hashtable_get_bucket(struct hashtable_t *tbl,
                                                const char *key);

extern bool hashtable_remove(struct hashtable_t *tbl, const char *key);

extern int hashtable_for_each(struct hashtable_t *tbl,
                int (*action)(const char *, void *, size_t));

extern int hashtable_for_each_bucket(struct hashtable_t *tbl,
                                int (*action)(struct bucket_t *));

# ifdef TEST_HASHTABLE__
#  include <stdio.h>
struct test_data_t {
        int num_lookups;
        double max_lookup_time;
        double sum_lookup_time;
};
extern void hashtable_diag(const struct test_data_t *test,
                           struct hashtable_t *tbl, FILE *fp);
# endif /* TEST_HASHTABLE__ */

#endif /* HASHTABLE_H */


