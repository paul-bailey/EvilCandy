/*
 * Implementation of ClassType and InstanceType objects.
 * MethodType objects are in method.c
 */
#include <evilcandy.h>
#include <internal/type_registry.h>
#include <internal/types/string.h>
#include <internal/types/sequential_types.h>
#include <var.h>

struct class_t {
        struct seqvar_t obj_head;
        Object *c_bases;
        Object *c_dict;
        Object *c_priv;
        Object *c_name;
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
        V2CL(class)->c_bases = NULL;
        if (x)
                VAR_DECR_REF(x);

        x = V2CL(class)->c_dict;
        V2CL(class)->c_dict = NULL;
        if (x)
                VAR_DECR_REF(x);

        x = V2CL(class)->c_name;
        V2CL(class)->c_name = NULL;
        if (x)
                VAR_DECR_REF(x);

        x = V2CL(class)->c_priv;
        V2CL(class)->c_priv = NULL;
        if (x)
                VAR_DECR_REF(x);
}

static bool
item_access_permitted(Frame *fr, Object *class, Object *key)
{
        if (!V2CL(class)->c_priv)
                return true;
        if (set_hasitem(V2CL(class)->c_priv, key)) {
                Object *inst;
                if (!fr)
                        return false;
                inst = vm_get_this(fr);
                if (!isvar_instance(inst)) /*< XXX bug? */
                        return false;
                return V2INST(inst)->inst_class == class;
        }
        return true;
}

/*
 * FIXME: This has a multiple-inheritance diamond problem.  Change this
 * so that cls->c_dict is filled in with methods at classvar_new() time,
 * and use a better method to resolve which methods are used from its
 * base classes.
 *
 * fr == NULL means "we definitely do not have private access"
 */
static Object *
class_getitem(Frame *fr, Object *class, Object *key)
{
        struct class_t *cls;
        size_t i, n;
        Object *ret;

        cls = V2CL(class);
        bug_on(!cls->c_dict);

        /*
         * TODO: replace c_priv and c_dict with a single map,
         * so we don't have to do a double lookup.
         */
        if (!item_access_permitted(fr, class, key))
                return NULL;

        ret = dict_getitem(cls->c_dict, key);
        if (ret)
                return ret;

        if (!cls->c_bases)
                return NULL;

        n = seqvar_size(cls->c_bases);
        for (i = 0; i < n; i++) {
                Object *item = tuple_borrowitem_(cls->c_bases, i);
                ret = class_getitem(fr, item, key);
                if (ret)
                        return ret;
        }
        return NULL;
}

/* This produces a reference */
Object *
class_get_name(Object *class)
{
        return VAR_NEW_REF(V2CL(class)->c_name);
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
                Object *obj = tuple_borrowitem_(bases, i);
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
        Object *name_obj, *ret;
        const char *name;

        ret = instance_call(instance, STRCONST_ID(__str__), NULL, NULL);
        if (ret && ret != ErrorVar)
                return ret;

        err_clear();
        name_obj = V2CL(V2INST(instance)->inst_class)->c_name;
        if (name_obj) {
                name = string_cstring(name_obj);
        } else {
                name = "<anonymous class>";
        }

        return stringvar_from_format("<instance of %s at %p>",
                                     name, instance);
}

/**
 * instance_getattr - Get an instance attribute
 * @fr: Frame, so we can tell if we have permission, should @key be for
 *      a private attribute.  If @fr is NULL, then only try to access
 *      public attributes.
 * @instance: Instance to get attribute from
 * @key: Key to the attribute
 *
 * Return: attribute if found, or NULL.
 */
Object *
instance_getattr(Frame *fr, Object *instance, Object *key)
{
        Object *ret;
        struct instance_t *inst;

        inst = V2INST(instance);
        bug_on(!isvar_instance(instance));
        bug_on(!inst->inst_attr);
        bug_on(!inst->inst_class);

        if (!item_access_permitted(fr, inst->inst_class, key))
                return NULL;

        ret = dict_getitem(inst->inst_attr, key);
        if (ret)
                goto found;
        ret = class_getitem(fr, inst->inst_class, key);
        if (ret)
                goto found;
        return NULL;

found:
        if (isvar_function(ret)) {
                Object *tmp = ret;
                ret = methodvar_new(tmp, instance);
                VAR_DECR_REF(tmp);
        }
        return ret;
}

/**
 * instance_setattr - Set an instance attribute
 * @fr: Frame, with same purpose and meaning as @fr arg to instance_getattr()
 * @instance: Instance to set attribute in
 * @key: Key to the attribute
 * @value: Value to set attribute to.
 *
 * Return: RES_OK if success, RES_ERROR if failure.  This does not raise an
 * exception.
 */
enum result_t
instance_setattr(Frame *fr, Object *instance, Object *key, Object *value)
{
        if (!item_access_permitted(fr, V2INST(instance)->inst_class, key))
                return RES_ERROR;

        Object *dict = V2INST(instance)->inst_attr;
        return dict_setitem(dict, key, value);
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
        /*
         * FIXME: Need a Frame arg; as-is, this will fail if method_name
         * is private, even if we technically have permission.
         */
        Object *ret;
        Object *method = instance_getattr(NULL, instance, method_name);
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
 * instance_super_getattr - Get an attribute from the base class of an
 *                          instance.
 * @instance: Instance to get the super from
 * @attribute_name: Name of the attribute
 *
 * Return: super's attribute or NULL if either no super or super does not
 *         contain attribute.  No exception will be thrown if super not
 *         found.
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
                Object *super = tuple_borrowitem_(bases, i);
                Object *attr = class_getitem(NULL, super, attribute_name);
                if (attr)
                        return attr;
        }

        return NULL;
}

/*
 * FIXME: This is naive.  I want the resolved method and its
 * public/private status to match.  So if a higher-precedence method
 * is public and a lower-precedence method with the same name is private,
 * the resolved public/private status should be public, not private.  But
 * this algorithm below will make it private for all methods of a certain
 * name.  The solution should be to resolve c_priv and c_dict together
 * in the same algorithm.
 */
static Object *
inherit_private_names(Object *bases)
{
        Object *priv_set = NULL;
        size_t i, n = seqvar_size(bases);

        for (i = 0; i < n; i++) {
                Object *base = tuple_borrowitem_(bases, i);
                Object *base_priv = V2CL(base)->c_priv;
                if (!base_priv)
                        continue;

                if (!priv_set)
                        priv_set = setvar_new(NULL);
                /*
                 * FIXME: Clobbering an error! Change policy on
                 * classvar_new() such that its return value could be
                 * ErrorVar.
                 */
                if (set_extend(priv_set, base_priv) == RES_ERROR)
                        err_clear();
        }
        return priv_set;
}

/**
 * classvar_new - Create a class
 * @bases: A tuple containing inherited base classes
 * @dict: Dictionary of methods, properties, and data
 * @name: NULL, NullVar, or a string object that names the class.
 * @priv_tup: NULL or a tuple of names of private attributes.
 */
Object *
classvar_new(Object *bases, Object *dict, Object *name, Object *priv_tup)
{
        Object *ret;
        struct class_t *class;
        size_t size;
        Object *priv_set;

        bug_on(!dict || !isvar_dict(dict));
        bug_on(priv_tup && !isvar_tuple(priv_tup));
        bug_on(name && name != NullVar && !isvar_string(name));

        size = seqvar_size(dict);
        priv_set = NULL;
        if (bases) {
                bases = verify_base_classes(bases, &size);
                if (bases == ErrorVar)
                        return bases;
                priv_set = inherit_private_names(bases);
        }

        if (priv_tup) {
                if (!priv_set)
                        priv_set = setvar_new(priv_tup);
                else
                        set_extend(priv_set, priv_tup);
        }

        if (name == NullVar)
                name = NULL;

        ret = var_new(&ClassType);
        class = V2CL(ret);
        class->c_priv = priv_set;
        class->c_bases = bases;
        class->c_dict = VAR_NEW_REF(dict);
        class->c_name = name ? VAR_NEW_REF(name) : NULL;
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

        /*
         * TODO: instance_setattr() never writes to the class dict, so
         * there's no worry about different instances mangling each
         * other's data as expressed in the class literal.  But the
         * bigger problem is, we need to resolve methods, and here is
         * the place to do it.
         */

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

        inst = V2INST(instance);
        class = V2CL(inst->inst_class);

        set = setvar_new(inst->inst_attr);
        set_extend(set, class->c_dict);

        if (class->c_bases) {
                size_t i, n;
                Object *base;

                n = seqvar_size(class->c_bases);
                for (i = 0; i < n; i++) {
                        base = tuple_borrowitem_(class->c_bases, i);
                        set_extend(set, V2CL(base)->c_dict);
                }
        }
        ret = arrayvar_new(0);
        array_extend(ret, set);
        var_sort(ret);

        VAR_DECR_REF(set);
        return ret;
}

/**
 * instance_instanceof - Return true if @class is a class or base class
 *                       of @instance.
 */
bool
instance_instanceof(Object *instance, Object *class)
{
        bug_on(!isvar_instance(instance) || !isvar_class(class));
        return class_issubclass(V2INST(instance)->inst_class, class);
}

/**
 * class_issubclass - Return true if @class is @base or a subclass of
 *                    @base
 */
bool
class_issubclass(Object *class, Object *base)
{
        size_t i;
        Object *bases;

        if (class == base)
                return true;

        bases = V2CL(class)->c_bases;
        if (!bases)
                return false;
        for (i = 0; i < seqvar_size(bases); i++) {
                if (class_issubclass(tuple_borrowitem_(bases, i), base))
                        return true;
        }
        return false;
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
        .cmpeq          = NULL,
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
        .cmpeq          = NULL,
        .cmpz           = NULL,
        .reset          = instance_reset,
        .prop_getsets   = NULL,
        .hash           = NULL,
        .iter_next      = NULL,
        .get_iter       = NULL,
};

