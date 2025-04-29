#include <evilcandy.h>

struct propertyvar_t {
        Object base;
        struct type_prop_t props;
};

#define V2P(v_) ((struct propertyvar_t *)(v_))

static bool
property_cmpz(Object *self)
{
        return false;
}

static int
property_cmp(Object *a, Object *b)
{
        /* These should all be unique */
        return OP_CMP((uintptr_t)a, (uintptr_t)b);
}

static Object *
property_str(Object *self)
{
        bug();
        /* since bug() doesn't catch things in release mode */
        return stringvar_new("<property getter/setter>");
}

struct type_t PropertyType = {
        .name   = "property",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct propertyvar_t),
        .str    = property_str,
        .cmp    = property_cmp,
        .cmpz   = property_cmpz,
        .reset  = NULL,
        .prop_getsets = NULL,
};

/**
 * property_set - Set an object's property
 * @prop: A setter/getter from the object's type handle
 * @owner: The object with a property to set
 * @value: The value to set the property to
 *
 * Return: RES_OK if all was set OK, RES_ERROR if failure.
 *      If the property is read-only, a TypeError will be thrown.
 */
enum result_t
property_set(Object *prop, Object *owner, Object *value)
{
        struct propertyvar_t *pr = V2P(prop);
        bug_on(!isvar_property(prop));
        if (pr->props.setprop)
                return pr->props.setprop(owner, value);
        err_setstr(TypeError, "Property %s is read-only for type %s",
                   pr->props.name, typestr(owner));
        return RES_ERROR;
}

/**
 * property_get - Get an object's property
 * @prop: A setter/getter from the object's type handle
 * @owner: The object with a property to set
 *
 * Return: The property's value, or ErrorVar if an error occured.
 *      If the property is write-only, a TypeErro will be thrown.
 */
Object *
property_get(Object *prop, Object *owner)
{
        struct propertyvar_t *pr = V2P(prop);
        bug_on(!isvar_property(prop));
        if (pr->props.getprop)
                return pr->props.getprop(owner);
        err_setstr(TypeError, "Property %s is write-only for type %s",
                   pr->props.name, typestr(owner));
        return ErrorVar;
}

Object *
propertyvar_new(const struct type_prop_t *props)
{
        Object *ret = var_new(&PropertyType);
        struct propertyvar_t *pr = V2P(ret);
        memcpy(&pr->props, props, sizeof(*props));
        return ret;
}

