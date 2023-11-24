/*
 * mempool.c - Alloc and free wrappers for certain cases.
 *
 * This is specifically for cases where the following are met:
 * 1. You will need lots and lots of small data chunks
 * 2. It's for chunks of the same type or size
 * 3. You'll be constantly allocating and freeing during the runtime
 *
 * See stack.c and buffer.c for allocating in different scenarios
 */
#include "egq.h"
#include <stdlib.h>
#include <string.h>

#define NDATA_PER_BLK  64

#define mempool_clz(v) \
        (sizeof(v) == 4 \
         ? clz32(v) \
         : (sizeof(v) == 8 \
            ? clz64(v) : ({ bug(); 0; })))

struct mempool_blk_t {
        uint64_t used;
        void *data;
};

struct mempool_t {
        size_t datalen; /* size of each data chunk */
        size_t size;    /* number of @blks */
        struct mempool_blk_t *blks;
};

static void
mempool_more(struct mempool_t *pool)
{
        enum { NBLKS = 16 };

        struct mempool_blk_t *new_blks;
        void *new_data;
        size_t newsize;
        size_t per_blk_datalen = pool->datalen * NDATA_PER_BLK;
        void *p;
        int i;

        newsize = pool->size + NBLKS;
        new_blks = realloc(pool->blks, sizeof(*new_blks) * newsize);
        if (!new_blks)
                fail("realloc failed");

        /*
         * I'm assuming I won't need a "mempool_less"
         * routine, since checking the malloc borders
         * of which pool->blks should be freed.
         */
        new_data = emalloc(per_blk_datalen * NBLKS);

        pool->blks = new_blks;
        p = new_data;
        for (i = pool->size; i < newsize; i++) {
                pool->blks[i].used = 0;
                pool->blks[i].data = p;
                p += per_blk_datalen;
        }
        pool->size = newsize;
}

static inline bool
pool_blk_full(struct mempool_blk_t *blk)
{
        return blk->used == (unsigned int)(~0u);
}

static struct mempool_blk_t *
avail_blk(struct mempool_t *pool)
{
        enum { BLK_SANITY = 1u << 12 };
        int i = 0;
        for (;;) {
                /*
                 * If script really needs this much, then it's a bug that
                 * we cannot support it yet, I guess.  Otherwise it means
                 * we're doing something wrong in this routine.
                 */
                bug_on(i >= BLK_SANITY);

                for (; i < pool->size; i++) {
                        if (!pool_blk_full(&pool->blks[i]))
                                return &pool->blks[i];
                }
                mempool_more(pool);
        }
}

void *
mempool_alloc(struct mempool_t *pool)
{
        struct mempool_blk_t *blk = avail_blk(pool);
        if (!blk->used) {
                blk->used |= 1;
                return blk->data;
        } else {
                unsigned int bit, shift;
                shift = mempool_clz(~blk->used);
                bug_on(shift >= 64);
                bit = 1u << shift;
                blk->used |= bit;
                return blk->data + pool->datalen * shift;
        }
}

void
mempool_free(struct mempool_t *pool, void *data)
{
        int i;
        unsigned int shift;
        size_t dbend = pool->datalen * NDATA_PER_BLK;
        struct mempool_blk_t *blk = NULL;
        for (i = 0, blk = pool->blks; i < pool->size; i++, blk++) {
                if (data >= blk->data && data < (blk->data + dbend))
                        break;
        }
        bug_on(i == pool->size);

        shift = (data - blk->data) / pool->datalen;
        bug_on(blk->data + shift * pool->datalen != data);
        bug_on(!(blk->used & (1u << shift)));

        blk->used &= ~(1u << shift);
}

struct mempool_t *
mempool_new(size_t datalen)
{
        struct mempool_t *new = ecalloc(sizeof(*new));
        new->datalen = datalen;
        return new;
}
