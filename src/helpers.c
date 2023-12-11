/* helpers.c - Wrappers to standard library functions that I find useful */
#include <evilcandy.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
        const char *ret = my_strrchrnul(path, '/');
        if (ret[0] == '\0')
                return path;
        return ret + 1;
}

/* Amazinglly, this is not in every C library */
char *
my_strrchrnul(const char *s, int c)
{
        const char *ret = NULL;
        while (*s != '\0') {
                if (*s == (char)c)
                        ret = s;
                ++s;
        }
        return ret ? (char *)ret : (char *)s;
}

/*
 * my_strrspn - Like strspn, but from the right
 * @s:          Input string
 * @charset:    Characters to filter
 * @end:        Pointer to last character in @s before the nullchar
 *              termination...since our struct buffer_t's happen to know
 *              this without requiring a strlen call.
 *
 * Return: Number of characters spanned.
 */
size_t
my_strrspn(const char *s, const char *charset, const char *end)
{
        const char *end_save = end;
        while (end >= s && strchr(charset, *end))
                end--;
        return end_save - end;
}

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
 * index_translate - Convert user index (or offset) to C index
 * @i:          Index from user
 * @size:       Size of the array being indexed
 *
 * Return: Transformed @i, or -1 if it is out of the range determined
 *         by @size
 *
 * To user, "i<0" means "index from end."  This function converts that
 * into an index from the start.
 */
ssize_t
index_translate(ssize_t i, size_t size)
{
        if (i < 0)
                i += size;
        return (size_t)i >= size ? -1 : i;
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
                a = malloc(new_alloc);
                if (!a)
                        return -1;
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

        a = realloc(*arr, new_alloc);
        if (!a)
                return -1;

done:
        *arr = a;
        *alloc_bytes = new_alloc;
        return 0;
}

/*
 * fnv_hash - The FNV-1a hash algorithm
 *
 * See Wikipedia article on this.
 * It could be made into 64-bit version with different consts.
 * Users may want to make a case-less version of this, for
 * things like case-insensitive databases.
 */
unsigned long
fnv_hash(const char *s)
{
        unsigned int c;
        unsigned long hash = 0x811c9dc5;
        while ((c = (unsigned char)(*s++)) != '\0')
                hash = (hash * 0x01000193) ^ c;
        return hash;
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

        info->enc_len = s - start - skip;
        info->ascii_len = s - start;
        info->enc = enc;
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

