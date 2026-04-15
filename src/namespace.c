/*
 * XXX: Should be src/types/namespace.c instead of src/namespace.c, except
 * that it doesn't use its own struct type_t.
 */
#include <evilcandy.h>
#include <evilcandy/var.h>
#include <evilcandy/global.h>

/**
 * namespacevar_new - Create a new namespace object.
 * @dict:       arg with same rules as dict arg to classvar_new
 * @name:       arg with same rules as name arg to classvar_new
 *
 * Return: A namespace instantiation.
 */
Object *
namespacevar_new(Object *dict, Object *name)
{
        Object *class, *ns;

        if (name == NullVar)
                name = NULL;

        class = classvar_new(NULL, dict, name, NULL);
        ns = instancevar_new(class, NULL, NULL, false);
        VAR_DECR_REF(class);
        return ns;
}

