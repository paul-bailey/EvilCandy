/*
 * Implementation of TypeType and InstanceType objects.
 * MethodType objects are in method.c
 *
 * Called "class.c" because there used to be a ClassType object
 * specifically for user-defined types, but I got rid of it.
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

enum {
        INST_FLAG_GETATTR_LOCK = 0x01,
};

struct instance_t {
        Object obj_head;
        Object *inst_attr;
        unsigned int inst_flags;
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
        if (!class->priv)
                return true;
        if (set_hasitem(class->priv, key)) {
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
 * TODO: Delete above and use this for beginning of getattr() as well
 * as setattr().
 */
static bool
item_write_access_permitted(Frame *fr, struct type_t *class, Object *key)
{
        if (!class->all_priv)
                return true;
        if (set_hasitem(class->all_priv, key)) {
                Object *inst;

                if (!fr || !class->priv || !set_hasitem(class->priv, key))
                        return false;

                inst = vm_get_this(fr);
                if (!inst || !isvar_instance(inst))
                        return false;
                return inst->v_type == class;
        }
        return true;
}

/* get item out of just this class's methods dict */
static Object *
type_getitem_shallow(Frame *fr, Object *type, Object *key)
{
        struct type_t *tp;

        tp = V2TP(type);
        if (!tp->methods)
                return NULL;
        if (!item_access_permitted(fr, tp, key))
                return NULL;
        return dict_getitem(tp->methods, key);
}

/* fr == NULL means "we definitely do not have private access" */
static Object *
type_getitem(Frame *fr, Object *type, Object *key)
{
        struct type_t *tp;
        size_t i, n;
        Object *ret;

        tp = V2TP(type);
        bug_on(!tp->methods);

        ret = type_getitem_shallow(fr, type, key);
        if (ret)
                return ret;

        if (!tp->mro)
                return NULL;

        n = seqvar_size(tp->mro);
        for (i = 0; i < n; i++) {
                /*
                 * XXX REVISIT: Setting fr -> NULL to forbid access to
                 * base class's private data, but is that what we want?
                 */
                Object *base = tuple_borrowitem_(tp->mro, i);
                if ((ret = type_getitem_shallow(NULL, base, key)) != NULL)
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

        x = inst->inst_attr;
        inst->inst_attr = NULL;
        if (x)
                VAR_DECR_REF(x);
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

static bool
mro_candidate_appears_in_tail(Object *candidate, Object *sequences)
{
        size_t i, n;
        n = seqvar_size(sequences);
        for (i = 0; i < n; i++) {
                Object *mro = array_borrowitem(sequences, i);
                bug_on(!isvar_array(mro));
                if (array_indexof_from(mro, candidate, 1) >= 1)
                        return true;
        }
        return false;
}

static void
mro_pop_candidate(Object *candidate, Object *sequences)
{
        /*
         * XXX low-level knowledge of var_match() behavior for
         * struct type_t: for classes, "==" and "===" are the same.
         */
        size_t i, n;
        n = seqvar_size(sequences);
        for (i = 0; i < n; i++) {
                Object *mro, *item;
                mro = array_borrowitem(sequences, i);
                bug_on(!isvar_array(mro));
                if (seqvar_size(mro) == 0)
                        continue;
                item = array_borrowitem(mro, 0);
                if (item != candidate)
                        continue;
                array_delete_chunk(mro, 0, 1);
        }
}

/* sequences is an array of arrays of classes */
static Object *
mro_merge(Object *sequences)
{
        Object *ret = arrayvar_new(0);
        bool empty;

        bug_on(!isvar_array(sequences));
        do {
                empty = true;
                size_t i, n = seqvar_size(sequences);
                for (i = 0; i < n; i++) {
                        Object *candidate, *seq;

                        seq = array_borrowitem(sequences, i);
                        bug_on(!isvar_array(seq));
                        if (seqvar_size(seq) == 0)
                                continue;

                        empty = false;
                        candidate = array_borrowitem(seq, 0);
                        bug_on(!isvar_type(candidate));
                        if (!mro_candidate_appears_in_tail(candidate,
                                                           sequences)) {
                                array_append(ret, candidate);
                                mro_pop_candidate(candidate, sequences);
                                break;
                        }
                }

                /*
                 * FIXME: during early init, this is a BUG, not an
                 * "error".  But that early, we have not initialized
                 * ErrorVar to the correct type, so the bug trap will
                 * be in err.c, and it will be much more opaque.
                 */
                if (!empty && i == n) {
                        err_setstr(TypeError,
                                   "inconsistent hierarchy (no valid MRO)");
                        VAR_DECR_REF(ret);
                        return ErrorVar;
                }
        } while (!empty);
        return ret;
}

static Object *
compute_mro(Object *class)
{
        Object *base_mros, *merged, *ret;
        size_t i, n;
        struct type_t *tp;

        tp = V2TP(class);

        if (!tp->bases)
                return arrayvar_from_stack(&class, 1, false);

        base_mros = arrayvar_new(0);
        n = seqvar_size(tp->bases);
        for (i = 0; i < n; i++) {
                Object *base, *mro;

                base = tuple_borrowitem_(tp->bases, i);
                if (V2TP(base)->mro) {
                        /* use array, not tuple */
                        Object *tup = V2TP(base)->mro;
                        mro = arrayvar_from_stack(tuple_get_data(tup),
                                                  seqvar_size(tup), false);
                } else {
                        mro = compute_mro(base);
                        if (mro == ErrorVar)
                                goto err_free_base_mros;
                }
                array_append(base_mros, mro);
                VAR_DECR_REF(mro);
        }

        {
                /* Need to convert to an array before appending */
                Object *base_arr = arrayvar_from_stack(
                                        tuple_get_data(tp->bases),
                                        seqvar_size(tp->bases), false);
                array_append(base_mros, base_arr);
                VAR_DECR_REF(base_arr);
        }

        merged = mro_merge(base_mros);
        if (merged == ErrorVar)
                goto err_free_base_mros;

        ret = arrayvar_new(0);
        array_append(ret, class);
        array_extend(ret, merged);
        VAR_DECR_REF(merged);
        VAR_DECR_REF(base_mros);
        return ret;

err_free_base_mros:
        VAR_DECR_REF(base_mros);
        return ErrorVar;
}

/*
 * bases_tup will be set to NULL if bases is NULL, a new reference
 * of bases if bases is a tuple, or a tuple made from bases if
 * bases is a single TypeType object.
 *
 * Return: new mro, NULL if no bases, or ErrorVar
 */
static Object *
type_init_mro(Object *class, Object *bases)
{
        size_t i, n;
        Object *array, *tup;
        if (!bases)
                return NULL;

        if (isvar_tuple(bases)) {
                VAR_INCR_REF(bases);
        } else {
                Object *stack[1] = { bases };
                bases = tuplevar_from_stack(stack, 1, false);
        }

        n = seqvar_size(bases);
        for (i = 0; i < n; i++) {
                Object *item = tuple_borrowitem(bases, i);
                if (!isvar_type(item)) {
                        err_setstr(TypeError, "Malformed base class");
                        VAR_DECR_REF(bases);
                        return ErrorVar;
                }

                if (!!(V2TP(item)->flags & OBF_INTERNAL)) {
                        /*
                         * We need to permit built-in inheritance for
                         * ErrorVar.  This is safe, however, because it
                         * is invisible to users.  (We could only reach
                         * the below continue in the case of early-init
                         * for our exceptions--see global.c.)
                         */
                        if (item == ErrorVar)
                                continue;
                        err_setstr(NotImplementedError,
                                   "Inheritance of built-in types not yet supported");
                        VAR_DECR_REF(bases);
                        return ErrorVar;
                }
        }

        V2TP(class)->bases = bases;

        if ((array = compute_mro(class)) == ErrorVar) {
                V2TP(class)->bases = NULL;
                VAR_DECR_REF(bases);
                return ErrorVar;
        }

        /* compute_mro by necessity leaves self at front */
        if (array_borrowitem(array, 0) == class) {
                array_delete_chunk(array, 0, 1);
                if (seqvar_size(array) == 0) {
                        /* ??? */
                        VAR_DECR_REF(array);
                        return NULL;
                }
        }

#ifndef NDEBUG
        /* sanity check: mro should all be classes now, not arrays */
        n = seqvar_size(array);
        for (i = 0; i < n; i++) {
                Object *item = array_borrowitem(array, i);
                bug_on(!isvar_type(item));
        }
#endif /* NDEBUG */

        tup = tuplevar_from_stack(array_get_data(array),
                                  seqvar_size(array), false);
        VAR_DECR_REF(array);
        return tup;
}

struct init_private_t {
        Object *seen_set;
        Object *priv_set;
};

/* var_traverse callback for type_init_private() */
static enum result_t
type_add_to_seen(Object *item, void *data)
{
        struct init_private_t *ip = (struct init_private_t *)data;
        if (!set_hasitem(ip->seen_set, item)) {
                if (set_additem(ip->priv_set, item, NULL) == RES_ERROR)
                        return RES_ERROR;
        }
        return set_additem(ip->seen_set, item, NULL);
}

/*
 * tp->methods and tp->mro must already be configured.
 * This initializes tp->priv and tp->all_priv
 */
static enum result_t
type_init_private(Object *class, Object *priv_tup)
{
        struct type_t *tp;
        size_t i;
        Object *priv_set, *seen_set;
        bool any_priv;

        tp = V2TP(class);
        if (!tp->mro) {
                /* most cases: no inheritance */
                tp->priv = priv_tup ? setvar_new(priv_tup) : NULL;
                tp->all_priv = tp->priv ? VAR_NEW_REF(tp->priv) : NULL;
                return RES_OK;
        }

        /*
         * Pseudo sketch of the following algo:
         *
         * seen = empty set
         * effective_private = empty set
         *
         * for cls in [tp] + tp.mro:
         *     for name in cls.private:
         *         if name not in seen:
         *             add name to seen
         *             add name to effective_private
         *
         *     for name in keys(cls.methods):
         *         if name not in seen:
         *             add name to seen
         *
         * The [tp] part of [tp] + tp.mro is done outside of the
         * for loop, but it's otherwise the same.
         */
        any_priv = false;
        if (priv_tup) {
                any_priv = true;
                tp->priv = setvar_new(priv_tup);
                priv_set = setvar_new(priv_tup);
        } else {
                tp->priv = NULL;
                priv_set = setvar_new(NULL);
        }
        seen_set = setvar_new(tp->methods);

        for (i = 0; i < seqvar_size(tp->mro); i++) {
                struct type_t *tpbase;

                tpbase = V2TP(tuple_borrowitem_(tp->mro, i));
                if (tpbase->priv) {
                        any_priv = true;
                        struct init_private_t ip = {
                                .seen_set = seen_set,
                                .priv_set = priv_set,
                        };
                        if (var_traverse(tpbase->priv, type_add_to_seen,
                                         (void *)&ip, NULL) == RES_ERROR) {
                                VAR_DECR_REF(priv_set);
                                VAR_DECR_REF(seen_set);
                                return RES_ERROR;
                        }
                }
                set_extend(seen_set, tpbase->methods);
        }

        /* no longer needed */
        VAR_DECR_REF(seen_set);
        if (!any_priv) {
                VAR_DECR_REF(priv_set);
                priv_set = NULL;
        }
        tp->all_priv = priv_set;
        return RES_OK;
}


static enum result_t
type_validate_new_attr(Object *name, void *unused)
{
        /*
         * TODO: This should probably be a registry somewhere,
         * so I don't end up with a lot of DRY violations.
         */
        static const char *valid_dunders[] = {
                "__init__",
                "__str__",
                NULL,
        };
        const char **dunder, *s;
        size_t n;

        (void)unused;
        if (!isvar_string(name))
                return RES_OK;

        s = string_cstring(name);
        n = string_nbytes(name);
        if (n < 4 || s[0] != '_' || s[1] != '_'
            || s[n-1] != '_' || s[n-2] != '_') {
                return RES_OK;
        }

        /* FIXME: what kind of exception to set? type, name, or key? */

        /* Attribute is a dunder.  Only permit certain kinds */
        if (strlen(s) != n) {
                err_setstr(TypeError, "embedded nullchar in dunder");
                return RES_ERROR;
        }
        for (dunder = valid_dunders; *dunder != NULL; dunder++) {
                if (!strcmp(s, *dunder))
                        return RES_OK;
        }
        err_setstr(TypeError, "unrecodnized dunder %s", s);
        return RES_ERROR;
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

        x = tp->all_priv;
        tp->all_priv = NULL;
        if (x)
                VAR_DECR_REF(x);

        x = tp->methods;
        tp->methods = NULL;
        if (x)
                VAR_DECR_REF(x);

        x = tp->mro;
        tp->mro = NULL;
        if (x)
                VAR_DECR_REF(x);

        x = tp->delegate_name;
        tp->delegate_name = NULL;
        if (x)
                VAR_DECR_REF(x);

        if (tp->name)
                efree((char *)tp->name);
        tp->name = NULL;
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

        if (!!(inst->inst_flags & INST_FLAG_GETATTR_LOCK))
                return NULL;
        inst->inst_flags |= INST_FLAG_GETATTR_LOCK;

        if (!item_access_permitted(fr, instance->v_type, key))
                goto notfound;

        ret = dict_getitem(inst->inst_attr, key);
        if (ret)
                goto found;
        ret = type_getitem(fr, (Object *)(instance->v_type), key);
        if (ret)
                goto found;

        if (instance->v_type->delegate_name && isvar_instance(instance)) {
                Object *delegate = dict_getitem(
                                        V2INST(instance)->inst_attr,
                                        instance->v_type->delegate_name);
                if (delegate) {
                        ret = NULL;
                        if (delegate != instance)
                                ret = var_getattr_or_null(fr, delegate, key);
                        VAR_DECR_REF(delegate);
                        if (ret)
                                goto found;
                }
        }
notfound:
        inst->inst_flags &= ~INST_FLAG_GETATTR_LOCK;
        return NULL;

found:
        inst->inst_flags &= ~INST_FLAG_GETATTR_LOCK;
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
        Object *dict;

        if (!item_write_access_permitted(fr, instance->v_type, key))
                return RES_ERROR;

        dict = V2INST(instance)->inst_attr;
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

/*
 * XXX: So close to making this be local and static, but namespace.c
 * needs it, to prevent calling an __init__ "method"
 */

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
 * type_get_builtin_attr - Get an attribute from a type methods dictionary
 * @tp: Type
 * @obj: Owner to get an attribute from
 * @key: Key to the attribute
 *
 * Return: NULL if not found.  Otherwise return the attribute.  If it is
 *         a function and @tp is configured to bind its methods, then
 *         a MethodType wrapper will be returned instead of the function.
 *
 * This function will not raise an exception if @key is not found.
 */
Object *
type_get_builtin_attr(struct type_t *tp, Object *obj, Object *key)
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

/**
 * Whatever @type is, instantiate a new one of it
 * @args: Arguments to .__init__() (if user) or .create() (if built-in)
 * @kwargs: Keyword argumens to the instantiation function
 *
 * Return: New instantiated object or ErrorVar
 */
Object *
type_instantiate_object(Object *type, Object *args, Object *kwargs)
{
        struct type_t *tp = (struct type_t *)type;
        if (tp->create) {
                Object *res, *func;
                /*
                 * FIXME: Creating and destroying a function object during
                 * a function call is non-trivial.  We should probably
                 * have an additional field in struct type_t that's just
                 * .create() turned into a UAPI function.
                 */
                func = funcvar_new_intl(tp->create, false);
                res = vm_exec_func(NULL, func, args, kwargs);
                VAR_DECR_REF(func);
                return res;

        }
        if (!(tp->flags & OBF_HEAP)) {
                /* XXX: This may be doable in the future */
                err_setstr(TypeError, "object is not callable");
                return ErrorVar;
        }
        return instancevar_new(type, args, kwargs, true);
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
                /*
                 * TODO: Intern this name. If called, it will likely be
                 * with an interned string literal.
                 */
                Object *k = stringvar_new(tp->name);
                vm_add_global(k, (Object *)tp);
                VAR_DECR_REF(k);
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
 * @delegate_name:
 *              A string naming which field is used for delegation.
 *              It will only apply to *this class*, not any of its base
 *              classes.  NULL if no delegate.
 *
 * Return:      A new class.  Caller may need to adjust flags on the
 *              return Value as needed (such as whether to bind functions
 *              during a call to get an attribute).
 */
Object *
typevar_new_user(Object *bases, Object *dict,
                 Object *name, Object *priv_tup,
                 Object *delegate_name)
{
        Object *ret, *mro;
        struct type_t *tp;

        ret = var_new(&TypeType);
        tp = V2TP(ret);

        /* do this early in case we have to free */
        tp->flags = OBF_HEAP | OBF_GP_INSTANCE;

        mro = type_init_mro(ret, bases);
        if (mro == ErrorVar) {
                VAR_DECR_REF(ret);
                return ErrorVar;
        }
        tp->mro = mro;

        tp->str = instance_str;
        tp->reset = instance_reset;
        tp->size = sizeof(struct instance_t);

        if (delegate_name) {
                bug_on(!isvar_string(delegate_name));
                VAR_INCR_REF(delegate_name);
        }
        tp->delegate_name = delegate_name;

        if (dict) {
                enum result_t result;
                /*
                 * TODO: Use dict_items(), not this, so I can also verify
                 * proper type for a given dunder.
                 */
                result = var_traverse(dict, type_validate_new_attr, NULL,
                                      name && isvar_string(name)
                                        ? string_cstring(name) : NULL);
                if (result == RES_ERROR) {
                        VAR_DECR_REF(ret);
                        return ErrorVar;
                }

                VAR_INCR_REF(dict);
        } else {
                dict = dictvar_new();
        }
        tp->methods = dict;

        if (type_init_private(ret, priv_tup) == RES_ERROR) {
                VAR_DECR_REF(ret);
                return ErrorVar;
        }

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
        Object *ret = typevar_new_user(bases, dict, name, NULL, NULL);
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

