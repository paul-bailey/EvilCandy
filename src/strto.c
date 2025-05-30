/*
 * str2enum.c - It would be in helpers.c, except it's more project-specific.
 */
#include <evilcandy.h>
#include <stdlib.h>
#include <errno.h>

enum result_t
str2enum(const struct str2enum_t *t, const char *s, int *value)
{
        while (t->s != NULL) {
                if (!strcmp(t->s, s)) {
                        *value = t->v;
                        return RES_OK;
                }
                t++;
        }
        return RES_ERROR;
}

enum result_t
strobj2enum(const struct str2enum_t *t, Object *str, int *value,
            int suppress, const char *what)
{
        const char *s;

        if (!isvar_string(str)) {
                if (!suppress)
                        err_argtype(typestr(str));
                return RES_ERROR;
        }
        s = string_cstring(str);
        if (str2enum(t, s, value) == RES_ERROR) {
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

