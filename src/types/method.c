/*
 * method.c - Wrapper for a function.  See var_getattr
 *            how these are used.
 */
#include <evilcandy.h>

struct methodvar_t {
        Object base;
        Object *func;
        Object *owner; /* @func's "this" */
};

#define V2M(v) ((struct methodvar_t *)(v))

/**
 * methodvar_tofunc - Get function/owner pair
 * @meth: Method object
 * @func: Pointer to store function, may not be NULL
 * @owner: Pointer to store owner, may not be NULL
 *
 * Return: RES_OK, if args are correct, RES_ERROR otherwise.
 *
 * These are not borrowed; references will be produced by this
 * call.
 */
int
methodvar_tofunc(Object *meth, Object **func, Object **owner)
{
        struct methodvar_t *m = V2M(meth);
        if (!isvar_method(meth))
                return RES_ERROR;

        VAR_INCR_REF(m->func);
        VAR_INCR_REF(m->owner);
        *func = m->func;
        *owner = m->owner;
        return RES_OK;
}

/**
 * methodvar_new - Create a new method object
 * @func: The actual method
 * @owner: The owner of the method
 */
Object *
methodvar_new(Object *func, Object *owner)
{
        Object *ret = var_new(&MethodType);
        struct methodvar_t *m = (struct methodvar_t *)ret;

        bug_on(!func || !isvar_function(func));
        bug_on(!owner);

        m->func = func;
        m->owner = owner;
        VAR_INCR_REF(func);
        VAR_INCR_REF(owner);
        return ret;
}

static Object *
method_str(Object *meth)
{
        char buf[72];
        struct methodvar_t *m = V2M(meth);
        bug_on(!isvar_method(meth));

        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf)-1,
                 "<function owned by %llu>",
                 (unsigned long long)m->owner);
        return stringvar_new(buf);
}

static int
method_cmp(Object *a, Object *b)
{
        return OP_CMP((uintptr_t)a, (uintptr_t)b);
}

static bool
method_cmpz(Object *meth)
{
        return meth == NULL;
}

static void
method_reset(Object *meth)
{
        struct methodvar_t *m = V2M(meth);
        bug_on(!m->func || !m->owner);
        VAR_DECR_REF(m->func);
        VAR_DECR_REF(m->owner);
}

struct type_t MethodType = {
        .name   = "method",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct methodvar_t),
        .str    = method_str,
        .cmp    = method_cmp,
        .cmpz   = method_cmpz,
        .reset  = method_reset,
};
