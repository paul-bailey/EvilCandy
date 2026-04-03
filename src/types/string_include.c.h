
static bool
STRING_HELPER(match_here)(const TYPE *a, const TYPE *b, size_t size)
{
        size_t i;
        /*
         * only for 'find' functions: we already confirmed
         * index zero matches, so only check from index 1.
         */
        for (i = 1; i < size; i++) {
                if (a[i] != b[i])
                        return false;
        }
        return true;
}

/*
 * For scenarios where @nsrc is really long, I could speed this up
 * dramatically using the Boyer-Moore Horspool algorithm.  The problem
 * is, since we're dealing with Unicode instead of ASCII, I cannot
 * naively use a 256-byte table.  I would have to build up a hash table
 * or array-mapped trie.  99% of the time, any speed advantage I could
 * get from that would be completely lost due to overhead.  So I'm
 * keeping the algorithm basic.
 */
static ssize_t
STRING_HELPER(lfind_idx)(const TYPE *hsrc, const TYPE *nsrc,
                               size_t start, size_t stop, size_t nlen,
                               unsigned int flags)
{
        size_t i;
        ssize_t count = 0;
        bool counting = !!(flags & SF_COUNT);
        for (i = start; i < stop + 1 - nlen; i++) {
                if (hsrc[i] == nsrc[0]) {
                        if (nlen == 1
                            || STRING_HELPER(match_here)(
                                        &hsrc[i+1], &nsrc[1], nlen - 1)) {
                                if (!counting)
                                        return i;
                                i += nlen - 1;
                                count++;
                                continue;
                        }
                }
        }
        return counting ? count : -1;
}

static ssize_t
STRING_HELPER(rfind_idx)(const TYPE *hsrc, const TYPE *nsrc,
                 size_t start, size_t stop, size_t nlen)
{
        ssize_t i;
        for (i = stop - nlen; i >= (ssize_t)start; i--) {
                if (hsrc[i] == nsrc[0]) {
                        if (nlen == 1)
                                return i;
                        if (STRING_HELPER(match_here)(
                                        &hsrc[i+1], &nsrc[1], nlen - 1))
                                return i;
                }
        }
        return -1;
}

static ssize_t
STRING_HELPER(find_idx)(const void *hsrc_, const void *nsrc_,
                size_t start, size_t stop, size_t nlen,
                unsigned int flags)
{
        const TYPE *hsrc = (TYPE *)hsrc_;
        const TYPE *nsrc = (TYPE *)nsrc_;
        bug_on(stop < start);
        if (!!(flags & SF_RIGHT)) {
                return STRING_HELPER(rfind_idx)(hsrc, nsrc, start,
                                                      stop, nlen);
        } else {
                return STRING_HELPER(lfind_idx)(hsrc, nsrc, start,
                                                      stop, nlen, flags);
        }
}

static size_t
STRING_HELPER(strip)(TYPE *src, size_t srclen, TYPE *skip,
           size_t skiplen, size_t width, unsigned int flags,
           size_t *new_end)
{
        size_t new_start = 0;
        if (!(flags & SF_RIGHT)) {
                size_t i, j;
                for (i = 0; i < srclen; i++) {
                        for (j = 0; j < skiplen; j++) {
                                if (src[i] == skip[j])
                                        break;
                        }
                        if (j == skiplen)
                                break;
                }
                new_start = i;
        }
        *new_end = srclen;
        if (!!(flags & (SF_CENTER|SF_RIGHT))) {
                size_t i, j;
                for (i = srclen - 1; (ssize_t)i >= new_start; i--) {
                        for (j = 0; j < skiplen; j++) {
                                if (src[i] == skip[j])
                                        break;
                        }
                        if (j == skiplen)
                                break;
                }
                *new_end = i + 1;
        }
        return new_start;
}


