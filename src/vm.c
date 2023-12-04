/*
 * vm.c - Virtual Machine for EvilCandy.
 *
 * This executes the byte code assembled in assembler.c
 *
 * When loading a file, vm_execute is called.  When calling a function
 * from internal code (eg. in a foreach loop), vm_reenter is called.
 */
#include <instructions.h>
#include <evilcandy.h>
#include "token.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static struct vmframe_t *current_frame;

/* XXX arbitrary */
static struct hashtable_t *symbol_table;

#define list2vmf(li) container_of(li, struct vmframe_t, list)

#define PUSH_(fr, v) \
        do { *((fr)->stackptr)++ = (v); } while (0)
#define POP_(fr) (*--((fr)->stackptr))
#define PUSH_LOCAL_(fr, v) \
        PUSH_(fr, v)
#define POP_LOCAL_(fr) POP_(fr)

#ifdef NDEBUG

static inline void push(struct vmframe_t *fr, struct var_t *v)
        { PUSH_(fr, v); }

static inline struct var_t *pop(struct vmframe_t *fr)
        { return POP_(fr); }

static inline struct var_t *RODATA(struct vmframe_t *fr, instruction_t ii)
        { return fr->ex->rodata[ii.arg2]; }
static inline char *RODATA_STR(struct vmframe_t *fr, instruction_t ii)
        { return RODATA(fr, ii)->strptr; }

static inline void push_local(struct vmframe_t *fr, struct var_t *v)
        { PUSH_LOCAL_(fr, v); }

static inline struct var_t *pop_local(struct vmframe_t *fr)
        { return POP_LOCAL_(fr); }

#else /* DEBUG */

static void
push(struct vmframe_t *fr, struct var_t *v)
{
        bug_on(fr->stackptr - fr->stack >= FRAME_STACK_MAX);
        PUSH_(fr, v);
}

static inline struct var_t *
pop(struct vmframe_t *fr)
{
        bug_on(fr->stackptr <= fr->stack);
        return POP_(fr);
}

static inline struct var_t *
RODATA(struct vmframe_t *fr, instruction_t ii)
{
        bug_on(ii.arg2 >= fr->ex->n_rodata);
        return fr->ex->rodata[ii.arg2];
}

static inline char *
RODATA_STR(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *vs = RODATA(fr, ii);
        bug_on(vs->magic != Q_STRPTR_MAGIC);
        return vs->strptr;
}

static inline void
push_local(struct vmframe_t *fr, struct var_t *v)
{
        push(fr, v);
}

static inline struct var_t *
pop_local(struct vmframe_t *fr)
{
        return pop(fr);
}

#endif /* DEBUG */

static struct var_t *
symbol_seek_this_(const char *s)
{
        struct var_t *o = get_this();
        if (o)
                return var_get_attr_by_string_l(o, s);
        return NULL;
}

/*
 * Search in the following order of precedence:
 *   1. __gbl__ (overrides any symbol-table variables also named __gbl__)
 *   2. symbol table, ie. variables declared with ``let'' outside of a
 *      function.
 *   3. attribute of owning object (``this'') with matching name
 *   4. attribute of __gbl__ with matching name
 */
static struct var_t *
symbol_seek_(const char *s)
{
        static char *gbl = NULL;
        struct var_t *v;

        bug_on(!s);

        if (!gbl)
                gbl = literal("__gbl__");

        if (s == gbl)
                return q_.gbl;
        if ((v = hashtable_get(symbol_table, s)) != NULL)
                return v;
        if ((v = symbol_seek_this_(s)) != NULL)
                return v;
        return var_get_attr_by_string_l(q_.gbl, s);
}

static struct var_t *
symbol_seek(const char *s)
{
        struct var_t *ret = symbol_seek_(s);
        if (!ret)
                syntax("Symbol %s not found", s);
        return ret;
}

static struct list_t vframe_free_list = LIST_INIT(&vframe_free_list);

static struct vmframe_t *
vmframe_alloc(void)
{
        struct vmframe_t *ret;
        struct list_t *li = vframe_free_list.next;
        if (li == &vframe_free_list) {
                ret = ecalloc(sizeof(*ret));
                list_init(&ret->alloc_list);
        } else {
                ret = container_of(li, struct vmframe_t, alloc_list);
                list_remove(li);
#ifndef NDEBUG
                bug_on(!ret->freed);
#endif
                memset(ret, 0, sizeof(*ret));
                list_init(&ret->alloc_list);
        }
#ifndef NDEBUG
        ret->freed = false;
#endif
        return ret;
}

static void
vmframe_free(struct vmframe_t *fr)
{
        struct var_t **vpp;

        bug_on(!fr);
        if (fr == current_frame)
                current_frame = fr->prev;

#ifndef NDEBUG
        bug_on(fr->freed);
        fr->freed = true;
#endif

        /*
         * XXX REVISIT: if (fr->stackptr != &fr->stack[fr->ap]),
         * there is a stack inbalance.  But how to tell if this
         * is due to an abrubt 'return' (which would cause this),
         * or a bug (which also cause this)?
         */

        /*
         * Note: fr->clo are the actual closures, not copied.
         * They're managed by their owning function object,
         * so we don't delete them here.
         */
        for (vpp = fr->stack; vpp < fr->stackptr; vpp++)
                var_delete(*vpp);
        if (fr->owner)
                var_delete(fr->owner);
        if (fr->func)
                var_delete(fr->func);
        list_add_tail(&fr->alloc_list, &vframe_free_list);
}

static inline __attribute__((always_inline)) struct var_t *
VARPTR(struct vmframe_t *fr, instruction_t ii)
{
        switch (ii.arg1) {
        case IARG_PTR_AP:
                return fr->stack[fr->ap + ii.arg2];
        case IARG_PTR_FP:
                return fr->stack[ii.arg2];
        case IARG_PTR_CP:
                return fr->clo[ii.arg2];
        case IARG_PTR_SEEK: {
                char *name = RODATA_STR(fr, ii);
                return symbol_seek(name);
        }
        case IARG_PTR_GBL:
                return q_.gbl;
        case IARG_PTR_THIS:
                bug_on(!current_frame || !current_frame->owner);
                return current_frame->owner;
        }
        bug();
        return NULL;
}

static inline struct var_t *var_copy(struct var_t *v)
        { return qop_mov(var_new(), v); }

static inline __attribute__((always_inline)) struct var_t *
VARCOPY(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *vp = VARPTR(fr, ii);
        return vp ? var_copy(vp) : NULL;
}

static inline __attribute__((always_inline)) struct var_t *
RODATA_COPY(struct vmframe_t *fr, instruction_t ii)
{
        return var_copy(RODATA(fr, ii));
}

static struct var_t *
pop_or_deref(struct vmframe_t *fr, bool *del)
{
        struct var_t *v = pop(fr);
        if (v->magic == Q_VARPTR_MAGIC) {
                struct var_t *tmp = v->vptr;
                var_delete(v);
                v = tmp;
                *del = false;
        } else {
                *del = true;
        }
        return v;
}

/*
 * Subtle difference between common binary ops and cmp/assign:
 * if ptr, copy rather than de-reference, since lval will get
 * clobbered by the operation.
 */
static struct var_t *
pop_or_deref_copy(struct vmframe_t *fr)
{
        struct var_t *v = pop(fr);
        if (v->magic == Q_VARPTR_MAGIC) {
                struct var_t *tmp = var_copy(v->vptr);
                var_delete(v);
                v = tmp;
        }
        return v;
}

#define binary_op_common(fr, op) do {   \
        struct var_t *lval, *rval;      \
        bool rdel;                      \
        rval = pop_or_deref(fr, &rdel); \
        lval = pop_or_deref_copy(fr);   \
        op(lval, rval);                 \
        push(fr, lval);                 \
        if (rdel)                       \
                var_delete(rval);       \
} while (0)

#define unary_op_common(fr, op) do {    \
        struct var_t *v = pop(fr);      \
        struct var_t *truev = v;        \
        if (truev->magic == Q_VARPTR_MAGIC) \
                truev = v->vptr;        \
        op(truev);                      \
        push(fr, v);                    \
} while (0)

#define assign_common(fr, op) do {              \
        struct var_t *from, *to;                \
        bool fdel;                              \
        from = pop_or_deref(fr, &fdel);         \
        to = pop(fr);                           \
        bug_on(to->magic != Q_VARPTR_MAGIC);    \
        op(to->vptr, from);                     \
        var_delete(to);                         \
        if (fdel)                               \
                var_delete(from);               \
} while (0)

static inline void
logical_or(struct var_t *a, struct var_t *b)
{
        bool res = !qop_cmpz(a) || !qop_cmpz(b);
        var_reset(a);
        integer_init(a, (int)res);
}

static inline void
logical_and(struct var_t *a, struct var_t *b)
{
        bool res = !qop_cmpz(a) && !qop_cmpz(b);
        var_reset(a);
        integer_init(a, (int)res);
}

static inline void
rshift(struct var_t *a, struct var_t *b)
{
        qop_shift(a, b, OC_RSHIFT);
}

static inline void
lshift(struct var_t *a, struct var_t *b)
{
        qop_shift(a, b, OC_LSHIFT);
}

static void
do_nop(struct vmframe_t *fr, instruction_t ii)
{
}

static void
do_push_local(struct vmframe_t *fr, instruction_t ii)
{
        push_local(fr, var_new());
}

static void
do_push_const(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = RODATA_COPY(fr, ii);
        push(fr, v);
}

static void
do_push_ptr(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v, *p = VARPTR(fr, ii);
        v = var_new();
        v->magic = Q_VARPTR_MAGIC;
        v->vptr = p;
        push(fr, v);
}

static void
do_push_copy(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = VARCOPY(fr, ii);
        push(fr, v);
}

static void
do_pop(struct vmframe_t *fr, instruction_t ii)
{
        var_delete(pop(fr));
}

static void
do_pop_local(struct vmframe_t *fr, instruction_t ii)
{
        var_delete(pop_local(fr));
}

static void
do_unwind(struct vmframe_t *fr, instruction_t ii)
{
        /*
         * See eval8 in assemble.c.  There's a buildup of the stack each
         * time we iterate through a '.' or '[#]' dereference.  It's
         * ugly, but the best solution I could think of is to save the
         * top value (the result), pop #arg2 times, then push the top
         * value back
         */
        struct var_t *sav = pop(fr);
        int count = ii.arg2;
        while (count-- > 0)
                var_delete(pop(fr));
        push(fr, sav);
}

static void
do_assign(struct vmframe_t *fr, instruction_t ii)
{
        assign_common(fr, qop_mov);
}

static void
do_assign_add(struct vmframe_t *fr, instruction_t ii)
{
        assign_common(fr, qop_add);
}

static void
do_assign_sub(struct vmframe_t *fr, instruction_t ii)
{
        assign_common(fr, qop_sub);
}

static void
do_assign_mul(struct vmframe_t *fr, instruction_t ii)
{
        assign_common(fr, qop_mul);
}

static void
do_assign_div(struct vmframe_t *fr, instruction_t ii)
{
        assign_common(fr, qop_div);
}

static void
do_assign_mod(struct vmframe_t *fr, instruction_t ii)
{
        assign_common(fr, qop_mod);
}

static void
do_assign_xor(struct vmframe_t *fr, instruction_t ii)
{
        assign_common(fr, qop_xor);
}

static void
do_assign_ls(struct vmframe_t *fr, instruction_t ii)
{
        assign_common(fr, lshift);
}

static void
do_assign_rs(struct vmframe_t *fr, instruction_t ii)
{
        assign_common(fr, rshift);
}

static void
do_assign_or(struct vmframe_t *fr, instruction_t ii)
{
        assign_common(fr, qop_bit_or);
}

static void
do_assign_and(struct vmframe_t *fr, instruction_t ii)
{
        assign_common(fr, qop_bit_and);
}

static void
do_symtab(struct vmframe_t *fr, instruction_t ii)
{
        char *s = RODATA_STR(fr, ii);
        hashtable_put(symbol_table, s, var_new());
}

static void
do_push_zero(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = var_new();
        integer_init(v, 0LL);
        push(fr, v);
}

static void
do_return_value(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *result = pop(fr);

        current_frame = fr->prev;
        vmframe_free(fr);
        fr = current_frame;
        /*
         * FIXME: This means results cannot be returned
         * from reentrant calls, such as in a foreach loop.
         */
        if (fr)
                push(fr, result);
        else
                var_delete(result);
}

static void
do_call_func(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *func, *owner, *res;
        struct vmframe_t *fr_new;
        int narg = ii.arg2;
        bool parent = ii.arg1 == IARG_WITH_PARENT;
        bool fdel, odel;

        /*
         * XXX REVISIT: This is why a (per-thread) global stack is
         * useful: we already pushed the args in order, but now we're
         * popping them and pushing them again into the new frame's
         * stack, an unnecesssary triple-operation.
         */

        fr_new = vmframe_alloc();
        fr_new->ap = narg;
        while (narg-- > 0)
                fr_new->stack[narg] = pop(fr);

        func = pop_or_deref(fr, &fdel);
        if (parent) {
                owner = pop_or_deref(fr, &odel);
        } else {
                odel = false;
                owner = NULL;
        }

        call_vmfunction_prep_frame(func, fr_new, owner);

        if (owner == fr_new->owner)
                odel = false;
        if (fr_new->func == func)
                fdel = false;

        /* ap may have been updated with optional-arg defaults */
        fr_new->stackptr = fr_new->stack + fr_new->ap;

        /* push new frame */
        fr_new->prev = current_frame;

        current_frame = fr_new;
        res = call_vmfunction(fr_new->func);
        if (res) {
                /* Internal, already called and executed
                 * pop new frame, push result in old frame
                 */
                current_frame = fr_new->prev;
                vmframe_free(fr_new);
                push(fr, res);
        } else {
                /*
                 * User function, not yet executed,
                 * carry on in new frame
                 */
                bug_on(!fr_new->ex);
                fr_new->ppii = fr_new->ex->instr;
        }
        if (fdel)
                var_delete(func);
        if (odel)
                var_delete(owner);
}

static void
do_deffunc(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *func = var_new();
        struct var_t *loc = RODATA(fr, ii);
        bug_on(loc->magic != Q_XPTR_MAGIC);
        function_init_vm(func, loc->xptr);
        push(fr, func);
}

static void
do_add_closure(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *clo = pop(fr);
        struct var_t *func = pop(fr);

        function_vmadd_closure(func, clo);
        push(fr, func);
}

static void
do_add_default(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *deflt = pop(fr);
        struct var_t *func = pop(fr);
        /*
         * XXX what's a check for the reasonable size of ii.arg2 here?
         * 99% of the time, this is single-digit, but some weirdos out
         * there love breaking weak programs.  It's signed 16-bits, so
         * the largest number of args can be 32767.
         */
        function_vmadd_default(func, deflt, ii.arg2);
        push(fr, func);
}

static void
do_deflist(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *arr = var_new();
        array_from_empty(arr);
        push(fr, arr);
}

static void
do_list_append(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *child = pop(fr);
        struct var_t *parent = pop(fr);
        array_add_child(parent, child);
        push(fr, parent);
}

static void
do_defdict(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *obj = var_new();
        object_init(obj);
        push(fr, obj);
}

static void
do_addattr(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *attr = pop(fr);
        struct var_t *obj = pop(fr);
        char *name = RODATA_STR(fr, ii);
        /*
         * There is no var_*_attr for add, since only dictionaries
         * support it.  (Lists have a separate opcode, see
         * do_list_append below.)
         */
        object_add_child(obj, attr, name);
        push(fr, obj);
}
static void
do_getattr(struct vmframe_t *fr, instruction_t ii)
{
        bool del = false;
        struct var_t *attr, *deref, *obj;

        if (ii.arg1 == IARG_ATTR_STACK) {
                deref = pop(fr);
                del = true;
        } else {
                deref = RODATA(fr, ii);
        }

        obj = pop(fr);

        attr = evar_get_attr(obj, deref);
        if (del)
                var_delete(deref);

        /*
         * see eval8 in assemble.c... we keep parent on GETATTR command.
         * Reason being, attr might be a function we're about to call,
         * in which case we need to know the parent.  We resolve the
         * stack discrepancy with INSTR_UNWIND
         */
        push(fr, obj);
        push(fr, var_copy(attr));
}

static void
do_setattr(struct vmframe_t *fr, instruction_t ii)
{
        bool del = false;
        struct var_t *val, *deref, *obj;

        val = pop(fr);

        if (ii.arg1 == IARG_ATTR_STACK) {
                deref = pop(fr);
                del = true;
        } else {
                deref = RODATA(fr, ii);
        }

        obj = pop(fr);

        evar_set_attr(obj, deref, val);
        if (del)
                var_delete(deref);

        /* attr is the actual thing, not a copy, so don't delete it. */
        var_delete(val);
        var_delete(obj);

        /* XXX: push attr back on stack, or not? */
}

static void
do_b_if(struct vmframe_t *fr, instruction_t ii)
{
        bool vdel;
        struct var_t *v = pop_or_deref(fr, &vdel);
        bool cond = !qop_cmpz(v);
        if ((bool)ii.arg1 == cond)
                fr->ppii += ii.arg2;
        if (vdel)
                var_delete(v);
}

static void
do_b(struct vmframe_t *fr, instruction_t ii)
{
        fr->ppii += ii.arg2;
}

static void
do_bitwise_not(struct vmframe_t *fr, instruction_t ii)
{
        unary_op_common(fr, qop_bit_not);
}

static void
do_negate(struct vmframe_t *fr, instruction_t ii)
{
        unary_op_common(fr, qop_negate);
}

static void
do_logical_not(struct vmframe_t *fr, instruction_t ii)
{
        unary_op_common(fr, qop_lnot);
}

static void
do_mul(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, qop_mul);
}

static void
do_div(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, qop_div);
}

static void
do_mod(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, qop_mod);
}

static void
do_add(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, qop_add);
}

static void
do_sub(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, qop_sub);
}

static void
do_lshift(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, lshift);
}

static void
do_rshift(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, rshift);
}

static void
do_cmp(struct vmframe_t *fr, instruction_t ii)
{
        static const int OCMAP[] = {
                OC_EQEQ, OC_LEQ, OC_GEQ, OC_NEQ, OC_LT, OC_GT
        };
        struct var_t *rval, *lval;
        bool rdel;

        bug_on(ii.arg1 >= ARRAY_SIZE(OCMAP));

        rval = pop_or_deref(fr, &rdel);
        lval = pop_or_deref_copy(fr);

        /* qop_cmp clobbers lval and stores result there */
        qop_cmp(lval, rval, OCMAP[ii.arg1]);
        push(fr, lval);
        if (rdel)
                var_delete(rval);
}

static void
do_binary_and(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, qop_bit_and);
}

static void
do_binary_or(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, qop_bit_or);
}

static void
do_binary_xor(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, qop_xor);
}

static void
do_logical_or(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, logical_or);
}

static void
do_logical_and(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, logical_and);
}

static void
do_incr(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = pop(fr);
        if (v->magic == Q_VARPTR_MAGIC) {
                qop_incr(v->vptr);
                var_delete(v);
        } else {
                /* shouldn't be reachable, right? */
                breakpoint();
                qop_incr(v);
        }
}

static void
do_decr(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = pop(fr);
        if (v->magic == Q_VARPTR_MAGIC) {
                qop_decr(v->vptr);
                var_delete(v);
        } else {
                /* shouldn't be reachable, right? */
                breakpoint();
                qop_decr(v);
        }
}

static void
do_end(struct vmframe_t *fr, instruction_t ii)
{
        /* dummy func, INSTR_END is handled in calling function */
}


typedef void (*callfunc_t)(struct vmframe_t *fr, instruction_t ii);

static const callfunc_t JUMP_TABLE[N_INSTR] = {
#include "vm_gen.c.h"
};

#define EXECUTE_LOOP(CHECK_NULL) do {                                   \
        instruction_t ii;                                               \
        while ((ii = *(current_frame->ppii)++).code != INSTR_END) {     \
                bug_on((unsigned int)ii.code >= N_INSTR);               \
                JUMP_TABLE[ii.code](current_frame, ii);                 \
                /*                                                      \
                 * In reentrance mode, INSTR_RETURN may set             \
                 * current_frame to NULL.  This, rather than            \
                 * INSTR_END, is our trigger to leave.                  \
                 */                                                     \
                if (CHECK_NULL && !current_frame)                       \
                        break;                                          \
        }                                                               \
} while (0)

/*
 * reentrance means recursion, means stack stress (the irl stack, not the
 * user-data stack).  Users shouldn't abuse an object's foreach method
 * with lots of nested calls back into another foreach method.
 *
 * XXX: Arbitrary choice for value, do some research and find out if
 * there's a known reason for a specific pick/method for stack overrun
 * protection.
 */
#define VM_REENT_MAX 128

#define REENTRANT_PUSH_(arr, idx) do { \
        if (idx >= VM_REENT_MAX) \
                syntax("Recursion max reahced"); \
        arr[idx] = current_frame; \
        current_frame = NULL; \
        idx++; \
} while (0)
#define REENTRANT_POP_(arr, idx) do { \
        bug_on(idx <= 0); \
        --idx; \
        current_frame = arr[idx]; \
} while (0)

#define REENTRANT_PUSH()                         \
        REENTRANT_PUSH_(vmframe_recursion_stack, \
                        vmframe_recursion_stack_idx)

#define REENTRANT_POP()                         \
        REENTRANT_POP_(vmframe_recursion_stack, \
                        vmframe_recursion_stack_idx)

static struct vmframe_t *vmframe_recursion_stack[VM_REENT_MAX];
static int vmframe_recursion_stack_idx = 0;

/**
 * vm_execute - Execute from the top level of a script.
 * @top_level: Result of assemble()
 */
void
vm_execute(struct executable_t *top_level)
{
        bug_on(!(top_level->flags & FE_TOP));
        REENTRANT_PUSH();

        current_frame = vmframe_alloc();
        current_frame->ex = top_level;
        current_frame->prev = NULL;
        current_frame->ppii = top_level->instr;
        current_frame->stackptr = current_frame->stack;
        current_frame->owner = q_.gbl;

        EXECUTE_LOOP(0);

        /* Don't let vmframe_free try to var_delete these */
        current_frame->func = NULL;
        current_frame->owner = NULL;

        vmframe_free(current_frame);
        bug_on(current_frame != NULL);

        REENTRANT_POP();
}

/**
 * vm_reenter - Call a function--user-defined or internal--from a builtin
 *              callback
 * @func:       Function to call
 * @owner:      ``this'' to set
 * @arc:        Number of arguments being passed to the function
 * @argv:       Array of arguments
 *
 * The return value of the user function will be thrown away
 */
void
vm_reenter(struct var_t *func, struct var_t *owner,
           int argc, struct var_t **argv)
{
        /*
         * FIXME: This is still not **fully** reentrance-proof, it still
         * doesn't allow for the ``load'' command.
         */
        struct vmframe_t *fr;
        struct var_t *res;

        /*
         * XXX REVISIT: lots of subtle differences between this and
         * do_call_func/do_return_value, but they're DRY violations just
         * the same.
         */
        bug_on(current_frame == NULL);

        fr = vmframe_alloc();
        fr->ap = argc;
        while (argc-- > 0)
                fr->stack[argc] = qop_mov(var_new(), argv[argc]);

        call_vmfunction_prep_frame(func, fr, owner);

        /*
         * can't push sooner, because call_vmfunction_prep_frame will
         * call get_this() if @owner is NULL
         */
        REENTRANT_PUSH();
        current_frame = fr;
        fr->prev = NULL;

        fr->stackptr = fr->stack + fr->ap;

        res = call_vmfunction(fr->func);
        if (res) {
                current_frame = fr->prev;
                vmframe_free(fr);
        } else {
                fr->ppii = fr->ex->instr;
                EXECUTE_LOOP(1);
                bug_on(current_frame);
                /* fr was already done by do_return_value */
        }

        REENTRANT_POP();
        bug_on(!current_frame);
}

void
moduleinit_vm(void)
{
        symbol_table = malloc(sizeof(*symbol_table));
        hashtable_init(symbol_table, ptr_hash, ptr_key_match,
                       var_bucket_delete);
}

struct var_t *
vm_get_this(void)
{
        bug_on(!current_frame);
        return current_frame->owner;
}

struct var_t *
vm_get_arg(unsigned int idx)
{
        if (idx >= current_frame->ap)
                return NULL;
        return current_frame->stack[idx];
}

