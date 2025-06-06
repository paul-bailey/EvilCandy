/*
 * str2enum.c - It would be in helpers.c, except it's more project-specific.
 */
#include <evilcandy.h>
#include <stdlib.h>
#include <errno.h>

enum result_t
str2enum(const struct str2enum_t *t, const char *s,
         int *value, bool nocase)
{
        if (nocase) {
                while (t->s != NULL) {
                        if (!strcasecmp(t->s, s)) {
                                *value = t->v;
                                return RES_OK;
                        }
                        t++;
                }
        } else {
                while (t->s != NULL) {
                        if (!strcmp(t->s, s)) {
                                *value = t->v;
                                return RES_OK;
                        }
                        t++;
                }
        }
        return RES_ERROR;
}

enum result_t
strobj2enum(const struct str2enum_t *t, Object *str, int *value,
            int suppress, const char *what, bool nocase)
{
        const char *s;

        if (!isvar_string(str)) {
                if (!suppress)
                        err_argtype(typestr(str));
                return RES_ERROR;
        }
        s = string_cstring(str);
        if (str2enum(t, s, value, nocase) == RES_ERROR) {
                if (!suppress) {
                        err_setstr(ValueError, "Invalid %s value: '%s'",
                                   what, s);
                }
                return RES_ERROR;
        }
        return RES_OK;
}

/**
 * evc_strtod - EvilCandy wrapper to stdlib's strtod
 * @s:          Input string
 * @endptr:     NULL or pointer to store end position
 * @d:          Result
 *
 * Return: RES_ERROR or RES_OK.  No exception will be set.
 */
enum result_t
evc_strtod(const char *s, char **endptr, double *d)
{
        char *ep2;
        errno = 0;
        *d = strtod(s, &ep2);
        if (errno || ep2 == s) {
                errno = 0;
                return RES_ERROR;
        }

        if (endptr)
                *endptr = ep2;
        return RES_OK;
}

/**
 * evc_strtol - EvilCandy wrapper to stdlib's strtoll.
 * @s:          Input string
 * @endptr:     NULL or pointer to store end position
 * @v:          Result
 *
 * If the expression is positive but it sets the 64th bit, it will not
 * be regarded as an error; instead it will become a negative number,
 * the two's-complement value of its final bitfield.
 *
 * Return: RES_ERROR or RES_OK.  No exception will be set.
 */
enum result_t
evc_strtol(const char *s, char **endptr, int base, long long *v)
{
        char *ep2;
        long long sign, res;

        s = slide(s, NULL);
        if (*s == '-') {
                sign = -1LL;
                s++;
        } else {
                sign = 1LL;
        }

        /*
         * GNU/Linux has no problem parsing '0b', but that's apparently
         * not standard.  So, while we're at it, also allow the very-not-
         * standard '0o' header.
         */
        if (base == 0) {
                if (s[0] == '0') {
                        if (s[1] == 'x' || s[1] == 'X') {
                                s += 2;
                                base = 16;
                        } else if (s[1] == 'b' || s[1] == 'B') {
                                s += 2;
                                base = 2;
                        } else if (s[1] == 'o' || s[1] == 'O') {
                                s += 2;
                                base = 8;
                        } else {
                                base = 8;
                        }
                }
        } else if (s[0] == '0') {
                int c;
                switch (base) {
                case 2:
                        c = 'B';
                        break;
                case 8:
                        c = 'O';
                        break;
                case 16:
                        c = 'X';
                        break;
                default:
                        c = 0;
                }

                if (c && (s[1] == c || s[1] == ('a' - 'A') + c))
                        s += 2;
        }

        errno = 0;
        res = strtoull(s, &ep2, base);
        if (errno || ep2 == s) {
                errno = 0;
                return RES_ERROR;
        }

        *v = res * sign;
        if (endptr)
                *endptr = ep2;
        return RES_OK;
}

/* rd will have updated position if return value is true */
static bool
string_reader_match(struct string_reader_t *rd, const char *s)
{
        const unsigned char *us = (unsigned char *)s;
        size_t pos = string_reader_getpos(rd);
        long csrc;
        while ((csrc = *us++) != '\0') {
                long c = string_reader_getc(rd);
                if (c != csrc) {
                        string_reader_setpos(rd, pos);
                        return false;
                }
        }
        return true;
}

/*
 * This assumes that the next 'e' or 'E' after number is an exponent;
 * it cannot be the start of a new token.
 */
static ssize_t
string_span_float(struct string_reader_t *rd,
                  int *may_be_int, bool interpret_enums)
{
        long c;
        size_t startpos, n_ival, n_remval;
        bool maybeint = true;

        startpos = string_reader_getpos(rd);
        c = string_reader_getc(rd);
        if (c != '+' && c != '-')
                string_reader_ungetc(rd, c);

        if (c >= 0 && interpret_enums) {
                if (string_reader_match(rd, "inf")) {
                        c = string_reader_getc(rd);
                        goto done;
                }
                if (string_reader_match(rd, "nan")) {
                        c = string_reader_getc(rd);
                        goto done;
                }
        }

        n_ival = 0;
        do {
                c = string_reader_getc(rd);
                n_ival++;
        } while (c >= '0' && c <= '9');
        n_ival--;
        if (n_ival == 0 && c < 0)
                return -1;

        n_remval = 0;
        if (c == '.') {
                maybeint = false;
                do {
                        c = string_reader_getc(rd);
                        n_remval++;
                } while (c >= '0' && c <= '9');
                n_remval--;
        }
        if (n_ival == 0 && n_remval == 0)
                return -1;

        if (c == 'e' || c == 'E') {
                size_t n_expval;
                maybeint = false;
                c = string_reader_getc(rd);
                if (c != '+' && c != '-')
                        string_reader_ungetc(rd, c);

                n_expval = 0;
                do {
                        c = string_reader_getc(rd);
                        n_expval++;
                } while (c >= '0' && c <= '9');
                n_expval--;
                if (n_expval == 0)
                        return -1;
        }

done:
        if (may_be_int)
                *may_be_int = maybeint;
        return string_reader_getpos_lastread(rd, c) - startpos;
}

/**
 * strtod_scanonly - Skip past the characters of a valid floating-point
 *                   expression.
 * @s:          C string to skip across
 * @may_be_int: If non-NULL, stores true if the expression found in @s may
 *              be either floating point or integer; false if the
 *              expression is definitely floating point.
 *
 * Return: Pointer to the first character in @s after the number, or NULL
 *      if @s does not contain a valid floating-point expression.
 *
 * Warning: This does not skip over any leading whitespace or delimiters.
 *      Calling code must do that first.
 */
char *
strtod_scanonly(const char *s, int *may_be_int)
{
        struct string_reader_t rd;
        ssize_t nscanned;

        string_reader_init_cstring(&rd, s);
        /*
         * FIXME: We're assuming this is called from tokenizer, hence
         * interpret_enums is false, but that may not forever be the case.
         */
        nscanned = string_span_float(&rd, may_be_int, false);
        return nscanned < 0 ? NULL : (char *)s + nscanned;
}

/**
 * string_tod - Like evc_strtod, but for string objects
 * @str: String object expressing floating point value
 * @pos: Start position.  This may not be NULL.  It will store the final
 *       position as well.
 * @result: Result
 *
 * Return: RES_ERROR or RES_OK.  No exception will be set.
 *
 * Notes:
 *      1. This does not slide across any leading whitespace.
 *      2. Integer expressions will be interpreted as floating-point
 *         values.
 */
enum result_t
string_tod(Object *str, size_t *pos, double *reslt)
{
        struct buffer_t b;
        struct string_reader_t rd;
        double d;
        char *endptr;
        enum result_t ret;
        size_t i;
        ssize_t nscanned;

        string_reader_init(&rd, str, *pos);
        /* Interpret ints as floats */
        nscanned = string_span_float(&rd, NULL, true);
        /* == 0 is just as much of an error */
        if (nscanned <= 0)
                return RES_ERROR;
        string_reader_init(&rd, str, *pos);
        buffer_init(&b);
        for (i = 0; i < nscanned; i++) {
                long c = string_reader_getc(&rd);
                bug_on(c < 0 || c > 127);
                buffer_putc(&b, c);
        }
        bug_on(!b.s);
        d = strtod(b.s, &endptr);
        if (errno || *endptr != '\0') {
                ret = RES_ERROR;
        } else {
                *reslt = d;
                *pos += nscanned;
                ret = RES_OK;
        }
        buffer_free(&b);
        return ret;
}

/* skip '0x', '0o' or '0b' */
static int
string_toll_header(struct string_reader_t *rd, int base, size_t startpos)
{
        long c, c2;
        switch (base) {
        case 2:
                c2 = 'B';
                break;
        case 8:
                c2 = 'O';
                break;
        case 16:
                c2 = 'X';
                break;
        default:
                return false;
        }
        c = string_reader_getc(rd);
        if (c != '0')
                goto nope;
        c = string_reader_getc(rd);
        if (c != c2 && c != (c2 + ('a' - 'A')))
                goto nope;
        return true;

nope:
        string_reader_setpos(rd, startpos);
        return false;
}

int
isinbase(long c, int base)
{
        if (c < 0 || c > 127 || !isalnum(c))
                return false;
        if (base <= 10)
                return c >= '0' && c - '0' < base;
        return isdigit(c) || (toupper(c) - ('A' - 10) < base);
}

enum result_t
string_toll(Object *str, int base, size_t *pos, long long *reslt)
{
        struct buffer_t b;
        struct string_reader_t rd;
        size_t hdrpos, startpos, len;
        long c;
        char *endptr;

        startpos = *pos;
        string_reader_init(&rd, str, startpos);

        c = string_reader_getc(&rd);
        if (c != '-' && c != '+')
                string_reader_ungetc(&rd, c);
        hdrpos = string_reader_getpos(&rd);
        if (!string_toll_header(&rd, base, hdrpos) && base == 0)
                base = 10;
        do {
                c = string_reader_getc(&rd);
        } while (isinbase(c, base));
        string_reader_ungetc(&rd, c);
        *pos = string_reader_getpos(&rd);
        len = *pos - hdrpos;
        if (!len)
                return RES_ERROR;

        len += hdrpos - startpos;
        string_reader_init(&rd, str, startpos);
        buffer_init(&b);
        while (len--) {
                long c = string_reader_getc(&rd);
                bug_on(c < 0 || c > 127);
                buffer_putc(&b, c);
        }
        /* XXX: the '0o' header will result in an error here */
        if (evc_strtol(b.s, &endptr, base, reslt) == RES_ERROR)
                return RES_ERROR;
        if (*endptr != '\0') /* XXX: Error, or bug? */
                return RES_ERROR;
        return RES_OK;
}

