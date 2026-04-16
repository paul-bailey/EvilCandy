/*
 * XXX: Should be src/types/namespace.c instead of src/namespace.c, except
 * that it doesn't use its own struct type_t.
 */
#include <evilcandy/var.h>
#include <evilcandy/global.h>
#include <evilcandy/types/class.h>
#include <internal/type_protocol.h>
#include <internal/types/internal_types.h>

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
        Object *type, *ns;

        if (name == NullVar)
                name = NULL;

        /*
         * XXX: is there any good reason we shouldn't just return
         * type instead of its one single instance?
         */
        type = typevar_new_intl(NULL, dict, name);
        ((struct type_t *)type)->flags
                |= OBF_NO_BIND_FUNCTION_ATTRS;
        ns = instancevar_new(type, NULL, NULL, false);
        VAR_DECR_REF(type);
        return ns;
}

