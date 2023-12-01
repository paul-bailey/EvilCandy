/* helpers.c - Wrappers to standard library functions that I find useful */
#include "egq.h"
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


/* Amazinglly, this is not C libraries */
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

