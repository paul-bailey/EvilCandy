/*
 * str2enum.c - It would be in helpers.c, except it's more project-specific.
 */
#include <evilcandy.h>

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
