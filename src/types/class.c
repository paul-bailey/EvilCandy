/*
 * Implementation of ClassType and InstanceType objects.
 * MethodType objects are in method.c
 */
#include <evilcandy.h>
#include <var.h>

struct class_t {
        struct seqvar_t obj_head;
        Object *c_bases;
        Object *c_dict;
};

struct instance_t {
        Object obj_head;
        Object *inst_class;
        Object *inst_attr;
        void *inst_priv;
        void (*inst_cleanup)(void *);
};

#define V2CL(obj_)        ((struct class_t *)(obj_))
#define V2INST(obj_)      ((struct instance_t *)(obj_))

static void
class_reset(Object *class)
{
        Object *x;
        x = V2CL(class)->c_bases;
        if (x)
                VAR_DECR_REF(x);

        x = V2CL(class)->c_dict;
        if (x)
                VAR_DECR_REF(x);
}

static Object *
class_getitem(Object *class, Object *key)
{
        struct class_t *cls;
        size_t i, n;
        Object *ret;

        cls = V2CL(class);
        bug_on(!cls->c_dict);
        ret = dict_getitem(cls->c_dict, key);
        if (ret)
                return ret;
        if (!cls->c_bases)
                return NULL;

        n = seqvar_size(cls->c_bases);
        for (i = 0; i < n; i++) {
                Object *item = tuple_borrowitem(cls->c_bases, i);
                ret = class_getitem(item, key);
                if (ret)
                        return ret;
        }
        return NULL;
}

static void
instance_reset(Object *instance)
{
        Object *x;
        struct instance_t *inst = V2INST(instance);

        bug_on(!isvar_instance(instance));
        if (inst->inst_priv && inst->inst_cleanup)
                inst->inst_cleanup(inst->inst_priv);
        inst->inst_cleanup = NULL;
        inst->inst_priv = NULL;

        x = inst->inst_attr;
        inst->inst_attr = NULL;
        if (x)
                VAR_DECR_REF(x);

        x = inst->inst_class;
        inst->inst_class = NULL;
        if (x)
                VAR_DECR_REF(x);
}

/**
 * instance_set_priv - Set instance private data
 * @instance:   Class instance
 * @cleanup:    callback to clean up private data during .reset().
 *              If NULL, then no cleanup will occur.
 * @priv:       Private data
 *
 * For internal classes only
 */
void
instance_set_priv(Object *instance, void (*cleanup)(void *), void *priv)
{
        struct instance_t *inst = V2INST(instance);
        bug_on(!isvar_instance(instance));
        bug_on(inst->inst_priv || inst->inst_cleanup);

        inst->inst_priv = priv;
        inst->inst_cleanup = cleanup;
}

/**
 * instance_get_priv - Get private data installed with instance
 *
 * For internal classes only
 */
void *
instance_get_priv(Object *instance)
{
        bug_on(!isvar_instance(instance));
        return V2INST(instance)->inst_priv;
}

static Object *
instance_getitem(Object *instance, Object *key)
{
        Object *ret;
        struct instance_t *inst;

        inst = V2INST(instance);
        bug_on(!isvar_instance(instance));
        bug_on(!inst->inst_attr);
        bug_on(!inst->inst_class);

        ret = dict_getitem(inst->inst_attr, key);
        if (ret)
                goto found;
        ret = class_getitem(inst->inst_class, key);
        if (ret)
                goto found;
        return NULL;

found:
        if (isvar_function(ret)) {
                Object *tmp = ret;
                ret = methodvar_new(tmp, instance);
                VAR_DECR_REF(tmp);
        } else if (isvar_property(ret)) {
                Object *tmp = ret;
                ret = property_get(ret, instance, key);
                VAR_DECR_REF(tmp);
        }
        return ret;
}

static enum result_t
instance_setitem(Object *instance, Object *key, Object *item)
{
        /* FIXME: what if we're setting a property? */
        bug_on(!isvar_instance(instance));
        return dict_setitem(V2INST(instance)->inst_attr, key, item);
}

static Object *
verify_base_classes(Object *bases, size_t *size)
{
        size_t i, n;
        if (isvar_tuple(bases)) {
                VAR_INCR_REF(bases);
        } else {
                Object *stack[1] = { bases };
                bases = tuplevar_from_stack(stack, 1, false);
        }

        n = seqvar_size(bases);
        for (i = 0; i < n; i++) {
                Object *obj = tuple_borrowitem(bases, i);
                if (!isvar_class(obj)) {
                        err_setstr(TypeError,
                                   "base class may not be type '%s'",
                                   typestr(obj));
                        VAR_DECR_REF(bases);
                        return ErrorVar;
                }
                (*size) += seqvar_size(obj);
        }
        return bases;
}

static Object *
instance_str(Object *instance)
{
        return instance_call(instance, STRCONST_ID(__str__), NULL, NULL);
}

Object *
instance_getattr(Object *instance, Object *key)
{
        return instance_getitem(instance, key);
}

enum result_t
instance_setattr(Object *instance, Object *key, Object *value)
{
        return instance_setitem(instance, key, value);
}

Object *
instance_get_class(Object *instance)
{
        Object *ret;
        bug_on(!isvar_instance(instance));
        ret = V2INST(instance)->inst_class;
        return VAR_NEW_REF(ret);
}

/**
 * instance_call - Call an instance method
 * @instance:           Instance
 * @method_name:        Name of the method to call (a dictionary key)
 * @args:               Args to pass to the method (an array)
 * @kwargs:             Keyword args to pass to the method (a dict)
 *
 * Return: NULL if method not found, result of call otherwise.
 *      If return value is NULL, no exception will be thrown.
 */
Object *
instance_call(Object *instance, Object *method_name,
              Object *args, Object *kwargs)
{
        Object *ret;
        Object *method = instance_getitem(instance, method_name);
        if (!method)
                return NULL;
        if (!isvar_method(method)) {
                /* do not throw error, let caller decide */
                VAR_DECR_REF(method);
                return NULL;
        }
        ret = vm_exec_func(NULL, method, args, kwargs);
        VAR_DECR_REF(method);
        return ret;
}

/**
 * instance_super - Get the base class of an instance.
 *
 * Return: super or NULL if no super.  No exception will be thrown
 *         if super not found.
 */
Object *
instance_super_getattr(Object *instance, Object *attribute_name)
{
        Object *class, *bases;
        size_t i, n;

        bug_on(!isvar_instance(instance));
        class = V2INST(instance)->inst_class;
        bases = V2CL(class)->c_bases;
        if (!bases)
                return NULL;

        n = seqvar_size(bases);
        for (i = 0; i < n; i++) {
                Object *super = tuple_borrowitem(bases, i);
                Object *attr = class_getitem(super, attribute_name);
                if (attr)
                        return attr;
        }

        return NULL;
}

/**
 * classvar_new - Create a class
 * @bases: A tuple containing inherited base classes
 * @dict: Dictionary of methods, properties, and data
 */
Object *
classvar_new(Object *bases, Object *dict)
{
        Object *ret;
        struct class_t *class;
        size_t size;

        bug_on(!dict || !isvar_dict(dict));

        size = seqvar_size(dict);
        if (bases) {
                bases = verify_base_classes(bases, &size);
                if (bases == ErrorVar)
                        return bases;
        }

        ret = var_new(&ClassType);
        class = V2CL(ret);
        class->c_bases = bases;
        class->c_dict = VAR_NEW_REF(dict);
        seqvar_set_size(ret, size);
        return ret;
}

/**
 * instancevar_new - Create a new instance
 * @class:      Class to create an instance of
 * @args:       Arguments to pass to constructor function
 * @kwargs:     Keyword arguments to pass to constructor function
 * @call_init:  true to call init, false otherwise.  This is almost
 *              always true, except with some built-in classes where
 *              it's easier to initialize stuff without a UAPI call.
 *
 * Return: New instance of @class
 */
Object *
instancevar_new(Object *class, Object *args,
                Object *kwargs, bool call_init)
{
        Object *init_result;
        Object *ret = var_new(&InstanceType);

        bug_on(args && (!isvar_tuple(args) && !isvar_array(args)));
        bug_on(kwargs && !isvar_dict(kwargs));
        bug_on(!isvar_class(class));

        V2INST(ret)->inst_class = VAR_NEW_REF(class);
        V2INST(ret)->inst_attr = dictvar_new();
        if (!call_init)
                return ret;

        init_result = instance_call(ret, STRCONST_ID(__init__),
                                    args, kwargs);
        if (init_result == ErrorVar) {
                VAR_DECR_REF(ret);
                return ErrorVar;
        } else if (init_result != NULL) {
                VAR_DECR_REF(init_result);
        } else if ((args && seqvar_size(args) > 0) ||
                   (kwargs && seqvar_size(kwargs) > 0)) {
                err_setstr(ArgumentError,
                        "passing arguments to constructor which does not exist");
                VAR_DECR_REF(ret);
                return ErrorVar;
        }
        /* else: no args and no constructor, no problem! */
        return ret;
}

/**
 * helper to built-in dir().
 * Return an array containing directory of instance attributes.
 */
Object *
instance_dir(Object *instance)
{
        struct instance_t *inst;
        struct class_t *class;
        Object *set, *ret;
        size_t i, n;

        inst = V2INST(instance);
        class = V2CL(inst->inst_class);

        set = setvar_new(inst->inst_attr);
        set_extend(set, class->c_dict);

        n = 0;
        if (class->c_bases)
                n = seqvar_size(class->c_bases);
        for (i = 0; i < n; i++) {
                Object *base = tuple_borrowitem(class->c_bases, i);
                set_extend(set, V2CL(base)->c_dict);
        }
        ret = arrayvar_new(0);
        array_extend(ret, set);
        var_sort(ret);

        VAR_DECR_REF(set);
        return ret;
}

struct type_t ClassType = {
        .flags          = 0,
        .name           = "class",
        .opm            = NULL,
        .cbm            = NULL,
        .mpm            = NULL,
        .sqm            = NULL,
        .size           = sizeof(struct class_t),
        .str            = NULL,
        .cmp            = NULL,
        .cmpz           = NULL,
        .reset          = class_reset,
        .prop_getsets   = NULL,
        .hash           = NULL,
        .iter_next      = NULL,
        .get_iter       = NULL,
};

struct type_t InstanceType = {
        .flags          = 0,
        .name           = "instance",
        .opm            = NULL,
        .cbm            = NULL,
        .mpm            = NULL,
        .sqm            = NULL,
        .size           = sizeof(struct instance_t),
        .str            = instance_str,
        .cmp            = NULL,
        .cmpz           = NULL,
        .reset          = instance_reset,
        .prop_getsets   = NULL,
        .hash           = NULL,
        .iter_next      = NULL,
        .get_iter       = NULL,
};

