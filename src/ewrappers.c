/*
 * ewrappers.c - "Do the other function but throw an error if it fails"
 *
 * This should make the error reporting cleaner and more uniform.
 */
#include <evilcandy.h>
#include <stdlib.h>
#include <string.h>

/**
 * estrdup - error-handling wrapper to strdup
 */
char *
estrdup(const char *s)
{
        char *res = strdup(s);
        if (!res)
                fail("strdup failed");
        return res;
}

/**
 * emalloc - error-handling wrapper to malloc
 */
void *
emalloc(size_t size)
{
        void *res = malloc(size);
        if (!res)
                fail("malloc failed");
        return res;
}

/**
 * ecalloc - like emalloc but it initializes allocated memory to 0
 */
void *
ecalloc(size_t size)
{
        void *res = emalloc(size);
        memset(res, 0, size);
        return res;
}

int
ebuffer_substr(struct buffer_t *tok, int i)
{
        int c = buffer_substr(tok, i);
        if (c < 0)
                syntax("String index out of bounds");
        return c;
}

static void
atrerr(struct var_t *obj, struct var_t *deref, const char *what)
{
        const char *attrstr;
        char numbuf[64];
        switch (deref->magic) {
        case Q_STRPTR_MAGIC:
                attrstr = (const char *)deref->strptr;
                break;
        case QSTRING_MAGIC:
                attrstr = (const char *)string_get_cstring(deref);
                break;
        case QINT_MAGIC:
                sprintf(numbuf, "%llu", deref->i);
                attrstr = numbuf;
                break;
        default:
                attrstr = "[egq: likely bug]";
                break;
        }
        syntax("Cannot %s attribute '%s' of type %s",
               what, attrstr, typestr(obj->magic));
}

struct var_t *
evar_get_attr(struct var_t *obj, struct var_t *deref)
{
        struct var_t *v = var_get_attr(obj, deref);
        if (!v)
                atrerr(obj, deref, "get");
        return v;
}

int
evar_set_attr(struct var_t *obj, struct var_t *deref, struct var_t *attr)
{
        int res = var_set_attr(obj, deref, attr);
        if (res != 0)
                atrerr(obj, deref, "set");
        return res;
}

