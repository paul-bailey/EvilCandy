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
        ret = dict_getitem(cls->c_dict, key);
        if (ret)
                return ret;

        n = seqvar_size(cls->c_bases);
        for (i = 0; i < n; i++) {
                Object *item = tuple_borrowitem(cls->c_bases, i);
                ret = class_getitem(item, key);
                if (ret)
                        return ret;
        }
        return NULL;
}

static int
class_hasitem(Object *class, Object *key)
{
        struct class_t *cls;
        size_t i, n;

        cls = V2CL(class);
        if (var_hasattr(cls->c_dict, key))
                return true;
        n = seqvar_size(cls->c_bases);
        for (i = 0; i < n; i++) {
                Object *item = tuple_borrowitem(cls->c_bases, i);
                if (class_hasitem(item, key))
                        return true;
        }
        return false;
}

static void
instance_reset(Object *instance)
{
        Object *x;

        x = V2INST(instance)->inst_attr;
        if (x)
                VAR_DECR_REF(x);
        x = V2INST(instance)->inst_class;
        if (x)
                VAR_DECR_REF(x);
}

static Object *
instance_getitem(Object *instance, Object *key)
{
        Object *ret;
        struct instance_t *inst;

        inst = V2INST(instance);
        bug_on(!isvar_instance(instance));

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
                ret = property_get(ret, instance);
                VAR_DECR_REF(tmp);
        }
        return ret;
}

static int
instance_hasitem(Object *instance, Object *key)
{
        struct instance_t *inst = V2INST(instance);
        bug_on(!isvar_instance(instance));
        if (var_hasattr(inst->inst_attr, key))
                return 1;
        return class_hasitem(inst->inst_class, key);
}

static enum result_t
instance_setitem(Object *instance, Object *key, Object *item)
{
        /* FIXME: what if we're setting a property? */
        bug_on(!isvar_instance(instance));
        return dict_setitem(V2INST(instance)->inst_attr, key, item);
}

static const struct map_methods_t instance_mpm = {
        .getitem = instance_getitem,
        .setitem = instance_setitem,
        .hasitem = instance_hasitem,
        .mpunion = NULL,
};

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

        bug_on(bases && !isvar_tuple(bases));
        bug_on(!dict || !isvar_dict(dict));

        size = seqvar_size(dict);
        if (bases) {
                size_t i, n;
                n = seqvar_size(bases);
                for (i = 0; i < n; i++) {
                        Object *obj = tuple_borrowitem(bases, i);
                        if (!isvar_dict(obj)) {
                                err_setstr(TypeError,
                                        "base class cannot be type '%s'",
                                        typestr(obj));
                                return ErrorVar;
                        }
                        size += seqvar_size(obj);
                }

                VAR_INCR_REF(bases);
        }

        ret = var_new(&ClassType);
        class = V2CL(ret);
        class->c_bases = bases;
        class->c_dict = VAR_NEW_REF(dict);
        seqvar_set_size(ret, size);
        return ret;
}

Object *
instancevar_new(Object *class)
{
        Object *ret = var_new(&InstanceType);

        bug_on(!isvar_class(class));
        V2INST(ret)->inst_class = VAR_NEW_REF(class);
        V2INST(ret)->inst_attr = dictvar_new();
        /* TODO: if class has __init__, call it */
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
        .mpm            = &instance_mpm,
        .sqm            = NULL,
        .size           = sizeof(struct instance_t),
        .str            = NULL,
        .cmp            = NULL,
        .cmpz           = NULL,
        .reset          = instance_reset,
        .prop_getsets   = NULL,
        .hash           = NULL,
        .iter_next      = NULL,
        .get_iter       = NULL,
};

