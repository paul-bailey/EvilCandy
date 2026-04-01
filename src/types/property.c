#include <evilcandy.h>

struct propertyvar_t {
        Object base;
        enum {
                /* if this property uses built-in callbacks */
                PR_INTL = 1,
                /* if this property uses script callbacks */
                PR_USER,
        } pr_kind;
        union {
                /* if pr_kind == PR_INTL */
                struct type_prop_t pr_props;
                /* if pr_kind == PR_USER */
                struct {
                        Object *pr_set;
                        Object *pr_get;
                };
        };
};

#define V2P(v_) ((struct propertyvar_t *)(v_))

static Object *
property_create(Frame *fr)
{
        Object *getter_pos, *getter_kw, *setter_pos, *setter_kw;
        setter_pos = setter_kw = getter_pos = getter_kw = NULL;
        if (vm_getargs(fr, "[|<x><x>]{|<x><x>}:property",
                       &getter_pos, &setter_pos,
                       STRCONST_ID(get), &getter_kw,
                       STRCONST_ID(set), &setter_kw) == RES_ERROR) {
                return ErrorVar;
        }
        if (getter_pos == NullVar)
                getter_pos = NULL;
        if (getter_kw == NullVar)
                getter_kw = NULL;
        if (setter_pos == NullVar)
                setter_pos = NULL;
        if (setter_kw == NullVar)
                getter_kw = NULL;
        if (!getter_pos) {
                getter_pos = getter_kw;
        } else if (getter_kw) {
                err_doublearg("get");
                return ErrorVar;
        }
        if (!setter_pos) {
                setter_pos = setter_kw;
        } else if (setter_kw) {
                err_doublearg("set");
                return ErrorVar;
        }
        if (!getter_pos && !setter_pos) {
                err_setstr(ArgumentError,
                           "property() expects at least one of 'set' or 'get'");
                return ErrorVar;
        }
        return propertyvar_new_user(setter_pos, getter_pos);
}

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
        .create = property_create,
        .hash   = NULL,
        .iter_next = NULL,
        .get_iter = NULL,
};

/**
 * property_set - Set an object's property
 * @prop: A setter/getter from the object's type handle
 * @owner: The object with a property to set
 * @value: The value to set the property to
 * @name: name of property, for error reporting
 *
 * Return: RES_OK if all was set OK, RES_ERROR if failure.
 *      If the property is read-only, a TypeError will be thrown.
 */
enum result_t
property_set(Object *prop, Object *owner, Object *value, Object *name)
{
        struct propertyvar_t *pr = V2P(prop);
        bug_on(!isvar_property(prop));
        if (pr->pr_kind == PR_INTL) {
                if (pr->pr_props.setprop)
                        return pr->pr_props.setprop(owner, value);
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
                }
        }

        err_setstr(TypeError, "Property %N is read-only for type %s",
                   name, typestr(owner));
        return RES_ERROR;
}

/**
 * property_get - Get an object's property
 * @prop: A setter/getter from the object's type handle
 * @owner: The object with a property to set
 * @name: name of property, for error reporting
 *
 * Return: The property's value, or ErrorVar if an error occured.
 *      If the property is write-only, a TypeErro will be thrown.
 */
Object *
property_get(Object *prop, Object *owner, Object *name)
{
        struct propertyvar_t *pr = V2P(prop);
        bug_on(!isvar_property(prop));
        if (pr->pr_kind == PR_INTL) {
                if (pr->pr_props.getprop)
                        return pr->pr_props.getprop(owner);
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
        }
        err_setstr(TypeError, "Property %N is write-only for type %s",
                   name, typestr(owner));
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
propertyvar_new_user(Object *setter, Object *getter)
{
        Object *ret = var_new(&PropertyType);
        struct propertyvar_t *pr = V2P(ret);
        pr->pr_kind = PR_USER;

        if (setter)
                pr->pr_set = VAR_NEW_REF(setter);
        if (getter)
                pr->pr_get = VAR_NEW_REF(getter);
        return ret;
}

