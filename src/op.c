/* op.c - built-in methods for operators like + and - */
#include "egq.h"
#include <math.h>
#include <string.h>

struct primitive_methods_t {
        void (*mul)(struct var_t *, struct var_t *);    /* a = a * b */
        void (*div)(struct var_t *, struct var_t *);    /* a = a / b */
        void (*mod)(struct var_t *, struct var_t *);    /* a = a % b */
        void (*add)(struct var_t *, struct var_t *);    /* a = a + b */
        void (*sub)(struct var_t *, struct var_t *);    /* a = a - b */

        /* <0 if a<b, 0 if a==b, >0 if a>b, doesn't set a or b */
        int (*cmp)(struct var_t *, struct var_t *);

        void (*lshift)(struct var_t *, struct var_t *); /* a = a << b */
        void (*rshift)(struct var_t *, struct var_t *); /* a = a >> b */
        void (*bit_and)(struct var_t *, struct var_t *); /* a = a & b */
        void (*bit_or)(struct var_t *, struct var_t *); /* a = a | b */
        void (*xor)(struct var_t *, struct var_t *);    /* a = a ^ b */
        bool (*cmpz)(struct var_t *);                   /* a == 0 ? */
        void (*incr)(struct var_t *);                   /* a++ */
        void (*decr)(struct var_t *);                   /* a-- */
        void (*bit_not)(struct var_t *);                /* ~a */
        void (*negate)(struct var_t *);                 /* -a */
        void (*mov)(struct var_t *, struct var_t *);    /* a = b */
};

/*
 * can't just be a-b, because if they're floats, a non-zero result
 * might cast to 0
 */
#define CMP(a_, b_) (a_ == b_ ? 0 : (a_ < b_ ? -1 : 1))

static void
type_err(struct var_t *v, int magic)
{
        syntax("You may not change variable %s from type %s to type %s",
                nameof(v), typestr(v->magic), typestr(magic));
}

static void
epermit(const char *op)
{
        syntax("%s operation not permitted for this type", op);
}

static void
emismatch(const char *op)
{
        syntax("cannot perform %s operation on mismatched types", op);
}

/* true if v is float or int */
static inline bool
isnumvar(struct var_t *v)
{
        return v->magic == QINT_MAGIC || v->magic == QFLOAT_MAGIC;
}

static inline long long
var2int(struct var_t *v, const char *op)
{
        if (!isnumvar(v))
                epermit(op);
        return v->magic == QINT_MAGIC ? v->i : (long long)v->f;
}

static inline double
var2float(struct var_t *v, const char *op)
{
        if (!isnumvar(v))
                epermit(op);
        return v->magic == QINT_MAGIC ? (double)v->i : v->f;
}

static bool
cmpztrue(struct var_t *v)
{
        return true;
}

static bool
cmpzfalse(struct var_t *v)
{
        return false;
}

static void
empty_bit_not(struct var_t *v)
{
        qop_assign_int(v, -1LL);
}

static const struct primitive_methods_t empty_primitives = {
        .cmpz           = cmpztrue,
        .bit_not        = empty_bit_not,
};

static void
obj_mov(struct var_t *to, struct var_t *from)
{
        to->o.owner = NULL;

        /* XXX is this the bug, or the fact that I'm not handling it? */
        bug_on(!!to->o.h && to->magic == QOBJECT_MAGIC);

        to->o.h = from->o.h;
        to->o.h->nref++;
}

/*
 * FIXME: Would be nice if we could do like Python and let objects have
 * user-defined operator callbacks
 */
static const struct primitive_methods_t object_primitives = {
        .cmpz           = cmpzfalse,
        .mov            = obj_mov,
};

static void
func_mov(struct var_t *to, struct var_t *from)
{
        if (from->magic == QPTRXU_MAGIC) {
                to->fn.mk.ns = from->px.ns;
                to->fn.mk.oc = from->px.oc;
        } else {
                to->fn.mk.oc = from->fn.mk.oc;
                to->fn.mk.ns = from->fn.mk.ns;
                if (to->magic == QEMPTY_MAGIC || !to->fn.owner)
                        to->fn.owner = from->fn.owner;
        }
}

static const struct primitive_methods_t function_primitives = {
        .cmpz           = cmpzfalse,
        .mov            = func_mov,
};

static void
float_mul(struct var_t *a, struct var_t *b)
{
        a->f *= var2float(b, "*");
}

static void
float_div(struct var_t *a, struct var_t *b)
{
        double f = var2float(b, "/");
        /* XXX: Should have some way of logging error to user */
        if (fpclassify(f) != FP_NORMAL)
                a->f = 0.;
        else
                a->f /= f;
}

static void
float_add(struct var_t *a, struct var_t *b)
{
        a->f += var2float(b, "+");
}

static void
float_sub(struct var_t *a, struct var_t *b)
{
        a->f -= var2float(b, "-");
}

static int
float_cmp(struct var_t *a, struct var_t *b)
{
        double f = var2float(b, "cmp");
        return CMP(a->f, f);
}

static bool
float_cmpz(struct var_t *a)
{
        return fpclassify(a->f) == FP_ZERO;
}

static void
float_incr(struct var_t *a)
{
        a->f += 1.0;
}

static void
float_decr(struct var_t *a)
{
        a->f -= 1.0;
}

static void
float_negate(struct var_t *a)
{
        a->f = -(a->f);
}

static void
float_mov(struct var_t *to, struct var_t *from)
{
        to->f = var2float(from, "mov");
}

static const struct primitive_methods_t float_primitives = {
        .mul            = float_mul,
        .div            = float_div,
        .add            = float_add,
        .sub            = float_sub,
        .cmp            = float_cmp,
        .cmpz           = float_cmpz,
        .incr           = float_incr,
        .decr           = float_decr,
        .negate         = float_negate,
        .mov            = float_mov,
};

static void
int_mul(struct var_t *a, struct var_t *b)
{
        a->i *= var2int(b, "*");
}

static void
int_div(struct var_t *a, struct var_t *b)
{
        long long i = var2int(b, "/");
        if (i == 0LL)
                a->i = 0;
        else
                a->i /= i;
}

static void
int_mod(struct var_t *a, struct var_t *b)
{
        long long i = var2int(b, "%");
        if (i == 0LL)
                a->i = 0;
        else
                a->i /= i;
}

static void
int_add(struct var_t *a, struct var_t *b)
{
        a->i += var2int(b, "+");
}

static void
int_sub(struct var_t *a, struct var_t *b)
{
        a->i -= var2int(b, "-");
}

static int
int_cmp(struct var_t *a, struct var_t *b)
{
        long long i = var2int(b, "cmp");
        return CMP(a->i, i);
}

static void
int_lshift(struct var_t *a, struct var_t *b)
{
        long long shift = var2int(b, "<<");
        if (shift >= 64)
                a->i = 0LL;
        else if (shift > 0)
                a->i <<= shift;
}

static void
int_rshift(struct var_t *a, struct var_t *b)
{
        /*
         * XXX REVISIT: Policy decision, is this logical shift,
         * or arithmetic shift?
         */
        long long shift = var2int(b, ">>");
        unsigned long long i = a->i;
        if (shift >= 64)
                i = 0LL;
        else if (shift > 0)
                i >>= shift;
        a->i = i;
}

static void
int_bit_and(struct var_t *a, struct var_t *b)
{
        a->i &= var2int(b, "&");
}

static void
int_bit_or(struct var_t *a, struct var_t *b)
{
        a->i |= var2int(b, "|");
}

static void
int_xor(struct var_t *a, struct var_t *b)
{
        a->i ^= var2int(b, "^");
}

static bool
int_cmpz(struct var_t *a)
{
        return a->i == 0LL;
}

static void
int_incr(struct var_t *a)
{
        a->i++;
}

static void
int_decr(struct var_t *a)
{
        a->i--;
}

static void
int_bit_not(struct var_t *a)
{
        a->i = ~(a->i);
}

static void
int_negate(struct var_t *a)
{
        a->i = -a->i;
}

static void
int_mov(struct var_t *a, struct var_t *b)
{
        a->i = var2int(b, "mov");
}

static const struct primitive_methods_t int_primitives = {
        .mul            = int_mul,
        .div            = int_div,
        .mod            = int_mod,
        .add            = int_add,
        .sub            = int_sub,
        .cmp            = int_cmp,
        .lshift         = int_lshift,
        .rshift         = int_rshift,
        .bit_and        = int_bit_and,
        .bit_or         = int_bit_or,
        .xor            = int_xor,
        .cmpz           = int_cmpz,
        .incr           = int_incr,
        .decr           = int_decr,
        .bit_not        = int_bit_not,
        .negate         = int_negate,
        .mov            = int_mov,
};

static void
string_add(struct var_t *a, struct var_t *b)
{
        if (b->magic != QSTRING_MAGIC)
                emismatch("+");
        buffer_puts(&a->s, b->s.s);
}

static int
string_cmp(struct var_t *a, struct var_t *b)
{
        int r;
        if (!a->s.s)
                return b->s.s ? -1 : 1;
        else if (!b->s.s)
                return 1;
        r = strcmp(a->s.s, b->s.s);
        return r ? (r < 0 ? -1 : 1) : 0;
}

static bool
string_cmpz(struct var_t *a)
{
        if (!a->s.s)
                return true;
        /* In C "" is non-NULL, but here we're calling it zero */
        return a->s.s[0] == '\0';
}

static void
string_mov(struct var_t *to, struct var_t *from)
{
        if (from->magic != QSTRING_MAGIC)
                type_err(to, from->magic);
        qop_assign_cstring(to, from->s.s);
}

static const struct primitive_methods_t string_primitives = {
        .add            = string_add,
        .cmp            = string_cmp,
        .cmpz           = string_cmpz,
        .mov            = string_mov,
};

static void
ptrxu_mov(struct var_t *to, struct var_t *from)
{
        if (from->magic == QFUNCTION_MAGIC) {
                to->px.ns = from->fn.mk.ns;
                to->px.oc = from->fn.mk.oc;
        } else if (from->magic == QPTRXU_MAGIC) {
                to->px.ns = from->px.ns;
                to->px.oc = from->px.oc;
        } else {
                type_err(to, from->magic);
        }
}

static const struct primitive_methods_t ptrxu_primitives = {
        .mov            = ptrxu_mov,
};

static void
ptrxi_mov(struct var_t *to, struct var_t *from)
{
        if (from->magic != QPTRXI_MAGIC)
                type_err(to, from->magic);
        to->fni = from->fni;
}

static const struct primitive_methods_t ptrxi_primitives = {
        .mov            = ptrxi_mov,
};

static void
array_mov(struct var_t *to, struct var_t *from)
{
        if (from->magic != QARRAY_MAGIC)
                type_err(to, from->magic);
        to->a = from->a;
        to->a->nref++;
}

static const struct primitive_methods_t array_primitives = {
        /* To do, I may want to support some of these */
        .mov = array_mov,
};

static const struct primitive_methods_t *all_primitives[Q_NMAGIC] = {
        &empty_primitives,
        &object_primitives,
        &function_primitives,
        &float_primitives,
        &int_primitives,
        &string_primitives,
        &ptrxu_primitives,
        &ptrxi_primitives,
        &array_primitives,
};

static inline const struct primitive_methods_t *
primitives_of(struct var_t *v)
{
        bug_on(v->magic >= Q_NMAGIC);
        return all_primitives[v->magic];
}

/**
 * assign a = a * b
 */
void
qop_mul(struct var_t *a, struct var_t *b)
{
        const struct primitive_methods_t *p = primitives_of(a);
        if (!p->mul)
                epermit("*");
        p->mul(a, b);
}

/**
 * assign a = a / b
 */
void
qop_div(struct var_t *a, struct var_t *b)
{
        const struct primitive_methods_t *p = primitives_of(a);
        if (!p->div)
                epermit("/");
        p->div(a, b);
}

/**
 * assign a = a % b
 */
void
qop_mod(struct var_t *a, struct var_t *b)
{
        const struct primitive_methods_t *p = primitives_of(a);
        if (!p->mod)
                epermit("%");
        p->mod(a, b);
}

/**
 * assign a = a + b
 */
void
qop_add(struct var_t *a, struct var_t *b)
{
        const struct primitive_methods_t *p = primitives_of(a);
        if (!p->add)
                epermit("+");
        p->add(a, b);
}

/**
 * assign a = a - b
 */
void
qop_sub(struct var_t *a, struct var_t *b)
{
        const struct primitive_methods_t *p = primitives_of(a);
        if (!p->sub)
                epermit("+");
        p->sub(a, b);
}

/**
 * qop_cmp - compare @a to @b and store result in @a
 * @op: An delimiter token indicating a comparison, e.g. OC_LT
 *
 * WARNING!! this re-casts @a, deleting what it had before.
 */
void
qop_cmp(struct var_t *a, struct var_t *b, int op)
{
        int ret, cmp;
        const struct primitive_methods_t *p = primitives_of(a);
        if (!p->cmp)
                epermit("cmp");
        cmp = p->cmp(a, b);
        switch (op) {
        case OC_EQEQ:
                ret = cmp == 0;
                break;
        case OC_LEQ:
                ret = cmp <= 0;
                break;
        case OC_GEQ:
                ret = cmp >= 0;
                break;
        case OC_NEQ:
                ret = cmp != 0;
                break;
        case OC_LT:
                ret = cmp < 0;
                break;
        case OC_GT:
                ret = cmp > 0;
                break;
        default:
                ret = 0;
                bug();
        }

        /* clobber @a and store ret in it */
        var_reset(a);
        a->magic = QINT_MAGIC;
        a->i = ret;
}

/**
 * qop_shift - left-shift value of @a by amount specified in @b
 *              and store result in @a
 * @op: Must be either OC_LSFHIT or OC_RSHIFT
 */
void
qop_shift(struct var_t *a, struct var_t *b, int op)
{
        const struct primitive_methods_t *p = primitives_of(a);
        if (op == OC_LSHIFT) {
                if (!p->lshift)
                        epermit("<<");
                p->lshift(a, b);
        } else {
                bug_on(op != OC_RSHIFT);
                if (!p->rshift)
                        epermit(">>");
                p->rshift(a, b);
        }
}

/* set a = a & b */
void
qop_bit_and(struct var_t *a, struct var_t *b)
{
        const struct primitive_methods_t *p = primitives_of(a);
        if (!p->bit_and)
                epermit("&");
        p->bit_and(a, b);
}

/* set a = a | b */
void
qop_bit_or(struct var_t *a, struct var_t *b)
{
        const struct primitive_methods_t *p = primitives_of(a);
        if (!p->bit_or)
                epermit("|");
        p->bit_or(a, b);
}

/* set a = a ^ b */
void
qop_xor(struct var_t *a, struct var_t *b)
{
        const struct primitive_methods_t *p = primitives_of(a);
        if (!p->xor)
                epermit("^");
        p->xor(a, b);
}

/**
 * qop_cmpz - Compare @v to zero, NULL, or something like it
 *
 * Return: if @v is...
 *      empty:          true always
 *      integer:        true if zero
 *      float:          true if 0.0 exactly
 *      string:         true if null or even if "", false otherwise
 *      object:         false always, even if empty
 *      anything else:  false or error
 */
bool
qop_cmpz(struct var_t *v)
{
        const struct primitive_methods_t *p = primitives_of(v);
        if (!p->cmpz)
                epermit("cmpz");
        return p->cmpz(v);
}

/* v++ */
void
qop_incr(struct var_t *v)
{
        const struct primitive_methods_t *p = primitives_of(v);
        if (!p->incr)
                epermit("++");
        p->incr(v);
}

/* v-- */
void
qop_decr(struct var_t *v)
{
        const struct primitive_methods_t *p = primitives_of(v);
        if (!p->decr)
                epermit("--");
        p->decr(v);
}

/* ~v */
void
qop_bit_not(struct var_t *v)
{
        const struct primitive_methods_t *p = primitives_of(v);
        if (!p->bit_not)
                epermit("~");
        p->bit_not(v);
}

/* -v */
void
qop_negate(struct var_t *v)
{
        const struct primitive_methods_t *p = primitives_of(v);
        if (!p->negate)
                epermit("-");
        p->negate(v);
}

/* !v WARNING! this clobbers v's type */
void
qop_lnot(struct var_t *v)
{
        bool cond = qop_cmpz(v);
        var_reset(v);
        qop_assign_int(v, (int)cond);
}

/**
 * qop_mov - Assign @to with the contents of @from
 * @to: var getting assigned
 * @from: var reference for assignation.  This will not be modified.
 *
 * If @to isn't empty and its type is different from @from,
 * a syntax error will be thrown.
 *
 * If @to and @from are objects or arrays, they will end up both
 * containing the handle to the same object.
 */
void
qop_mov(struct var_t *to, struct var_t *from)
{
        const struct primitive_methods_t *p;
        if (from == to)
                return;

        bug_on(!from || !to);
        bug_on(from->magic == QEMPTY_MAGIC);
        if (to->magic == QEMPTY_MAGIC) {
                p = primitives_of(from);
                if (!p->mov)
                        epermit("mov");
                p->mov(to, from);
                to->magic = from->magic;
        } else {
                p = primitives_of(to);
                if (!p->mov)
                        epermit("mov");
                p->mov(to, from);
        }
}

/**
 * like qop_mov, but if @to is an incompatible type,
 * it will be reset and clobbered.
 */
void
qop_clobber(struct var_t *to, struct var_t *from)
{
        var_reset(to);
        qop_mov(to, from);
}

void
qop_assign_cstring(struct var_t *v, const char *s)
{
        if (v->magic == QEMPTY_MAGIC) {
                v->magic = QSTRING_MAGIC;
                buffer_init(&v->s);
        } else if (v->magic == QSTRING_MAGIC) {
                buffer_reset(&v->s);
        } else {
                type_err(v, QSTRING_MAGIC);
        }
        if (!s || s[0] == '\0') {
                buffer_putc(&v->s, 'a');
                buffer_reset(&v->s);
        } else {
                buffer_puts(&v->s, s);
        }
}

void
qop_assign_int(struct var_t *v, long long i)
{
        if (v->magic == QEMPTY_MAGIC)
                v->magic = QINT_MAGIC;

        if (v->magic == QINT_MAGIC)
                v->i = i;
        else if (v->magic == QFLOAT_MAGIC)
                v->f = (long long)i;
        else
                type_err(v, QINT_MAGIC);
}

void
qop_assign_float(struct var_t *v, double f)
{
        if (v->magic == QEMPTY_MAGIC)
                v->magic = QFLOAT_MAGIC;

        if (v->magic == QINT_MAGIC)
                v->i = (long long)f;
        else if (v->magic == QFLOAT_MAGIC)
                v->f = f;
        else
                type_err(v, QFLOAT_MAGIC);
}



