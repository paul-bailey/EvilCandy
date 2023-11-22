/*
 * ewrappers.c - "Do the other function but throw an error if it fails"
 *
 * This should make the error reporting cleaner and more uniform.
 */
#include "egq.h"
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

struct var_t *
ebuiltin_method(struct var_t *v, const char *method_name)
{
        struct var_t *ret = builtin_method(v, method_name);
        if (!ret) {
                syntax("type %s has no method %s",
                        typestr(v->magic), method_name);
        }
        return ret;
}

int
ebuffer_substr(struct buffer_t *tok, int i)
{
        int c = buffer_substr(tok, i);
        if (c < 0)
                syntax("String index out of bounds");
        return c;
}

struct var_t *
eobject_child(struct var_t *o, const char *s)
{
        return eobject_child_l(o, literal(s));
}

struct var_t *
eobject_child_l(struct var_t *o, const char *s)
{
        struct var_t *v;
        v = object_child_l(o, s);
        if (!v)
                syntax("object %s has no child %s", nameof(o), s);
        return v;
}

struct var_t *
eobject_nth_child(struct var_t *o, int n)
{
        struct var_t *v;
        v = object_nth_child(o, n);
        if (!v)
                syntax("object %s has no %dth child", nameof(o), n);
        return v;
}

int
earray_child(struct var_t *array, int idx, struct var_t *child)
{
        int ret = array_child(array, idx, child);
        if (ret < 0)
                syntax("Array %s has no %llith element", nameof(array), idx);
        return ret;
}

int
earray_set_child(struct var_t *array, int idx, struct var_t *child)
{
        int ret = array_set_child(array, idx, child);
        if (ret < 0)
                syntax("Array index %d out of bounds", idx);
        return ret;
}

struct var_t *
esymbol_seek(const char *name)
{
        struct var_t *ret = symbol_seek(name);
        if (!ret)
                syntax("Symbol %s not found", name);
        return ret;
}

