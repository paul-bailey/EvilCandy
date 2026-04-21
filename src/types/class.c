/*
 * Implementation of ClassType and InstanceType objects.
 * MethodType objects are in method.c
 */
#include <evilcandy/vm.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/var.h>
#include <evilcandy/global.h>
#include <evilcandy/err.h>
#include <evilcandy/types/array.h>
#include <evilcandy/types/class.h>
#include <evilcandy/types/function.h>
#include <evilcandy/types/property.h>
#include <evilcandy/types/dict.h>
#include <evilcandy/types/string.h>
#include <evilcandy/types/method.h>
#include <evilcandy/types/set.h>
#include <evilcandy/types/tuple.h>
#include <internal/type_registry.h>
#include <internal/type_registry.h>
#include <internal/types/string.h>
#include <internal/types/sequential_types.h>

struct instance_t {
        Object obj_head;
        Object *inst_attr;
        void *inst_priv;
        void (*inst_cleanup)(void *);
};

#define V2TP(obj_)        ((struct type_t *)(obj_))
#define V2INST(obj_)      ((struct instance_t *)(obj_))

static Object *
maybe_bind_function(Object *instance, Object *maybe_function)
{
        if (isvar_function(maybe_function) &&
            !(instance->v_type->flags & OBF_NO_BIND_FUNCTION_ATTRS)) {
                Object *tmp = maybe_function;
                maybe_function = methodvar_new(tmp, instance);
                VAR_DECR_REF(tmp);
        }
        return maybe_function;
}

static bool
item_access_permitted(Frame *fr, struct type_t *class, Object *key)
{
        if (!V2TP(class)->priv)
                return true;
        if (set_hasitem(V2TP(class)->priv, key)) {
                Object *inst;
                if (!fr)
                        return false;
                inst = vm_get_this(fr);
                if (!inst || !isvar_instance(inst)) /*< XXX bug? */
                        return false;
                return inst->v_type == class;
        }
        return true;
}

/*
 * FIXME: This has a multiple-inheritance diamond problem.  Change this
 * so that cls->c_dict is filled in with methods at typevar_new() time,
 * and use a better method to resolve which methods are used from its
 * base classes.
 *
 * fr == NULL means "we definitely do not have private access"
 */
static Object *
type_getitem(Frame *fr, Object *type, Object *key)
{
        struct type_t *tp;
        size_t i, n;
        Object *ret;

        tp = V2TP(type);
        bug_on(!tp->methods);

        /*
         * TODO: replace .priv and .methods with a single map,
         * so we don't have to do a double lookup.
         */
        if (!item_access_permitted(fr, V2TP(type), key))
                return NULL;

        ret = dict_getitem(tp->methods, key);
        if (ret)
                return ret;

        if (!tp->bases)
                return NULL;

        n = seqvar_size(tp->bases);
        for (i = 0; i < n; i++) {
                Object *item = tuple_borrowitem_(tp->bases, i);
                ret = type_getitem(fr, item, key);
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
instance_str(Object *instance)
{
        Object *ret;
        const char *name;

        ret = instance_call(instance, STRCONST_ID(__str__), NULL, NULL);
        if (ret && ret != ErrorVar)
                return ret;

        err_clear();
        name = instance->v_type->name;
        return stringvar_from_format("<instance of %s at %p>",
                                     name, instance);
}

/*
 * NOTE: These look like egregious DRY violations (because they are),
 * but the struct class_t and class_xxx methods above are being phased
 * out.
 */
static Object *
type_verify_base_classes(Object *bases)
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
                unsigned flags;
                if (!isvar_type(obj)) {
                        err_setstr(TypeError,
                                   "base class may not be type '%s'",
                                   typestr(obj));
                        goto err;
                }

                flags = ((struct type_t *)obj)->flags;
                /*
                 * See gh issue #52.  Until then, forbid inheritance of
                 * built-in classes.
                 *
                 * XXX REVISIT: This works with the ErrorVar exception,
                 * but it's a little confusing, and in terms of future
                 * maintainability, a little dangerous.  Unlike most of
                 * our other built-in types, "ErrorVar" is not a
                 * statically-allocated TypeType object.  It is created
                 * through typevar_new_intl() (see global.h).  We just
                 * happen to know that it has no built-in methods which
                 * would require a private-data struct, so this is safe
                 * until we change our minds about that.  This assumes
                 * that we will finish gh issue #52 (enable inheritance
                 * of built-in classes) before then.
                 */
                if (obj != ErrorVar && !!(flags & OBF_INTERNAL)) {
                        err_setstr(NotImplementedError,
                                   "inheritance of built-in types not yet supported");
                        goto err;
                }
        }
        return bases;

err:
        VAR_DECR_REF(bases);
        return ErrorVar;
}

static Object *
type_inherit_private_names(Object *bases)
{
        Object *priv_set = NULL;
        size_t i, n = seqvar_size(bases);

        for (i = 0; i < n; i++) {
                Object *base = tuple_borrowitem_(bases, i);
                Object *base_priv = ((struct type_t *)base)->priv;
                if (!base_priv)
                        continue;

                if (!priv_set)
                        priv_set = setvar_new(NULL);
                /* FIXME: Clobbering an error! */
                if (set_extend(priv_set, base_priv) == RES_ERROR)
                        err_clear();
        }
        return priv_set;
}

static void
type_reset(Object *type)
{
        Object *x;
        struct type_t *tp = (struct type_t *)type;
        bug_on(!isvar_type(type));

        /*
         * Consider the following a bug, because it would only trigger
         * if VAR_INCR/DECR_REF was being used improperly on a static
         * type.
         */
        bug_on(!(tp->flags & OBF_HEAP));

        var_type_clear_freelist(tp);

        x = tp->bases;
        tp->bases = NULL;
        if (x)
                VAR_DECR_REF(x);

        x = tp->priv;
        tp->priv = NULL;
        if (x)
                VAR_DECR_REF(x);

        x = tp->methods;
        tp->methods = NULL;
        if (x)
                VAR_DECR_REF(x);

        bug_on(!tp->name);
        efree((char *)tp->name);
        tp->flags = 0;
        tp->size = 0;
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
 *
 * This does not raise an error if not found.
 */
Object *
instance_getattr(Frame *fr, Object *instance, Object *key)
{
        Object *ret;
        struct instance_t *inst;

        inst = V2INST(instance);
        bug_on(!isvar_instance(instance));
        bug_on(!inst->inst_attr);

        if (!item_access_permitted(fr, instance->v_type, key))
                return NULL;

        ret = dict_getitem(inst->inst_attr, key);
        if (ret)
                goto found;
        ret = type_getitem(fr, (Object *)(instance->v_type), key);
        if (ret)
                goto found;
        return NULL;

found:
        return maybe_bind_function(instance, ret);
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
        if (!item_access_permitted(fr, instance->v_type, key))
                return RES_ERROR;

        Object *dict = V2INST(instance)->inst_attr;
        return dict_setitem(dict, key, value);
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
        class = (Object *)(instance->v_type);
        bases = V2TP(class)->bases;
        if (!bases)
                return NULL;

        n = seqvar_size(bases);
        for (i = 0; i < n; i++) {
                Object *super = tuple_borrowitem_(bases, i);
                Object *attr = type_getitem(NULL, super, attribute_name);
                if (attr)
                        return maybe_bind_function(instance, attr);
        }

        return NULL;
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
        Object *init_result, *ret;
        struct type_t *tp = V2TP(class);

        bug_on(args && (!isvar_tuple(args) && !isvar_array(args)));
        bug_on(kwargs && !isvar_dict(kwargs));
        bug_on(!isvar_type(class));
        bug_on(!(tp->flags & OBF_HEAP));
        bug_on(tp->size != sizeof(struct instance_t));

        ret = var_new(tp);

        /*
         * Instances use their class object as v_type.  Keep that heap
         * type alive for as long as the instance exists.  The matching
         * decrement cannot run from instance_reset(), because
         * var_delete__() still needs v->v_type after reset in order to
         * return the instance storage to the type's freelist.
         * var_delete__() therefore drops this reference after var_free(v).
         */
        VAR_INCR_REF(class);

        V2INST(ret)->inst_attr = dictvar_new();

        /*
         * TODO: instance_setattr() never writes to the class dict, so
         * there's no worry about different instances mangling each
         * other's data as expressed in the class literal.  But the
         * bigger problem is, we need to resolve methods, and here is
         * the place to do it.
         */

        if (call_init) {
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
        }

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
        struct type_t *class;
        Object *set, *ret;

        inst = V2INST(instance);
        class = instance->v_type;

        set = setvar_new(inst->inst_attr);
        set_extend(set, class->methods);

        if (class->bases) {
                size_t i, n;
                Object *base;

                n = seqvar_size(class->bases);
                for (i = 0; i < n; i++) {
                        base = tuple_borrowitem_(class->bases, i);
                        set_extend(set, V2TP(base)->methods);
                }
        }
        ret = arrayvar_new(0);
        array_extend(ret, set);
        var_sort(ret);

        VAR_DECR_REF(set);
        return ret;
}

/**
 * type_get_bound_attr - Get an attribute from a type methods dictionary
 * @tp: Type
 * @obj: Owner to get an attribute from
 * @key: Key to the attribute
 *
 * Return: NULL if not found.  Otherwise return the attribute.  If it is
 *         a function and @tp is configured to bind its methods, then
 *         a MethodType wrapper will be returned instead of the function.
 *
 * This function will not raise an exception if @key is not found.
 *
 * FIXME: There's some confusion between this and local type_getitem().
 * The latter is for user-defined types and this is for built-in types.
 * This will be unified when I add an MRO at typevar_new-time.  Then only
 * this function or that one will exist.
 */
Object *
type_get_bound_attr(struct type_t *tp, Object *obj, Object *key)
{
        Object *ret = dict_getitem(tp->methods, key);
        if (!ret)
                return NULL;

        if (isvar_property(ret)) {
                Object *tmp = ret;
                ret = property_get(ret, obj, key);
                VAR_DECR_REF(tmp);
                /*
                 * Do not fall through.  A property should never be a
                 * class method.
                 */
                return ret;
        }

        return maybe_bind_function(obj, ret);
}

/**
 * type_issubclass - Return true if @type is @base or a subclass of
 *                   @base
 */
bool
type_issubclass(Object *type, Object *base)
{
        size_t i;
        Object *bases;

        if (type == base)
                return true;

        bases = V2TP(type)->bases;
        if (!bases)
                return false;
        for (i = 0; i < seqvar_size(bases); i++) {
                if (type_issubclass(tuple_borrowitem_(bases, i), base))
                        return true;
        }
        return false;
}

/*
 * TODO: Temporary, until I have a more organized scheme
 * for choosing which classes are made global.
 */
static bool
type_is_in_modules(struct type_t *tp)
{
        return tp == &BinFileType || tp == &RawFileType
               || tp == &TextFileType || tp == &SocketType;
}

/**
 * type_init_builtin - Initialize a statically allocated
 *                     built-in type.
 * @type:       Type object to initialize
 * @isheap:     True if this was dynamically allocated on the heap;
 *              false if it was declared static.
 */
void
type_init_builtin(Object *type, bool isheap)
{
        /*
         * XXX REVISIT: Some bug traps in this function assume we're
         * doing early-init stuff, where only a bug can cause the sort of
         * malformed @type that would fail `dict_setitem_exclusive`.
         * In the future, we should reconsider whether this is still true.
         */
        struct type_t *tp = V2TP(type);

        tp->methods = dictvar_new();

        Object *dict = tp->methods;
        const struct type_method_t *t = tp->cbm;
        if (t) {
                while (t->name != NULL) {
                        Object *v, *k;
                        enum result_t res;

                        v = funcvar_from_lut(t, true);
                        k = stringvar_new(t->name);
                        res = dict_setitem_exclusive(dict, k, v);
                        VAR_DECR_REF(k);
                        VAR_DECR_REF(v);

                        bug_on(res != RES_OK);
                        (void)res;

                        t++;
                }
        }

        const struct type_prop_t *p = tp->prop_getsets;
        if (p) {
                while (p->name != NULL) {
                        Object *v, *k;
                        enum result_t res;

                        v = propertyvar_new_intl(p);
                        k = stringvar_new(p->name);
                        res = dict_setitem_exclusive(dict, k, v);
                        VAR_DECR_REF(k);
                        VAR_DECR_REF(v);

                        bug_on(res != RES_OK);
                        (void)res;

                        p++;
                }
        }

        if (tp->create && !type_is_in_modules(tp)) {
                Object *v, *k;
                k = stringvar_new(tp->name);
                v = funcvar_new_intl(tp->create, false);
                vm_add_global(k, v);
                VAR_DECR_REF(k);
                VAR_DECR_REF(v);
        }

        /* Just to be sure */
        if (isheap)
                tp->flags |= OBF_HEAP;
        else
                tp->flags &= ~OBF_HEAP;
        tp->flags |= OBF_INTERNAL;
}

/**
 * typevar_new_user - Return a new user-defined type
 * @bases:      Base class, either NULL (if none), a TypeType object
 *              (if only one base), or a tuple (if multiple bases)
 * @dict:       Dictionary of class methods.  It may contain data as
 *              well.
 * @name:       Class name.  If NULL, name will be set to "<anonymous>".
 * @priv_tup:   Tuple of names of class methods/data which are private.
 *
 * Return:      A new class.  Caller may need to adjust flags on the
 *              return Value as needed (such as whether to bind functions
 *              during a call to get an attribute).
 */
Object *
typevar_new_user(Object *bases, Object *dict,
                 Object *name, Object *priv_tup)
{
        Object *ret, *priv_set;
        struct type_t *tp;

        priv_set = NULL;
        if (bases) {
                bases = type_verify_base_classes(bases);
                if (bases == ErrorVar)
                        return bases;
                priv_set = type_inherit_private_names(bases);
        }

        ret = var_new(&TypeType);
        tp = V2TP(ret);
        tp->flags = OBF_HEAP | OBF_GP_INSTANCE;
        tp->bases = bases;
        tp->priv = priv_set;

        if (priv_tup) {
                if (!priv_set)
                        priv_set = setvar_new(priv_tup);
                else
                        set_extend(priv_set, priv_tup);
        }

        tp->bases = bases;
        tp->priv = priv_set;
        tp->str = instance_str;
        tp->reset = instance_reset;
        tp->size = sizeof(struct instance_t);

        if (dict)
                VAR_INCR_REF(dict);
        else
                dict = dictvar_new();
        tp->methods = dict;

        if (name == NullVar)
                name = NULL;
        if (name)
                tp->name = estrdup(string_cstring(name));
        else
                tp->name = estrdup("<anonymous>");

        return ret;
}

/*
 * FIXME: This is patently WRONG! but some of our internal code is still
 * trying to use user-type classes with their own internal data, so until
 * I fix that, typevar_new_intl() is nearly identical to typevar_new_user().
 *
 * TODO: for above fixme, the correct prototype should be something like
 *
 *      Object *typevar_new_intl(const struct type_t *templ)
 *
 * where templ was likely declared on the stack, and whose minimum fields
 * were filled in the same way a static type is filled in, and then this
 * function would call var_new(templ), copy its meaningul fields, then
 * pass the return value through type_init_builtin() before returning it
 * to the caller.
 */
Object *
typevar_new_intl(Object *bases, Object *dict, Object *name)
{
        Object *ret = typevar_new_user(bases, dict, name, NULL);
        V2TP(ret)->flags |= OBF_INTERNAL;
        return ret;
}

struct type_t TypeType = {
        .flags          = 0,
        .name           = "type",
        .opm            = NULL,
        .cbm            = NULL,
        .mpm            = NULL,
        .sqm            = NULL,
        .size           = sizeof(struct type_t),
        .str            = NULL,
        .cmp            = NULL,
        .cmpeq          = NULL,
        .cmpz           = NULL,
        .reset          = type_reset,
        .prop_getsets   = NULL,
        .create         = NULL,
        .hash           = NULL,
        .iter_next      = NULL,
        .get_iter       = NULL,
};

