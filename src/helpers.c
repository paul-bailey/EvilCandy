/* helpers.c - Wrappers to standard library functions that I find useful */
#include <evilcandy.h>

/**
 * x2bin - interpret a hex char
 * @c: a character representing a hex digit
 *
 * Return; Interpreted @c or -1 if @c does not
 * represent a hex digit.
 */
int
x2bin(int c)
{
        if (isdigit(c))
                return c - '0';
        else if (isxdigit(c))
                return toupper(c) - ('A' - 10);
        return -1;
}

const char *
notdir(const char *path)
{
        const char *ret = strrchrnul(path, '/');
        if (ret[0] == '\0')
                return path;
        return ret + 1;
}

#ifndef HAVE_STRRSTR
char *
strrstr(const char *haystack, const char *needle)
{
        size_t nlen = strlen(needle);
        const char *end = &haystack[strlen(haystack) - nlen];
        while (end >= haystack) {
                if (!memcmp(end, needle, nlen))
                        return (char *)end;
                end--;
        }
        return NULL;
}
#endif /* HAVE_STRRSTR */

#ifndef HAVE_STRRCHRNUL
/* Amazinglly, this is not in every C library */
char *
strrchrnul(const char *s, int c)
{
        const char *ret = NULL;
        while (*s != '\0') {
                if (*s == (char)c)
                        ret = s;
                ++s;
        }
        return ret ? (char *)ret : (char *)s;
}
#endif /* HAVE_STRRCHRNUL */

#ifndef HAVE_STRRSPN
/*
 * strrspn - Like strspn, but from the right
 * @s:          Input string
 * @charset:    Characters to filter
 * @end:        Pointer to last character in @s before the nullchar
 *              termination...it's not a very standard-looking arg,
 *              but our struct buffer_t's happen to already know this,
 *              forgoing our need for a strlen call.
 *
 * Return: Number of characters spanned.
 */
size_t
strrspn(const char *s, const char *charset, const char *end)
{
        const char *end_save = end;
        while (end >= s && strchr(charset, *end))
                end--;
        return end_save - end;
}
#endif /* HAVE_STRRSPN */

/**
 * memcount - Count the non-overlapping occurrances of @needle within
 *            @haystack.
 * @haystack: Data buffer which might contain instances of @needle
 * @hlen:     Length of @haystack
 * @needle:   Data to compare sub-buffers of @haystack against
 * @nlen:     Length of @needle
 */
int
memcount(const void *haystack, size_t hlen,
         const void *needle, size_t nlen)
{
        int count = 0;
        size_t i;
        if (nlen == 1) {
                /*
                 * Honestly I don't know if every implementation of
                 * memchr disregards nullchar termination, so just do
                 * this manually.
                 */
                unsigned char *p8 = (unsigned char *)haystack;
                unsigned char c = *(unsigned char *)needle;
                for (i = 0; i < hlen; i++) {
                        if (p8[i] == c)
                                count++;
                }
        } else if (nlen > hlen && hlen > 0) {
                i = 0;
                while (i + nlen < hlen) {
                        if (!memcmp(&haystack[i], needle, nlen)) {
                                count++;
                                i += nlen;
                        } else {
                                i++;
                        }
                }
        }
        return count;
}

#ifndef HAVE_MEMMEM
/**
 * memmem - Like strstr, but for any kind of data
 *
 * This is a Gnu extension, so a better version than this will likely
 * be linked instead.
 */
void *
memmem(const void *haystack, size_t hlen,
       const void *needle, size_t nlen)
{
        if (!nlen)
                return NULL;

        while (hlen > nlen) {
                if (!memcmp(haystack, needle, nlen))
                        return (void *)haystack;
                hlen--;
                haystack++;
        }
        return NULL;
}
#endif /* HAVE_MEMMEM */

#ifndef HAVE_MEMRMEM
/**
 * memrmem - Like memmem, but from the right
 */
void *
memrmem(const void *haystack, size_t hlen,
        const void *needle, size_t nlen)
{
        const void *end;
        if (!nlen)
                return NULL;

        end = haystack + hlen - nlen;
        while (end >= haystack) {
                if (!memcmp(end, needle, nlen))
                        return (void *)end;
                end--;
        }
        return NULL;
}
#endif /* HAVE_MEMRMEM */

/**
 * bit_count16 - Count the number of '1' bits in an 16-bit datum
 * @v: Data whose '1' bits are counted.
 *
 * Return: Number of '1' bits in @v.
 */
int
bit_count16(uint16_t v)
{
        v = (v & 0x5555U) + ((v >> 1) & 0x5555U);
        v = (v & 0x3333U) + ((v >> 2) & 0x3333U);
        v = (v & 0x0F0FU) + ((v >> 4) & 0x0F0FU);
        v = (v & 0x00FFU) + ((v >> 8) & 0x00FFU);
        return v;
}

/**
 * bit_count32 - Count the number of '1' bits in an 32-bit datum
 * @v: Data whose '1' bits are counted.
 *
 * Return: Number of '1' bits in @v.
 *
 * Credit to the book *Hacker's Delight* by Henry S. Warren, Jr.
 */
int
bit_count32(uint32_t v)
{
        v = (v & 0x55555555U) + ((v >> 1) & 0x55555555U);
        v = (v & 0x33333333U) + ((v >> 2) & 0x33333333U);
        v = (v & 0x0F0F0F0FU) + ((v >> 4) & 0x0F0F0F0FU);
        v = (v & 0x00FF00FFU) + ((v >> 8) & 0x00FF00FFU);
        v = (v & 0x0000FFFFU) + ((v >> 16) & 0x0000FFFFU);
        return v;
}

/**
 * bit_count32 - Count the number of '1' bits in a 64-bit datum
 *
 * Credit to the book *Hacker's Delight" by Henry S. Warren, Jr.
 */
int
bit_count64(uint64_t v)
{
        v = (v & 0x5555555555555555ull) + ((v >> 1) & 0x5555555555555555ull);
        v = (v & 0x3333333333333333ull) + ((v >> 2) & 0x3333333333333333ull);
        v = (v & 0x0f0f0f0f0f0f0f0full) + ((v >> 4) & 0x0f0f0f0f0f0f0f0full);
        v = (v & 0x00ff00ff00ff00ffull) + ((v >> 8) & 0x00ff00ff00ff00ffull);
        v = (v & 0x0000ffff0000ffffull) + ((v >> 16) & 0x0000ffff0000ffffull);
        v = (v & 0x00000000ffffffffull) + ((v >> 32) & 0x00000000ffffffffull);
        return (int)v;
}

/*
 * Count trailing bits.
 * These return a meaningless 31 and 63, respectively, if x is zero.
 */
int
ctz32(uint32_t x)
{
        int i = 0;
        if (!(x & 0xffffu)) {
                x >>= 16;
                i += 16;
        }
        if (!(x & 0xffu)) {
                x >>= 8;
                i += 8;
        }
        if (!(x & 0xfu)) {
                x >>= 4;
                i += 4;
        }
        if (!(x & 0x3u)) {
                x >>= 2;
                i += 2;
        }
        if (!(x & 1))
                i++;
        return i;
}

int
ctz64(uint64_t x)
{
        int i = 0;
        if (!(x & 0xffffffffu)) {
                x >>= 32;
                i += 32;
        }
        if (!(x & 0xffffu)) {
                x >>= 16;
                i += 16;
        }
        if (!(x & 0xffu)) {
                x >>= 8;
                i += 8;
        }
        if (!(x & 0xfu)) {
                x >>= 4;
                i += 4;
        }
        if (!(x & 0x3u)) {
                x >>= 2;
                i+= 2;
        }
        if (!(x & 1))
                i++;
        return i;
}

/* Helper to match */
static bool
matchhere(const char *needle, const char *haystack)
{
        while (*haystack != '\0' && *haystack == *needle) {
                haystack++;
                needle++;
        }
        return *needle == '\0';
}

/**
 * match - Find instance of needle in haystack
 * @needle: String to match.  This is an exact expression, not a
 *              pattern text or regular expression (use rematch
 *              for that)
 * @haystack: String that may contain match
 *
 * Return: First instance in @haystack containing @needle, in number
 * of characters traversed. or -1 if @needle not found.
 */
ssize_t
match(const char *needle, const char *haystack)
{
        const char *h = haystack;

        if (*h == '\0' || *needle == '\0')
                return -1;

        while (*h) {
                if (matchhere(needle, h))
                        return h - haystack;
                h++;
        }
        return -1;
}

/**
 * print_escapestr - Print a string escaping newlines and control
 *                      characters.
 * @fp: File to print to
 * @s:  Input string
 * @quote: Char to wrap as a quote, usu. ' or ", or '\0' to not wrap
 *      with a quote.  If nonzero, any of character in @s matching
 *      quote will be escaped with a backslash.
 */
void
print_escapestr(FILE *fp, const char *s, int quote)
{
        int c;
        if (quote)
                putc(quote, fp);
        while ((c = *s++) != '\0') {
                if (c == quote) {
                        putc('\\', fp);
                        putc(c, fp);
                } else if (isspace(c)) {
                        switch (c) {
                        case ' ': /* can print this one */
                                putc(c, fp);
                                continue;
                        case '\n':
                                c = 'n';
                                break;
                        case '\t':
                                c = 't';
                                break;
                        case '\v':
                                c = 'v';
                                break;
                        case '\f':
                                c = 'f';
                                break;
                        case '\r':
                                c = 'r';
                                break;
                        }
                        putc('\\', fp);
                        putc(c, fp);
                } else if (!isgraph(c)) {
                        putc('\\', fp);
                        putc(((c >> 6) & 0x07) + '0', fp);
                        putc(((c >> 3) & 0x07) + '0', fp);
                        putc((c & 0x07) + '0', fp);
                } else {
                        putc(c, fp);
                }
        }
        if (quote)
                putc(quote, fp);
}

/*
 * None of these pointers may be NULL
 *
 * Return:
 * 0 if success, -1 if failure
 */
int
assert_array_pos(int idx, void **arr,
                 size_t *alloc_bytes, size_t type_size)
{
        /* let's try to be reasonable */
        enum { MAX_ALLOC = 1u << 20 };

        size_t need_size = (idx + 1) * type_size;
        size_t new_alloc;
        void *a = *arr;

        if (!a) {
                new_alloc = need_size;
                if (new_alloc < 8)
                        new_alloc = 8;
                a = emalloc(new_alloc);
                goto done;
        }

        new_alloc = *alloc_bytes;
        while (new_alloc < need_size) {
                new_alloc <<= 2;
                if (new_alloc > MAX_ALLOC)
                        return -1;
        }
        if (new_alloc == *alloc_bytes)
                return 0; /* didn't need to do anything */

        a = erealloc(*arr, new_alloc);

done:
        *arr = a;
        *alloc_bytes = new_alloc;
        return 0;
}

/* true if @c is a valid UTF8 following (ie. not 1st) char */
static inline bool isutf8(unsigned int c)
        { return ((c & 0xC0) == 0x80); }

/**
 * like strlen, except @s may contain UTF-8-encoded Unicode characters.
 * Characters >127 which are not proper UTF-8 will be treated as individuals.
 * Results may be undefined if a proper UTF-8 character shortly follows such
 * a malformed character.
 */
size_t
utf8_strlen(const char *s)
{
        struct utf8_info_t info;
        utf8_scan(s, &info);
        return info.enc_len;
}

/* like utf8_strgetc but without putting a nullchar at the end of @dst */
static size_t
utf8_strgetc_(const char *s, char *dst)
{
        unsigned int c0;
        *dst++ = c0 = *s++;
        if (c0 == 0)
                return 0;

        if (c0 > 127) {
                unsigned int c;
                *dst++ = c = *s++;
                if (!isutf8(c))
                        return 1;
                if ((c0 & 0xe0) == 0xc0)
                        return 2;
                *dst++ = c = *s++;
                if (!isutf8(c))
                        return 1;
                if ((c0 & 0xf0) == 0xe0)
                        return 3;
                *dst++ = c = *s++;
                if (!isutf8(c))
                        return 1;
                if ((c0 & 0xf8) == 0xf0)
                        return 4;
        }
        return 1;
}

/**
 * utf8_strgetc - Get the next character out of @s and store it in @dst
 * @s:          Source tring
 * @dst:        Buffer that must be at least five chars long, to store
 *              the next char or UTF-8 set of chars, plus a nullchar
 *              terminator.
 *
 * Return:
 * Amount of chars stuffed in @dst, not counting a nulchar terminator.
 * This is 0 if *s == '\0', 1 if *s was an ASCII or random large datum,
 * 2-4 if it was a valid UTF-8-encoded character.  In the latter case,
 * the value will remain encoded and copied verbatim in @dst.
 */
size_t
utf8_strgetc(const char *s, char *dst)
{
        size_t ret = utf8_strgetc_(s, dst);
        dst[ret] = '\0';
        return ret;
}

/**
 * utf8_scan - Get info about a C-string
 * @s:  String to inspect
 * @info: Struct to fill with info
 */
void
utf8_scan(const char *s, struct utf8_info_t *info)
{
        int enc = STRING_ENC_ASCII;
        size_t skip = 0;
        const char *start = s;
        int c;

        if (!s) {
                info->enc_len = info->ascii_len = 0;
                info->enc = STRING_ENC_ASCII;
                return;
        }

        while ((c = *s++) != '\0') {
                if ((unsigned)c > 127) {
                        bool malformed = true;
                        const char *ts = s;
                        do {
                                if ((c & 0xe0) == 0xc0) {
                                        if (!isutf8(*ts++))
                                                break;
                                        malformed = false;
                                } else if ((c & 0xf0) == 0xe0) {
                                        if (!isutf8(*ts++))
                                                break;
                                        if (!isutf8(*ts++))
                                                break;
                                        malformed = false;
                                } else if ((c & 0xf8) == 0xf0) {
                                        if (!isutf8(*ts++))
                                                break;
                                        if (!isutf8(*ts++))
                                                break;
                                        if (!isutf8(*ts++))
                                                break;
                                        malformed = false;
                                }
                        } while (0);
                        if (malformed) {
                                enc = STRING_ENC_UNK;
                        } else {
                                if (enc != STRING_ENC_UNK)
                                        enc = STRING_ENC_UTF8;
                                skip += ts - s;
                                s = ts;
                        }
                }
        }
        s--;

        info->ascii_len = s - start;
        info->enc = enc;

        /*
         * If not utf-8 or ASCII, treat as Latin1 or some binary,
         * where #chars == #bytes
         */
        if (enc == STRING_ENC_UTF8)
                info->enc_len = s - start - skip;
        else
                info->enc_len = info->ascii_len;
}

/**
 * Get a subscript of a string, which may be UTF-8
 * @src:        String to search
 * @idx:        Index
 * @dest:       Buffer to contain the undecoded results plus a nulchar
 *              terminator.  This must be at least 5 bytes long.
 *
 * Return:
 * 0 if found, -1 if out of range.
 */
int
utf8_subscr_str(const char *src, size_t idx, char *dest)
{
        int c, i;
        const char *s = src;
        for (i = 0, c = *s++; i < idx && c != '\0'; i++, c = *s++) {
                if ((unsigned)c > 127) {
                        if ((c & 0xe0) == 0xc0) {
                                if (isutf8(s[0]))
                                        s++;
                        } else if ((c & 0xf0) == 0xe0) {
                                if (isutf8(s[0]) && isutf8(s[1]))
                                        s += 2;
                        } else if ((c & 0xf8) == 0xf0) {
                                if (isutf8(s[0]) &&
                                    isutf8(s[1]) &&
                                    isutf8(s[2])) {
                                        s += 3;
                                }
                        }
                }
        }
        if (c == '\0')
                return -1;

        utf8_strgetc(s - 1, dest);
        return 0;
}

/**
 * utf8_encode - Encode a Unicode point in UTF-8
 * @point:      A unicode point from U+0001 to U+10FFFF
 * @buf:        Buffer to store encoded result
 *
 * Behavior is undefined if @point is not valid Unicode
 */
void
utf8_encode(unsigned long point, struct buffer_t *buf)
{
        if (point < 0x7ff) {
                buffer_putc(buf, 0xc0 | (point >> 6));
                buffer_putc(buf, 0x80 | (point & 0x3f));
        } else if (point < 0xffff) {
                buffer_putc(buf, 0xe0 | (point >> 12));
                buffer_putc(buf, 0x80 | ((point >> 6) & 0x3f));
                buffer_putc(buf, 0x80 | (point & 0x3f));
        } else {
                buffer_putc(buf, 0xf0 | (point >> 18));
                buffer_putc(buf, 0x80 | ((point >> 12) & 0x3f));
                buffer_putc(buf, 0x80 | ((point >> 6) & 0x3f));
                buffer_putc(buf, 0x80 | (point & 0x3f));
        }
}

static void
utf8_decode_write_point(struct buffer_t *b,
                        unsigned long point, unsigned int maxwidth)
{
        bug_on(point > 0xfffful && maxwidth < 4);
        bug_on(point > 0xff && maxwidth < 2);
        union {
                uint8_t u8;
                uint16_t u16;
                uint32_t u32;
        } x;
        void *p;

        switch (maxwidth) {
        case 1:
                x.u8 = point;
                p = &x.u8;
                break;
        case 2:
                x.u16 = point;
                p = &x.u16;
                break;
        case 4:
                x.u32 = point;
                p = &x.u32;
                break;
        default:
                bug();
                return;
        }
        buffer_putd(b, p, maxwidth);
}

static long
decode_one_point(const unsigned char *s, unsigned char **endptr,
                 unsigned long point, int n)
{
        const unsigned char *ts = s;

        while (n-- > 0) {
                unsigned int c = *ts++;
                if ((c & 0xc0) != 0x80) {
                        --ts;
                        point = -1L;
                        break;
                }
                point = (point << 6) & (c & 0x3fu);
        }
        *endptr = (unsigned char *)ts;
        return (long)point;
}

/**
 * utf8_decode_one - Get a single unicode point
 * @src: C string containing UTF-8-encoded Unicode points.
 * @endptr: stop point
 *
 * Return value: Unicode point, or -1 if either next char in @src is
 * ASCII or @src points to an invalid UTF-8 sequence.
 */
long
utf8_decode_one(const unsigned char *src, unsigned char **endptr)
{
        unsigned int c = *src++;
        long point = -1;
        do {
                int count;
                if ((c & 0xf8u) == 0xf0u) {
                        c &= 0x07u;
                        count = 3;
                } else if ((c & 0xf0u) == 0xe0u) {
                        c &= 0x0fu;
                        count = 2;
                } else if ((c & 0xe0u) == 0xc0u) {
                        c &= 0x1fu;
                        count = 1;
                } else {
                        break;
                }
                point = decode_one_point(src, endptr, c, count);
        } while (0);

        if (point >= 0LL && !utf8_valid_unicode(point))
                point = -1LL;

        return point;
}

/**
 * utf8_decode - Decode a (possibly) UTF-8 encoded C-string, and return
 *               an array of its Unicode points.
 * @src:        C-string to decode
 * @width:      Variable to store the max width of the return value.
 *              Values are 1, 2, and 4
 * @len:        Variable to store array length of return value.  Total
 *              number of allocated bytes is @len x @width.
 * @ascii:      Variable to store true if @src is all ASCII, false if
 *              @src is non-ASCII.
 *
 * Return:      - @src if @ascii is true
 *              - a newly allocated buffer containing the Unicode points,
 *                even if @width is 1.
 *              - NULL if malformed UTF-8 characters exist in @src.
 */
void *
utf8_decode(const char *src, size_t *width,
            size_t *len, int *ascii)
{
        const unsigned char *s;
        size_t maxwidth;
        unsigned int c;
        struct buffer_t b;

        /*
         * This makes it slower for non-ASCII strings, but
         * most strings will be all-ASCII, so over-all it will
         * probably be faster.
         */
        for (s = (unsigned char *)src; *s != '\0'; s++) {
                if (((unsigned)(*s) & 0xffu) >= 128)
                        break;
        }

        if (*s == '\0') {
                /* ASCII-only, our likeliest fast path */
                *ascii = 1;
                *width = 1;
                *len = (size_t)(s - (unsigned char *)src);
                return (void *)src;
        }

        /*
         * XXX: Some corner cases exist where I set maxwidth > 1
         * but the encoding is Latin1, causing me to waste RAM.
         */
        maxwidth = 1;
        while ((c = *s++) != '\0') {
                if (c < 0xc0)
                        continue;

                if ((c & 0xf8) == 0xf0) {
                        maxwidth = 4;
                } else if ((c & 0xf0) == 0xe0) {
                        if (maxwidth < 2)
                                maxwidth = 2;
                } else if ((c & 0xe0) == 0xc0) {
                        /*
                         * 110aaabb 10bbcccc: if the 'aaa' bits are zero,
                         * then this is range 0x80...0xff (width 1).
                         * Otherwise, the range is 0x100...0x7ff (width 2).
                         */
                        if (c > 0xc3 && maxwidth < 2)
                                maxwidth = 2;
                }
        }

        /*
         * If maxwidth is still 1, then we have some non-ASCII,
         * non-UTF-8 chars in our string.  They are easy enough
         * to parse amidst the UTF-8 characters so long as they
         * are not in the middle of a malformed UTF-8 sequence,
         * so add them to the points array below and tell caller
         * it was UTF-8 all along.
         */

        buffer_init(&b);
        s = (unsigned char *)src;
        while ((c = *s++) != '\0') {
                unsigned long point;
                unsigned char *endptr;
                if (c < 128) {
                        endptr = (unsigned char *)s;
                        point = c;
                } else if ((c & 0xf8) == 0xf0) {
                        point = decode_one_point(s, &endptr, c & 0x07, 3);
                } else if ((c & 0xf0) == 0xe0) {
                        point = decode_one_point(s, &endptr, c & 0x0f, 2);
                } else if ((c & 0xe0) == 0xc0) {
                        point = decode_one_point(s, &endptr, c & 0x1f, 1);
                } else {
                        endptr = (unsigned char *)s;
                        point = c;
                }

                if ((long)point == -1L) {
                        while (s < endptr && *s != '\0') {
                                utf8_decode_write_point(&b, *s, maxwidth);
                                s++;
                        }
                        continue;
                }

                if (!utf8_valid_unicode(point))
                        goto err;

                utf8_decode_write_point(&b, point, maxwidth);
                s = endptr;
        }

        utf8_decode_write_point(&b, 0, maxwidth);

        *ascii = 0;
        *width = maxwidth;
        *len = buffer_size(&b) / maxwidth;
        return buffer_trim(&b);

err:
        buffer_free(&b);
        return NULL;
}

