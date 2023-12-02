
#define VM_READY 1

#if VM_READY

#include <instructions.h>
#include <limits.h>
#include <egq.h>
#include <stdlib.h>

struct vmframe_t *current_frame;

static struct hashtable_t *symbol_table;

#define list2vmf(li) container_of(li, struct vmframe_t, list)

#define PUSH_(fr, v) \
        do { *((fr)->stackptr)++ = (v); } while (0)
#define POP_(fr) (*--((fr)->stackptr))

#ifdef NDEBUG

static inline void push(struct vmframe_t *fr, struct var_t *v)
        { PUSH_(fr, v); }

static inline struct var_t *pop(struct vmframe_t *fr)
        { return POP_(fr); }

static inline struct var_t *RODATA(struct vmframe_t *fr, instruction_t ii)
        { return fr->ex->rodata[ii.arg2]; }
static inline char *RODATA_STR(struct vmframe_t *fr, instruction_t ii)
        { return RODATA(fr, ii)->strptr; }

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

#endif /* DEBUG */

static struct vmframe_t *
vmframe_alloc(void)
{
        return ecalloc(sizeof(struct vmframe_t));
}

static void
vmframe_free(struct vmframe_t *fr)
{
        if (fr == current_frame)
                current_frame = fr->prev;
        bug_on(fr->stackptr != fr->stack + fr->ap);
        while (fr->stackptr > fr->stack)
                pop(fr);
        free(fr);
}

static inline __attribute__((always_inline)) struct var_t *
VARPTR(struct vmframe_t *fr, instruction_t ii)
{
        switch (ii.arg1) {
        case IARG_PTR_AP:
                return fr->stack[fr->ap + ii.arg2];
        case IARG_PTR_FP:
                return fr->stack[fr->fp + ii.arg2];
        case IARG_PTR_CP:
                return fr->clo[ii.arg2];
        case IARG_PTR_SP:
                return fr->stack[fr->sp + ii.arg2];
        case IARG_PTR_SEEK: {
                char *name = RODATA_STR(fr, ii);
                struct var_t *vp = hashtable_get(symbol_table, name);
                if (vp)
                        return vp;
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

typedef void (*binary_opfunc_t)(struct var_t *, struct var_t *);

static inline __attribute__((always_inline)) void
binary_op_common(struct vmframe_t *fr, binary_opfunc_t op)
{
        struct var_t *rval = pop(fr);
        struct var_t *lval = pop(fr);
        bool rdel;

        /* de-reference rval if it's a pointer */
        if (rval->magic == Q_VARPTR_MAGIC) {
                struct var_t *tmp = rval->vptr;
                var_delete(rval);
                rval = tmp;
                rdel = false;
        } else {
                rdel = true;
        }

        /*
         * Subtle differnece between common binary ops and INSTR_ASSIGN:
         * if ptr, copy rather than de-reference
         */
        if (lval->magic == Q_VARPTR_MAGIC) {
                /* not sure yet if this code should be reachable */
                breakpoint();
                struct var_t *tmp = var_copy(lval->vptr);
                var_delete(lval);
                lval = tmp;
        }

        (*op)(lval, rval);
        push(fr, lval);
        if (rdel)
                var_delete(rval);
}

static struct var_t *
attr_ptr__(struct var_t *obj, struct var_t *deref)
{
        if (deref->magic == Q_STRPTR_MAGIC) {
                if (obj->magic == QOBJECT_MAGIC)
                        return eobject_child(obj, deref->strptr);
                return builtin_method(obj, deref->strptr);
        } else if (deref->magic == QINT_MAGIC) {
                /* because idx stores long long, but ii.i is int */
                if (deref->i < INT_MIN || deref->i > INT_MAX)
                        syntax("Array index out of range");
                if (obj->magic == QARRAY_MAGIC)
                        return earray_child(obj, deref->i);
                else if (obj->magic == QOBJECT_MAGIC)
                        return eobject_nth_child(obj, deref->i);
        } else if (deref->magic == QSTRING_MAGIC) {
                if (obj->magic == QOBJECT_MAGIC)
                        return eobject_child(obj, string_get_cstring(obj));
                return builtin_method(obj, string_get_cstring(obj));
        }

        syntax("Cannot get attribute");
        return NULL;
}

static void
do_nop(struct vmframe_t *fr, instruction_t ii)
{
}

static void
do_push(struct vmframe_t *fr, instruction_t ii)
{
        push(fr, var_new());
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
        struct var_t *v = VARPTR(fr, ii);
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
do_unwind(struct vmframe_t *fr, instruction_t ii)
{
        /*
         * See eval8 in assemble.c.  There's a buildup
         * of the stack each time we iterate through a
         * '.' or [...]
         * The solution is to save the top value (the result),
         * pop #arg2 times, then push the top value back
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
        struct var_t *from = pop(fr);
        struct var_t *to = pop(fr);
        if (from->magic == Q_VARPTR_MAGIC) {
                struct var_t *tmp = from->vptr;
                var_delete(from);
                from = tmp;
        }
        if (to->magic == Q_VARPTR_MAGIC) {
                struct var_t *tmp = to->vptr;
                qop_mov(tmp, from);
                push(fr, to);
        } else {
                struct var_t *tmp = qop_mov(to, from);
                push(fr, tmp);
        }
        var_delete(from);
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
        qop_assign_int(v, 0LL);
        push(fr, v);
}

static void
do_return_value(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *result = pop(fr);

        current_frame = fr->prev;
        vmframe_free(fr);
        fr = current_frame;
        bug_on(current_frame == NULL);
        push(fr, result);
}

static void
do_call_func(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *func, *owner, *res;
        struct vmframe_t *fr_new;
        int narg = ii.arg2;
        bool parent = !!ii.arg1;

        /*
         * XXX REVISIT: This is why a (per-thread) global stack is
         * useful: we already pushed the args in order, but now we're
         * popping them and pushing them again into the new frame's
         * stack, an unnecesssary triple-operation.
         */

        fr_new = vmframe_alloc();
        fr_new->ap = narg;
        while (narg > 0) {
                struct var_t *v = pop(fr);
                /*
                 * we pushed these in order, so we're popping
                 * them out of order.
                 */
                fr_new->stack[narg - 1] = v;
                narg--;
        }
        fr_new->fp = 0;

        func = pop(fr);
        owner = parent ? pop(fr) : NULL;
        call_vmfunction_prep_frame(func, fr_new, owner);

        /* ap may have been updated with optional-arg defaults */
        fr_new->sp = fr_new->ap;
        fr_new->stackptr = fr_new->stack + fr_new->ap;

        /* push new frame */
        fr_new->prev = current_frame;

        current_frame = fr_new;
        res = call_vmfunction(func);
        if (res) {
                /* internal: pop new frame, push result in old frame */
                current_frame = fr_new->prev;
                vmframe_free(fr_new);
                push(fr, res);
        } else {
                /* carry on in new frame */
                bug_on(!fr_new->ex);
                fr_new->ppii = fr_new->ex->instr;
        }
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

        attr = attr_ptr__(obj, deref);
        if (del)
                var_delete(deref);

        /*
         * see INSTR_UNWIND and eval8 in assemble.c... we keep
         * parent on GETATTR command.
         */
        push(fr, obj);
        push(fr, var_copy(attr));
}

static void
do_setattr(struct vmframe_t *fr, instruction_t ii)
{
        bool del = false;
        struct var_t *attr, *val, *deref, *obj;

        val = pop(fr);

        if (ii.arg1 == IARG_ATTR_STACK) {
                deref = pop(fr);
                del = true;
        } else {
                deref = RODATA(fr, ii);
        }

        obj = pop(fr);

        attr = attr_ptr__(obj, deref);
        if (del)
                var_delete(deref);

        qop_mov(attr, val);

        /* attr is the actual thing, not a copy, so don't delete it. */
        var_delete(val);
        var_delete(obj);

        /* XXX: push attr back on stack, or not? */
}

static void
do_b_if(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = pop(fr);
        bool cond = !qop_cmpz(v);
        if (ii.arg1) {
                if (cond)
                        fr->ppii += ii.arg2;
        } else {
                if (!cond)
                        fr->ppii += ii.arg2;
        }
}

static void
do_b(struct vmframe_t *fr, instruction_t ii)
{
        fr->ppii += ii.arg2;
}

static void
do_bitwise_not(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = pop(fr);
        qop_bit_not(v);
        push(fr, v);
}

static void
do_negate(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = pop(fr);
        qop_negate(v);
        push(fr, v);
}

static void
do_logical_not(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = pop(fr);
        qop_lnot(v);
        push(fr, v);
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
rshift(struct var_t *a, struct var_t *b)
{
        qop_shift(a, b, OC_RSHIFT);
}

static void
lshift(struct var_t *a, struct var_t *b)
{
        qop_shift(a, b, OC_LSHIFT);
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
        bug_on(ii.arg1 >= ARRAY_SIZE(OCMAP));
        struct var_t *rval = pop(fr);
        struct var_t *lval = pop(fr);
        bool rdel = false;
        if (rval->magic == Q_VARPTR_MAGIC) {
                struct var_t *tmp = rval->vptr;
                var_delete(rval);
                rval = tmp;
        } else {
                rdel = true;
        }

        if (lval->magic == Q_VARPTR_MAGIC) {
                struct var_t *tmp = var_copy(lval->vptr);
                var_delete(lval);
                lval = tmp;
        }

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
logical_or(struct var_t *a, struct var_t *b)
{
        bool res = !qop_cmpz(a) || !qop_cmpz(b);
        var_reset(a);
        qop_assign_int(a, (int)res);
}

static void
do_logical_or(struct vmframe_t *fr, instruction_t ii)
{
        binary_op_common(fr, logical_or);
}

static void
logical_and(struct var_t *a, struct var_t *b)
{
        bool res = !qop_cmpz(a) && !qop_cmpz(b);
        var_reset(a);
        qop_assign_int(a, (int)res);
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
        qop_incr(v);
        push(fr, v);
}

static void
do_decr(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = pop(fr);
        qop_decr(v);
        push(fr, v);
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

void
vm_execute(struct executable_t *top_level)
{
        instruction_t ii;

        bug_on(!(top_level->flags & FE_TOP));
        /*
         * FIXME: not true when 'load' command is implemented.
         * This whole thing is not reentrant-safe.
         */
        bug_on(current_frame != NULL);

        current_frame = vmframe_alloc();
        current_frame->ex = top_level;
        current_frame->prev = NULL;
        current_frame->ppii = top_level->instr;
        current_frame->stackptr = current_frame->stack;
        current_frame->owner = q_.gbl;

        /*
         * FIXME: lots of indrection and non-register hot access.
         * Maybe this is why Python uses a ginormous switch statement.
         */
        while ((ii = *(current_frame->ppii)++).code != INSTR_END) {
                bug_on((unsigned int)ii.code >= N_INSTR);
                JUMP_TABLE[ii.code](current_frame, ii);
        }
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
        return current_frame->owner;
}

struct var_t *
vm_get_arg(unsigned int idx)
{
        if (idx >= current_frame->ap)
                return NULL;
        return current_frame->stack[idx];
}

#else /* !VM_READY */

void
vm_execute(struct executable_t *top_level)
{
        warning("VM not ready yet");
}

void
moduleinit_vm(void)
{
}

struct var_t *
vm_get_this(void)
{
        /*  \_ (!) _/ */
        return q_.gbl;
}

struct var_t *
vm_get_arg(unsigned int idx)
{
        warning("vm_get_arg unsupported in this build mode");
        return NULL;
}

#endif /* !VM_READY */

