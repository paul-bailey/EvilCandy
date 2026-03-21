#include <evilcandy.h>

struct propertyvar_t {
        Object base;
        enum {
                PR_INTL = 1,
                PR_USER,
        } pr_kind;
        union {
                struct type_prop_t pr_props;
                struct {
                        Object *pr_set;
                        Object *pr_get;
                        Object *pr_name;
                };
        };
};

#define V2P(v_) ((struct propertyvar_t *)(v_))

struct type_t PropertyType = {
        .flags  = 0,
        .name   = "property",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct propertyvar_t),
        .str    = NULL,
        .cmp    = NULL,
        .cmpz   = NULL,
        .reset  = NULL,
        .prop_getsets = NULL,
        .hash   = NULL,
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
        if (pr->pr_kind == PR_INTL) {
                if (pr->pr_props.setprop)
                        return pr->pr_props.setprop(owner, value);
                err_setstr(TypeError,
                           "Property %s is read-only for type %s",
                           pr->pr_props.name, typestr(owner));
        } else {
                bug_on(pr->pr_kind != PR_USER);
                if (pr->pr_set) {
                        Object *meth, *args, *retval;
                        if (isvar_function(pr->pr_set)) {
                                meth = methodvar_new(pr->pr_set, owner);
                        } else {
                                meth = VAR_NEW_REF(pr->pr_set);
                        }
                        args = arrayvar_from_stack(&value, 1, false);
                        retval = vm_exec_func(NULL, meth, args, NULL);
                        VAR_DECR_REF(args);
                        VAR_DECR_REF(meth);
                        if (retval != ErrorVar) {
                                /* should be NullVar then */
                                VAR_DECR_REF(retval);
                                return RES_OK;
                        }
                } else {
                        err_setstr(TypeError,
                                   "Property %N is read-only for type %s",
                                   pr->pr_name, typestr(owner));
                }
        }
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
        if (pr->pr_kind == PR_INTL) {
                if (pr->pr_props.getprop)
                        return pr->pr_props.getprop(owner);
                err_setstr(TypeError, "Property %s is write-only for type %s",
                           pr->pr_props.name, typestr(owner));
        } else {
                bug_on(pr->pr_kind != PR_USER);
                if (pr->pr_get) {
                        Object *meth, *retval;
                        if (isvar_function(pr->pr_get))
                                meth = methodvar_new(pr->pr_get, owner);
                        else
                                meth = VAR_NEW_REF(pr->pr_get);
                        retval = vm_exec_func(NULL, meth, NULL, NULL);
                        VAR_DECR_REF(meth);
                        return retval;
                }
                err_setstr(TypeError, "Property %N is write-only for type %s",
                           pr->pr_name, typestr(owner));
        }
        return ErrorVar;
}

Object *
propertyvar_new_intl(const struct type_prop_t *props)
{
        Object *ret = var_new(&PropertyType);
        struct propertyvar_t *pr = V2P(ret);

        pr->pr_kind = PR_INTL;
        bug_on(!props);
        memcpy(&pr->pr_props, props, sizeof(*props));
        return ret;
}

Object *
propertyvar_new_user(Object *setter, Object *getter, Object *name)
{
        Object *ret = var_new(&PropertyType);
        struct propertyvar_t *pr = V2P(ret);
        pr->pr_kind = PR_USER;

        pr->pr_set = VAR_NEW_REF(setter);
        pr->pr_get = VAR_NEW_REF(getter);
        pr->pr_name = VAR_NEW_REF(name);
        return ret;
}


