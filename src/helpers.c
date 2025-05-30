/*
 * helpers.c - Stuff that ought to be in the C standard library but isn't
 */
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

/**
 * notdir - Return pointer into @path of a file
 *              without the directory.
 * @path: A directory path, where separator is '/'
 */
const char *
notdir(const char *path)
{
        /* FIXME: Not portable! We need a per-platform separator */
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
 * srchr_nonnull - Like  strchr but return NULL if c is '\0'
 */
char *
strchr_nonnull(const char *charset, int c)
{
        if (c) while (*charset) {
                if (*charset == c)
                        return (char *)charset;
                charset++;
        }
        return NULL;
}

/* FIXME: Need a width arg */
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
 * bit_count64 - Count the number of '1' bits in a 64-bit datum
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

/**
 * slide - skip whitespace and separators
 * @src: Input string
 * @sep: If NULL, only skip whitespace.  If not NULL, this is a set
 *      of characters to skip in addition to whitespace.
 *
 * Return: Pointer into @s of the first non-whitespace, non-@sep
 *         character.
 */
char *
slide(const char *src, const char *sep)
{
        int c;
        while ((c = *src) != '\0' && isspace(c)
                        && (!sep || strchr(sep, c) != NULL)) {
                src++;
        }
        return (char *)src;
}

