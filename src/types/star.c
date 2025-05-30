/* star.c - Wrapper for a "starred" function argument */
#include <evilcandy.h>

struct starvar_t {
        Object base;
        Object *st_elem;
};

static Object *
star_str(Object *star)
{
        char buf[72];
        bug_on(!isvar_star(star));
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf)-1,
                "<list pointer at %p>",
                ((struct starvar_t *)star)->st_elem);
        return stringvar_new(buf);
}

static int
star_cmp(Object *a, Object *b)
{
        uintptr_t la, lb;
        bug_on(!isvar_star(a) || !isvar_star(b));
        la = (uintptr_t)((struct starvar_t *)a)->st_elem;
        lb = (uintptr_t)((struct starvar_t *)b)->st_elem;
        return OP_CMP(la, lb);
}

static bool
star_cmpz(Object *star)
{
        Object *li;
        bug_on(!isvar_star(star));
        li = ((struct starvar_t *)star)->st_elem;
        bug_on(!isvar_seq(li));
        return seqvar_size(li) == 0;
}

static void
star_reset(Object *star)
{
        Object *e;
        bug_on(!isvar_star(star));
        e = ((struct starvar_t *)star)->st_elem;
        VAR_DECR_REF(e);
}

struct type_t StarType = {
        .flags  = 0,
        .name   = "star",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct starvar_t),
        .str    = star_str,
        .cmp    = star_cmp,
        .cmpz   = star_cmpz,
        .reset  = star_reset,
};

Object *
star_unpack(Object *star)
{
        Object *e;
        bug_on(!isvar_star(star));
        e = ((struct starvar_t *)star)->st_elem;
        VAR_INCR_REF(e);
        return e;
}

Object *
starvar_new(Object *x)
{
        Object *ret = var_new(&StarType);
        bug_on(!isvar_array(x));
        VAR_INCR_REF(x);
        ((struct starvar_t *)ret)->st_elem = x;
        return ret;
}

