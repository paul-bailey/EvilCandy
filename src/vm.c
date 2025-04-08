/*
 * vm.c - Virtual Machine for EvilCandy.
 *
 * This executes the byte code assembled in assembler.c
 *
 * When loading a file, vm_execute is called.  When calling a function
 * from internal code (eg. in a foreach loop), vm_reenter is called.
 *
 * EvilCandy uses a jump table of function pointers.  Most of this file
 * consists of the callbacks; the table itself is auto-generated as
 * "vm_gen.c.h" and inserted with an #include in the middle of JUMP_TABLE
 * below.
 *
 * This is slightly less optimal than a switch statement, since the
 * callbacks need access to some upstream variables; C's pass-by-value
 * enforcement requires the overhead of passing pointers onto the stack
 * during these function calls.  The problem with switch statements is
 * that they will compile into a (crippling, in this case) if-else-if
 * block unless you take the sort of fussy precautions that are way too
 * easy to overlook.  Looking at Cpython's code, I see that they use a
 * fifty-mile-long switch statement with tons of gimmicks and Gnu
 * extensions like arrays of goto labels.  Well, more power to them,
 * since they also have legions of developers to check each other.
 * I'll play it safe and accept being like 5% slower.
 */
#include <instructions.h>
#include <evilcandy.h>
#include "token.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* XXX arbitrary */
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
        bug_on(vs->magic != TYPE_STRPTR);
        return vs->strptr;
}

#endif /* DEBUG */

static struct var_t *
symbol_seek_this_(struct vmframe_t *fr, struct var_t *name)
{
        struct var_t *o = get_this(fr);
        if (o)
                return var_getattr(o, name);
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
symbol_seek_(struct vmframe_t *fr, struct var_t *name)
{
        static char *gbl = NULL;
        struct var_t *v;
        const char *s = name->strptr;

        bug_on(name->magic != TYPE_STRPTR);
        bug_on(!s);

        if (!gbl)
                gbl = literal_put("__gbl__");

        if (s == gbl)
                return GlobalObject;
        if ((v = hashtable_get(symbol_table, s)) != NULL)
                return v;
        if ((v = symbol_seek_this_(fr, name)) != NULL)
                return v;
        return object_getattr(GlobalObject, s);
}

static struct var_t *
symbol_seek(struct vmframe_t *fr, struct var_t *name)
{
        bug_on(name->magic != TYPE_STRPTR);
        struct var_t *ret = symbol_seek_(fr, name);
        if (!ret) {
                err_setstr(RuntimeError,
                           "Symbol %s not found", name->strptr);
        }
        return ret;
}

/*
 * DOC: Frame allocation
 *
 *      When returning from one function it will probably not be long
 *      before we call another one.  It makes no sense to keep on calling
 *      malloc and free for our frame structs.  Instead we never free the
 *      old frame; we add it to an inactive-frames list, to be repurposed
 *      on the next function call.  We only need to call malloc whenever
 *      we exceed our previous max of how deeply nested our function calls
 *      have been.  The total number of allocated frames will never exceed
 *      VM_RECURSION_MAX, and they will rarely even exceed a small handful,
 *      even in long-running scripts.
 */
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
                VAR_DECR_REF(*vpp);
        if (fr->owner)
                VAR_DECR_REF(fr->owner);
        if (fr->func)
                VAR_DECR_REF(fr->func);
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
                struct var_t *name = RODATA(fr, ii);
                return symbol_seek(fr, name);
        }
        case IARG_PTR_GBL:
                return GlobalObject;
        case IARG_PTR_THIS:
                bug_on(!fr || !fr->owner);
                return fr->owner;
        }
        bug();
        return NULL;
}

static struct block_t *
vmframe_pop_block(struct vmframe_t *fr)
{
        /* XXX can a user error also cause this? */
        if (fr->n_blocks <= 0)
                fail("(possible bug) Frame stack underflow");

        fr->n_blocks--;
        return &fr->blocks[fr->n_blocks];
}

static int
vmframe_push_block(struct vmframe_t *fr, unsigned char reason)
{
        struct block_t *bl;
        if (fr->n_blocks >= FRAME_NEST_MAX) {
                err_setstr(RuntimeError, "Frame stack overflow");
                return -1;
        }
        bl = &fr->blocks[fr->n_blocks];
        bl->stack_level = fr->stackptr;
        bl->type = reason;
        fr->n_blocks++;
        return 0;
}

static void
vmframe_unwind_block(struct vmframe_t *fr, struct block_t *bl)
{
        while (fr->stackptr > bl->stack_level) {
                struct var_t *v = pop(fr);
                VAR_DECR_REF(v);
        }
}

static int
binary_op_common(struct vmframe_t *fr,
                 struct var_t *(*op)(struct var_t *, struct var_t *))
{
        struct var_t *lval, *rval, *opres;
        int ret = 0;
        rval = pop(fr);
        lval = pop(fr);
        opres = op(lval, rval);
        if (opres)
                push(fr, opres);
        else
                ret = -1;
        VAR_DECR_REF(rval);
        VAR_DECR_REF(lval);
        return ret;
}

static int
unary_op_common(struct vmframe_t *fr,
                struct var_t *(*op)(struct var_t *))
{
        struct var_t *v = pop(fr);
        struct var_t *opres = op(v);
        if (!opres)
                return -1;
        push(fr, opres);
        return 0;
}

static int
assign_common(struct vmframe_t *fr,
              struct var_t *(*op)(struct var_t *, struct var_t *))
{
        struct var_t *from, *to, *opres;
        int ret = 0;
        from = pop(fr);
        to = pop(fr);
        opres = op(to, from);
        if (!opres)
                ret = -1;
        else if (!qop_mov(to, opres))
                ret = -1;

        if (opres)
                VAR_DECR_REF(opres);
        VAR_DECR_REF(to);
        VAR_DECR_REF(from);
        return ret;
}

static inline struct var_t *
logical_or(struct var_t *a, struct var_t *b)
{
        int status;
        bool res = !qop_cmpz(a, &status);
        if (status)
                return NULL;
        res = res || !qop_cmpz(b, &status);
        if (status)
                return NULL;
        return intvar_new((int)res);
}

static inline struct var_t *
logical_and(struct var_t *a, struct var_t *b)
{
        int status;
        bool res = !qop_cmpz(a, &status);
        if (status)
                return NULL;
        res = res && !qop_cmpz(b, &status);
        if (status)
                return NULL;
        return intvar_new((int)res);
}

static inline struct var_t *
rshift(struct var_t *a, struct var_t *b)
{
        return qop_shift(a, b, OC_RSHIFT);
}

static inline struct var_t *
lshift(struct var_t *a, struct var_t *b)
{
        return qop_shift(a, b, OC_LSHIFT);
}

static int
do_nop(struct vmframe_t *fr, instruction_t ii)
{
        return 0;
}

static int
do_load(struct vmframe_t *fr, instruction_t ii)
{
        load_file(RODATA_STR(fr, ii));
        return 0;
}

static int
do_push_local(struct vmframe_t *fr, instruction_t ii)
{
        push(fr, var_new());
        return 0;
}

static int
do_push_const(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = qop_mov(var_new(), RODATA(fr, ii));
        if (!v)
                return -1;
        push(fr, v);
        return 0;
}

static int
do_push_ptr(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *p = VARPTR(fr, ii);
        if (!p)
                return -1;
        VAR_INCR_REF(p);
        push(fr, p);
        return 0;
}

static int
do_pop(struct vmframe_t *fr, instruction_t ii)
{
        VAR_DECR_REF(pop(fr));
        return 0;
}

static int
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
                VAR_DECR_REF(pop(fr));
        push(fr, sav);
        return 0;
}

static int
do_push_block(struct vmframe_t *fr, instruction_t ii)
{
        return vmframe_push_block(fr, ii.arg1);
}

static int
do_pop_block(struct vmframe_t *fr, instruction_t ii)
{
        vmframe_unwind_block(fr, vmframe_pop_block(fr));
        return 0;
}

static int
do_break(struct vmframe_t *fr, instruction_t ii)
{
        struct block_t *bl;
        do {
                bl = vmframe_pop_block(fr);
        } while (bl->type != IARG_LOOP);
        vmframe_unwind_block(fr, bl);
        return 0;
}

static int
do_assign(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *from, *to;
        int res = 0;
        from = pop(fr);
        to = pop(fr);
        if (qop_mov(to, from) == NULL)
                res = -1;
        else if (!!(ii.arg1 & IARG_FLAG_CONST))
                to->flags |= VF_CONST;

        VAR_DECR_REF(to);
        VAR_DECR_REF(from);
        return res;
}

static int
do_assign_add(struct vmframe_t *fr, instruction_t ii)
{
        return assign_common(fr, qop_add);
}

static int
do_assign_sub(struct vmframe_t *fr, instruction_t ii)
{
        return assign_common(fr, qop_sub);
}

static int
do_assign_mul(struct vmframe_t *fr, instruction_t ii)
{
        return assign_common(fr, qop_mul);
}

static int
do_assign_div(struct vmframe_t *fr, instruction_t ii)
{
        return assign_common(fr, qop_div);
}

static int
do_assign_mod(struct vmframe_t *fr, instruction_t ii)
{
        return assign_common(fr, qop_mod);
}

static int
do_assign_xor(struct vmframe_t *fr, instruction_t ii)
{
        return assign_common(fr, qop_xor);
}

static int
do_assign_ls(struct vmframe_t *fr, instruction_t ii)
{
        return assign_common(fr, lshift);
}

static int
do_assign_rs(struct vmframe_t *fr, instruction_t ii)
{
        return assign_common(fr, rshift);
}

static int
do_assign_or(struct vmframe_t *fr, instruction_t ii)
{
        return assign_common(fr, qop_bit_or);
}

static int
do_assign_and(struct vmframe_t *fr, instruction_t ii)
{
        return assign_common(fr, qop_bit_and);
}

static int
do_symtab(struct vmframe_t *fr, instruction_t ii)
{
        char *s = RODATA_STR(fr, ii);
        hashtable_put(symbol_table, s, var_new());
        return 0;
}

static int
do_push_zero(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = intvar_new(0LL);
        push(fr, v);
        return 0;
}

static int
do_return_value(struct vmframe_t *fr, instruction_t ii)
{
        return RES_RETURN;
}

static int
do_call_func(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *func, *owner, *retval, **argv;
        int argc;

        argc = ii.arg2;
        /*
         * Low-level hack alert!!
         * It would be cleaner to allocate an array and fill
         * it by pop()-ing off the stack, but that's overkill
         */
        argv = fr->stackptr - argc;
        func = *(fr->stackptr - argc - 1);
        if (ii.arg1 == IARG_WITH_PARENT)
                owner = *(fr->stackptr - argc - 2);
        else
                owner = NULL;

        /* see comments to vm_reenter: this may be NULL */
        retval = vm_reenter(fr, func, owner, argc, argv);
        if (!retval)
                retval = var_new();

        /* Unwind stack in calling frame */
        while (argc-- != 0)
                pop(fr); /* arg */
        pop(fr); /* func */
        VAR_DECR_REF(func);
        if (owner) {
                pop(fr); /* owner */
                VAR_DECR_REF(owner);
        }

        if (retval == ErrorVar)
                return RES_ERROR;

        push(fr, retval);
        return RES_OK;
}

static int
do_deffunc(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *func;
        struct var_t *loc = RODATA(fr, ii);
        bug_on(loc->magic != TYPE_XPTR);
        func = funcvar_new_user(loc->xptr);
        push(fr, func);
        return 0;
}

static int
do_add_closure(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *clo = pop(fr);
        struct var_t *func = pop(fr);

        function_add_closure(func, clo);
        push(fr, func);
        return 0;
}

static int
do_add_default(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *deflt = pop(fr);
        struct var_t *func = pop(fr);
        /*
         * XXX what's a check for the reasonable size of ii.arg2 here?
         * 99% of the time, this is single-digit, but some weirdos out
         * there love breaking weak programs. FRAME_STACK_MAX limits
         * number of args to something than can easily fit into ii.arg2.
         */
        function_add_default(func, deflt, ii.arg2);
        push(fr, func);
        return 0;
}

static int
do_deflist(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *arr = arrayvar_new();
        push(fr, arr);
        return 0;
}

static int
do_list_append(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *child = pop(fr);
        struct var_t *parent = pop(fr);
        array_append(parent, child);
        VAR_DECR_REF(child);
        push(fr, parent);
        return 0;
}

static int
do_defdict(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *obj = objectvar_new();
        push(fr, obj);
        return 0;
}

static int
do_addattr(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *attr = pop(fr);
        struct var_t *obj = pop(fr);
        char *name = RODATA_STR(fr, ii);
        int res;
        /*
         * There is no var_*_attr for add, since only dictionaries
         * support it.  (Lists have a separate opcode, see
         * do_list_append below.)
         */
        if (!!(ii.arg1 & IARG_FLAG_CONST))
                attr->flags |= VF_CONST;
        res = object_addattr(obj, attr, name);
        VAR_DECR_REF(attr);
        push(fr, obj);
        return res;
}

static int
do_getattr(struct vmframe_t *fr, instruction_t ii)
{
        bool del = false;
        struct var_t *attr, *deref, *obj;
        int ret = 0;

        if (ii.arg1 == IARG_ATTR_STACK) {
                deref = pop(fr);
                del = true;
        } else {
                deref = RODATA(fr, ii);
        }

        obj = pop(fr);

        attr = var_getattr(obj, deref);
        if (!attr) {
                err_attribute("get", deref, obj);
                ret = -1;
        } else if (obj->magic != TYPE_STRING || deref->magic != TYPE_INT) {
                /*
                 * FIXME: This is hacky, but string_nth_child creates a
                 * new var, the others return an existing var, and I need to
                 * know that.  Alternative is to make the getattr callbacks
                 * themselves decide whether or not to VAR_INCR_REF, but that
                 * increases the opportunity for mistakes.
                 */
                VAR_INCR_REF(attr);
        }

        if (del)
                VAR_DECR_REF(deref);

        /*
         * see eval8 in assemble.c... we keep parent on GETATTR command.
         * Reason being, attr might be a function we're about to call,
         * in which case we need to know the parent.  We resolve the
         * stack discrepancy with INSTR_UNWIND
         */
        push(fr, obj);
        if (attr)
                push(fr, attr);

        return ret;
}

static int
do_setattr(struct vmframe_t *fr, instruction_t ii)
{
        bool del = false;
        struct var_t *val, *deref, *obj;
        int ret = 0;

        val = pop(fr);

        if (ii.arg1 == IARG_ATTR_STACK) {
                deref = pop(fr);
                del = true;
        } else {
                deref = RODATA(fr, ii);
        }

        obj = pop(fr);

        if (var_setattr(obj, deref, val) != 0) {
                /*
                 * XXX This could clobber a pending error message that
                 * was set during var_setattr
                 */
                err_attribute("set", deref, obj);
                ret = -1;
        }

        if (del)
                VAR_DECR_REF(deref);

        VAR_DECR_REF(val);
        VAR_DECR_REF(obj);
        return ret;
}

static int
do_b_if(struct vmframe_t *fr, instruction_t ii)
{
        int status;
        struct var_t *v = pop(fr);
        bool cond = !qop_cmpz(v, &status);
        if (!status && (bool)ii.arg1 == cond)
                fr->ppii += ii.arg2;
        VAR_DECR_REF(v);
        return status;
}

static int
do_b(struct vmframe_t *fr, instruction_t ii)
{
        fr->ppii += ii.arg2;
        return 0;
}

static int
do_bitwise_not(struct vmframe_t *fr, instruction_t ii)
{
        return unary_op_common(fr, qop_bit_not);
}

static int
do_negate(struct vmframe_t *fr, instruction_t ii)
{
        return unary_op_common(fr, qop_negate);
}

static int
do_logical_not(struct vmframe_t *fr, instruction_t ii)
{
        return unary_op_common(fr, qop_lnot);
}

static int
do_mul(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_mul);
}

static int
do_div(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_div);
}

static int
do_mod(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_mod);
}

static int
do_add(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_add);
}

static int
do_sub(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_sub);
}

static int
do_lshift(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, lshift);
}

static int
do_rshift(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, rshift);
}

static int
do_cmp(struct vmframe_t *fr, instruction_t ii)
{
        static const int OCMAP[] = {
                OC_EQEQ, OC_LEQ, OC_GEQ, OC_NEQ, OC_LT, OC_GT
        };
        struct var_t *rval, *lval, *res;

        bug_on(ii.arg1 >= ARRAY_SIZE(OCMAP));

        rval = pop(fr);
        lval = pop(fr);

        res = qop_cmp(lval, rval, OCMAP[ii.arg1]);
        if (res)
                push(fr, res);
        VAR_DECR_REF(rval);
        VAR_DECR_REF(lval);
        return res ? 0 : -1;
}

static int
do_binary_and(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_bit_and);
}

static int
do_binary_or(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_bit_or);
}

static int
do_binary_xor(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_xor);
}

static int
do_logical_or(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, logical_or);
}

static int
do_logical_and(struct vmframe_t *fr, instruction_t ii)
{
        return binary_op_common(fr, logical_and);
}

static int
do_incr(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = pop(fr);
        int res = qop_incr(v);
        VAR_DECR_REF(v);
        return res;
}

static int
do_decr(struct vmframe_t *fr, instruction_t ii)
{
        struct var_t *v = pop(fr);
        int res = qop_decr(v);
        VAR_DECR_REF(v);
        return res;
}

static int
do_end(struct vmframe_t *fr, instruction_t ii)
{
        /* dummy func, INSTR_END is handled in calling function */
        return 0;
}


/* return value is 0 or OPRES_* enum */
typedef int (*callfunc_t)(struct vmframe_t *fr, instruction_t ii);

static const callfunc_t JUMP_TABLE[N_INSTR] = {
#include "vm_gen.c.h"
};

/*
 * TODO: Some fancy level-of-debuggery configuration.
 * I don't want this bogging down my system just because
 * DEBUG is defined.
 */
#if 0
static void
check_ghost_errors(int res)
{
        bool e = err_exists();

        if (e && res == RES_OK)
                fprintf(stderr, "EvilCandy: Ghost error slipped by\n");
        if (!e && res != RES_OK && res != RES_RETURN)
                fprintf(stderr, "EvilCandy: Error return but none reported\n");
}
#else
# define check_ghost_errors(x_) do { (void)0; } while (0)
#endif

/*
 * DOC: VM_RECURSION_MAX
 *
 * "Recursion" was chosen for lack of a better word.  In this case, it's
 * one of three things:
 *      1) a script being executed (ergo execute_loop is running) loads
 *         another script, thus recursion occurs on execute_loop.
 *      2) a built-in C function calls a user-define script function
 *         (vm_reenter), also causing recursion of execute_loop.
 *      3) ANY call to a user function causes recursion of execute_loop.
 *         I previously tried to limit these calls to the same instance,
 *         no matter how deeply nested the CALL_FUNC instructions got,
 *         but that made a dirty, confusing, error-prone mess of things,
 *         so I gave up and also recurse here.  It's just cleaner that way.
 *
 * Recursion means stack stress (the irl stack, not the user-data stack).
 * Users shouldn't write absurdly deep recursive functions.  In case they
 * do, VM_RECURSION_MAX is the limit at which this can happen.
 *
 * XXX: Make this configurable by the command-line.  Arbitrary choice for
 * value, do some research and find out if there's a known reason for a
 * specific pick/method for stack overrun protection.
 */
#define VM_RECURSION_MAX 128

struct var_t *
execute_loop(struct vmframe_t *fr)
{
        static int recursion_count = 0;
        struct var_t *retval;

        if (recursion_count >= VM_RECURSION_MAX)
                fail("Recursion max reached: you may need to adjust VM_RECURSION_MAX");
        recursion_count++;

        instruction_t ii;
        while ((ii = *(fr->ppii)++).code != INSTR_END) {
                enum result_t res;
                bug_on((unsigned int)ii.code >= N_INSTR);
                res = JUMP_TABLE[ii.code](fr, ii);

                check_ghost_errors(res);

                if (res == RES_OK)
                        continue; /* fast path */

                if (res == RES_RETURN) {
                        retval = pop(fr);
                        goto out;
                } else {
                        /*
                         * res neither 0 nor RES_RETURN, it's an error or exception.
                         *
                         * TODO: Need to know if we gotta exit, unwind an exception
                         * (if we're in a try/catch loop), or just return an error
                         * value if in TTY mode.  Right now we just print error and
                         * return result.
                         */
                        err_print_last(stderr);
                        retval = ErrorVar;
                        goto out;
                }
        }
        /*
         * We hit INSTR_END without any RETURN.
         * Return zero by default.
         */
        retval = intvar_new(0LL);

out:
        bug_on(recursion_count < 0);
        recursion_count--;
        return retval;
}

/**
 * vm_execute - Execute from the top level of a script.
 * @top_level: Result of assemble()
 *
 * Return: RES_OK or an error
 */
enum result_t
vm_execute(struct executable_t *top_level)
{
        struct var_t *res;
        enum result_t ret;
        struct vmframe_t *fr;

        bug_on(!(top_level->flags & FE_TOP));

        fr = vmframe_alloc();
        fr->ex = top_level;
        fr->ppii = top_level->instr;
        fr->stackptr = fr->stack;
        fr->owner = GlobalObject;

        res = execute_loop(fr);
        if (res == ErrorVar) {
                ret = RES_ERROR;
        } else {
                /*
                 * Top level of script does not return a value.  This is
                 * probably the defaault zero value.  Throw it away.
                 */
                ret = RES_OK;
                VAR_DECR_REF(res);
        }

        /* Don't let vmframe_free try to VAR_DECR_REF these */
        fr->func = NULL;
        fr->owner = NULL;

        vmframe_free(fr);
        return ret;
}

/**
 * vm_reenter - Call a function--user-defined or internal--from a builtin
 *              callback
 * @fr_old:     Frame we're currently in
 * @func:       Function to call
 * @owner:      ``this'' to set
 * @arc:        Number of arguments being passed to the function
 * @argv:       Array of arguments
 *
 * Return: Return value of function being called or ErrorVar if execution
 *         failed.  NULL means "nothing to return." Not all functions
 *         calling vm_reenter() use the return value, so let's not make
 *         superfluous calls to var_new()/VAR_DECR_REF().  For callers
 *         who do use the return value, NULL can mean "call var_new() and
 *         use that."
 */
struct var_t *
vm_reenter(struct vmframe_t *fr_old, struct var_t *func,
           struct var_t *owner, int argc, struct var_t **argv)
{
        struct vmframe_t *fr;
        struct var_t *res;

        fr = vmframe_alloc();
        fr->ap = argc;
        while (argc-- > 0) {
                fr->stack[argc] = argv[argc];
                VAR_INCR_REF(argv[argc]);
        }

        if (function_prep_frame(func, fr,
                                owner ? owner : get_this(fr_old))
            == ErrorVar) {
                /* frame only partly set up, we need to set this */
                fr->stackptr = fr->stack + fr->ap;
                vmframe_free(fr);
                return ErrorVar;
        }

        /*
         * can't push sooner, because function_prep_frame will
         * call get_this() if @owner is NULL
         */
        fr->stackptr = fr->stack + fr->ap;

        if (fr->ex)
                fr->ppii = fr->ex->instr;

        res = call_function(fr, fr->func);
        vmframe_free(fr);

        return res;
}

void
moduleinit_vm(void)
{
        symbol_table = malloc(sizeof(*symbol_table));
        hashtable_init(symbol_table, ptr_hash, ptr_key_match,
                       var_bucket_delete);
}

