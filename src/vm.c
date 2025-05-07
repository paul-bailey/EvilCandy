/*
 * vm.c - Virtual Machine for EvilCandy.
 *
 * This executes the byte code assembled in assembler.c
 *
 * When loading a file, vm_exec_script is called.  When calling a function
 * from internal code (eg. in a foreach loop), vm_exec_func is called.
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
#include <evilcandy.h>
#include <token.h>
#include <xptr.h>

static Object *symbol_table = NULL;

/* XXX: Need to be made per-thread */
static Object **vm_stack;
static Object **vm_stack_end;

#define list2vmf(li) container_of(li, Frame, list)

#define PUSH_(fr, v) \
        do { *((fr)->stackptr)++ = (v); } while (0)
#define POP_(fr) (*--((fr)->stackptr))

#ifdef NDEBUG

static inline void push(Frame *fr, Object *v)
        { PUSH_(fr, v); }

static inline Object *pop(Frame *fr)
        { return POP_(fr); }

static inline Object *RODATA(Frame *fr, instruction_t ii)
        { return fr->ex->rodata[ii.arg2]; }

#else /* DEBUG */

static void
push(Frame *fr, Object *v)
{
        bug_on(fr->stackptr >= vm_stack_end);
        PUSH_(fr, v);
}

static inline Object *
pop(Frame *fr)
{
        bug_on(fr->stackptr <= fr->stack);
        return POP_(fr);
}

static inline Object *
RODATA(Frame *fr, instruction_t ii)
{
        bug_on(ii.arg2 >= fr->ex->n_rodata);
        return fr->ex->rodata[ii.arg2];
}

#endif /* DEBUG */

static Object *
symbol_seek(Object *name)
{
        Object *ret;

        bug_on(!isvar_string(name));

        ret = dict_getitem(symbol_table, name);
        if (!ret) {
                err_setstr(NameError, "Symbol %s not found",
                           string_get_cstring(name));
        } else {
                /*
                 * See where used below.  dict_getitem produced a
                 * reference, but so will do_load, since VARPTR might
                 * give it something from the stack instead of here.
                 * So consume one reference to keep it balanced.
                 */
                VAR_DECR_REF(ret);
        }
        return ret;
}

static int
symbol_put(Frame *fr, Object *name, Object *v)
{
        bug_on(!isvar_string(name));
        int ret = dict_setitem_replace(symbol_table, name, v);
        if (ret != RES_OK && !err_occurred()) {
                err_setstr(NameError,
                           "Symbol '%s' does not exist or is unassignable",
                           string_get_cstring(name));
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

static Frame *
vmframe_alloc(void)
{
        Frame *ret;
        struct list_t *li = vframe_free_list.next;
        if (li == &vframe_free_list) {
                ret = ecalloc(sizeof(*ret));
                list_init(&ret->alloc_list);
        } else {
                ret = container_of(li, Frame, alloc_list);
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
vmframe_free(Frame *fr)
{
        bug_on(!fr);

#ifndef NDEBUG
        bug_on(fr->freed);
        fr->freed = true;
#endif

        /*
         * Note: fr->clo are the actual closures, not copied.
         * They're managed by their owning function object,
         * so we don't consume their references here.
         */
        while (fr->stackptr > fr->stack) {
                Object *v = pop(fr);
                VAR_DECR_REF(v);
        }

        /*
         * XXX REVISIT: Not at all obvious that these DECR's are parallel
         * with INCR's in function_prep_frame.
         */
        if (fr->owner)
                VAR_DECR_REF(fr->owner);
        if (fr->func)
                VAR_DECR_REF(fr->func);
        list_add_tail(&fr->alloc_list, &vframe_free_list);
}

static inline __attribute__((always_inline)) Object *
VARPTR(Frame *fr, instruction_t ii)
{
        switch (ii.arg1) {
        case IARG_PTR_AP:
                return fr->stack[fr->ap + ii.arg2];
        case IARG_PTR_FP:
                return fr->stack[ii.arg2];
        case IARG_PTR_CP:
                bug_on(!fr->clo);
                return fr->clo[ii.arg2];
        case IARG_PTR_SEEK: {
                Object *name = RODATA(fr, ii);
                return symbol_seek(name);
        }
        case IARG_PTR_THIS:
                bug_on(!fr || !fr->owner);
                return fr->owner;
        }
        bug();
        return NULL;
}

static struct block_t *
vmframe_pop_block(Frame *fr)
{
        /* XXX can a user error also cause this? */
        if (fr->n_blocks <= 0)
                fail("(possible bug) Frame stack underflow");

        fr->n_blocks--;
        return &fr->blocks[fr->n_blocks];
}

static int
vmframe_push_block(Frame *fr, unsigned char reason, short where)
{
        struct block_t *bl;
        if (fr->n_blocks >= FRAME_NEST_MAX) {
                err_setstr(RecursionError, "Frame stack overflow");
                return -1;
        }
        bl = &fr->blocks[fr->n_blocks];
        bl->stack_level = fr->stackptr;
        bl->type = reason;
        bl->jmpto = fr->ppii + where;
        fr->n_blocks++;
        return 0;
}

static void
vmframe_unwind_block(Frame *fr, struct block_t *bl)
{
        while (fr->stackptr > bl->stack_level) {
                Object *v = pop(fr);
                VAR_DECR_REF(v);
        }
}

static int
binary_op_common(Frame *fr,
                 Object *(*op)(Object *, Object *))
{
        Object *lval, *rval, *opres;
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
unary_op_common(Frame *fr,
                Object *(*op)(Object *))
{
        Object *v = pop(fr);
        Object *opres = op(v);
        if (!opres)
                return -1;
        push(fr, opres);
        return 0;
}

/* helper to assign_common and do_assign */
static int
assign_complete(Frame *fr, instruction_t ii, Object *from)
{
        Object **ppto;
        switch (ii.arg1) {
        case IARG_PTR_AP:
                ppto = fr->stack + fr->ap + ii.arg2;
                break;
        case IARG_PTR_FP:
                ppto = fr->stack + ii.arg2;
                break;
        case IARG_PTR_CP:
                bug_on(!fr->clo);
                ppto = fr->clo + ii.arg2;
                break;
        case IARG_PTR_SEEK:
            {
                /* Global variable or attribute in namespace */
                Object *key = RODATA(fr, ii);
                int ret = symbol_put(fr, key, from);
                VAR_DECR_REF(from);
                VAR_DECR_REF(key);
                return ret;
            }
        case IARG_PTR_THIS:
                err_setstr(TypeError, "You may not assign `this'");
                return RES_ERROR;
        default:
                /*
                 * XXX: bug? could we reach this if user tries to assign
                 * a const, or did the assembler trap that already?
                 */
                DBUG("Invalid ASSIGN address arg1=%d", ii.arg1);
                bug();
                return RES_ERROR;
        }

        bug_on(!ppto || !(*ppto));
        VAR_DECR_REF(*ppto);
        *ppto = from;
        return RES_OK;
}

static Object *
logical_or(Object *a, Object *b)
{
        int status;
        bool res = !var_cmpz(a, &status);
        if (status)
                return NULL;
        res = res || !var_cmpz(b, &status);
        if (status)
                return NULL;
        return intvar_new((int)res);
}

static Object *
logical_and(Object *a, Object *b)
{
        int status;
        bool res = !var_cmpz(a, &status);
        if (status)
                return NULL;
        res = res && !var_cmpz(b, &status);
        if (status)
                return NULL;
        return intvar_new((int)res);
}

static int
do_nop(Frame *fr, instruction_t ii)
{
        return 0;
}

static int
do_push_local(Frame *fr, instruction_t ii)
{
        VAR_INCR_REF(NullVar);
        push(fr, NullVar);
        return 0;
}

static int
do_load_const(Frame *fr, instruction_t ii)
{
        Object *v = RODATA(fr, ii);
        if (!v) /* xxx bug? */
                return -1;
        /*
         * Don't need a shallow copy, because all RODATA
         * vars are the immutable kind--no lists or dictionaries,
         * and there's no arg to do_assign that could replace the
         * const in .rodata.
         */
        VAR_INCR_REF(v);
        push(fr, v);
        return 0;
}

/* ie. 'load variable', not the 'load' keyword */
static int
do_load(Frame *fr, instruction_t ii)
{
        Object *p = VARPTR(fr, ii);
        if (!p)
                return -1;
        VAR_INCR_REF(p);
        push(fr, p);
        return 0;
}

static int
do_pop(Frame *fr, instruction_t ii)
{
        Object *p = pop(fr);
        if (ii.arg1 == IARG_POP_PRINT && p != NullVar) {
                Object *str = var_str(p);
                fprintf(stderr, "%s\n", string_get_cstring(str));
                VAR_DECR_REF(str);
        }
        VAR_DECR_REF(p);
        return 0;
}

static int
do_push_block(Frame *fr, instruction_t ii)
{
        /* note: arg2 is don't-care if arg1 is IARG_BLOCK */
        return vmframe_push_block(fr, ii.arg1, ii.arg2);
}

static int
do_pop_block(Frame *fr, instruction_t ii)
{
        vmframe_unwind_block(fr, vmframe_pop_block(fr));
        return 0;
}

static int
break_or_continue(Frame *fr, int type)
{
        struct block_t *bl = NULL;
        while (fr->n_blocks > 0) {
                bl = vmframe_pop_block(fr);
                if (bl->type == type)
                        break;
        }

        if (!bl || bl->type != type) {
                const char *what = (type == IARG_LOOP)
                                   ? "break" : "continue";
                err_setstr(RuntimeError,
                        "%s not in a control loop", what);
                return RES_ERROR;
        }

        vmframe_unwind_block(fr, bl);
        fr->ppii = bl->jmpto;
        return RES_OK;
}

/*
 * Implements both 'break' and 'continue'.
 * This just manages the stack; next instr does the jump
 */
static int
do_break(Frame *fr, instruction_t ii)
{
        return break_or_continue(fr, IARG_LOOP);
}

static int
do_continue(Frame *fr, instruction_t ii)
{
        return break_or_continue(fr, IARG_CONTINUE);
}

static int
do_assign(Frame *fr, instruction_t ii)
{
        Object *from = pop(fr);
        int ret = assign_complete(fr, ii, from);
        if (ret == RES_ERROR)
                VAR_DECR_REF(from);
        /* else, @from got assigned */
        return ret;
}

static int
do_symtab(Frame *fr, instruction_t ii)
{
        int res;
        Object *name = RODATA(fr, ii);
        bug_on(!isvar_string(name));
        res = dict_setitem_exclusive(symbol_table, name, NullVar);
        if (res != RES_OK) {
                err_setstr(NameError, "Symbol %s already exists",
                                string_get_cstring(name));
        }
        return res;
}

static int
do_return_value(Frame *fr, instruction_t ii)
{
        return RES_RETURN;
}

static int
do_call_func(Frame *fr, instruction_t ii)
{
        Object *func, *retval, **argv;
        int argc;

        argc = ii.arg2;

        /*
         * Low-level hack alert!!
         * It would be cleaner to allocate an array and fill
         * it by pop()-ing off the stack, but that's overkill
         */
        argv = fr->stackptr - argc;
        func = *(fr->stackptr - argc - 1);

        /* see comments to vm_exec_func: this may be NULL */
        retval = vm_exec_func(fr, func, argc, argv,
                              ii.arg1 == IARG_HAVE_DICT);
        if (!retval) {
                VAR_INCR_REF(NullVar);
                retval = NullVar;
        }

        /*
         * Unwind stack in calling frame.
         * vm_exec_func doesn't consume args,
         * so we have to do that instead.
         */
        while (argc-- != 0) {
                Object *arg = pop(fr);
                VAR_DECR_REF(arg);
        }

        pop(fr); /* func */
        VAR_DECR_REF(func);

        if (retval == ErrorVar)
                return RES_ERROR;

        push(fr, retval);
        return RES_OK;
}

static int
do_deffunc(Frame *fr, instruction_t ii)
{
        Object *func;
        Object *x = RODATA(fr, ii);
        bug_on(!isvar_xptr(x));
        func = funcvar_new_user(x);
        push(fr, func);
        return 0;
}

static int
do_add_closure(Frame *fr, instruction_t ii)
{
        Object *clo = pop(fr);
        Object *func = pop(fr);

        function_add_closure(func, clo);
        VAR_DECR_REF(clo);
        push(fr, func);
        return 0;
}

static int
do_func_setattr(Frame *fr, instruction_t ii)
{
        Object *func = pop(fr);
        function_setattr(func, ii.arg1, ii.arg2);
        push(fr, func);
        return 0;
}

static int
do_deftuple(Frame *fr, instruction_t ii)
{
        int n = ii.arg2;
        Object *tup = tuplevar_from_stack(fr->stackptr - n, n, true);
        fr->stackptr -= n;
        push(fr, tup);
        return 0;
}

static int
do_deflist(Frame *fr, instruction_t ii)
{
        int n = ii.arg2;
        Object *arr = arrayvar_from_stack(fr->stackptr - n, n, true);
        fr->stackptr -= n;
        push(fr, arr);
        return 0;
}

static int
do_defdict(Frame *fr, instruction_t ii)
{
        Object **arr;
        Object *obj = dictvar_new();
        int i, n = ii.arg2;

        arr = fr->stackptr - (2 * n);
        for (i = 0; i < (2 * n); i += 2) {
                Object *k = arr[i];
                Object *v = arr[i+1];
                bug_on(!isvar_string(k));
                if (dict_setitem(obj, k, v) != RES_OK) {
                        /* unwind the rest and fail */
                        while (i < n) {
                                VAR_DECR_REF(arr[i]);
                                i++;
                        }
                        goto fail;
                }
                VAR_DECR_REF(k);
                VAR_DECR_REF(v);
        }

        fr->stackptr -= 2 * n;
        push(fr, obj);
        return RES_OK;

fail:
        fr->stackptr -= 2 * n;
        VAR_DECR_REF(obj);
        return RES_ERROR;
}

static int
do_setattr(Frame *fr, instruction_t ii)
{
        Object *val, *key, *obj;
        int ret = 0;

        val = pop(fr);
        key = pop(fr);
        obj = pop(fr);

        /* FIXME: asymmetric var_setattr/var_getattr */
        if (isvar_method(val) && method_peek_self(val) == obj) {
                /*
                 * Prevent unnecessary cyclic reference.
                 * Store the function instead of the 'method'.
                 */
                Object *towner, *tfunc;
                methodvar_tofunc(val, &tfunc, &towner);
                VAR_DECR_REF(towner);
                VAR_DECR_REF(val);
                val = tfunc;
        }

        if ((ret = var_setattr(obj, key, val)) != 0) {
                if (!err_occurred())
                        err_attribute("set", key, obj);
        }

        VAR_DECR_REF(key);
        VAR_DECR_REF(val);
        VAR_DECR_REF(obj);
        return ret;
}

static int
do_getattr(Frame *fr, instruction_t ii)
{
        Object *attr, *key, *obj;
        int ret;

        key = pop(fr);
        obj = pop(fr);

        attr = var_getattr(obj, key);
        if (attr == ErrorVar) {
                if (!err_occurred())
                        err_attribute("get", key, obj);
                ret = RES_ERROR;
        } else {
                push(fr, attr);
                ret = RES_OK;
        }

        VAR_DECR_REF(key);
        VAR_DECR_REF(obj);
        return ret;
}

static int
do_loadattr(Frame *fr, instruction_t ii)
{
        Object *attr, *key, *obj;

        key = pop(fr);
        obj = pop(fr);
        attr = var_getattr(obj, key);
        if (attr == ErrorVar) {
                if (!err_occurred())
                        err_attribute("load", key, obj);
                VAR_DECR_REF(key);
                VAR_DECR_REF(obj);
                return RES_ERROR;
        }

        push(fr, obj);
        push(fr, key);
        push(fr, attr);
        return RES_OK;
}

static int
do_foreach_setup(Frame *fr, instruction_t ii)
{
        /*
         * Check if the item on top of the stack is iterable.
         * If it's a dictionary, replace it with its key list.
         * If neither dictionary nor iterable, throw error.
         * If no error, SP should be in same place as where
         * we started.
         */
        Object *v   = pop(fr); /* haystack */
        if (!v->v_type->sqm) {
                Object *w;
                if (!isvar_dict(v))
                        goto cant;

                /* Dictionary, use its keys instead */
                w = dict_keys(v);
                VAR_DECR_REF(v);
                v = w;
        }

        /* now it's sequential, check if iterable too */
        if (!v->v_type->sqm->getitem)
                goto cant;

        push(fr, v);
        return RES_OK;

cant:
        err_setstr(TypeError, "'%s' is not iterable", typestr(v));
        VAR_DECR_REF(v);
        return RES_ERROR;
}

static int
do_foreach_iter(Frame *fr, instruction_t ii)
{
        Object *haystack, *iter, *needle;
        long long i, n;
        int res = RES_OK;

        iter      = pop(fr);
        haystack  = pop(fr);

        bug_on(!isvar_seq(haystack));
        bug_on(!haystack->v_type->sqm->getitem);
        bug_on(!isvar_int(iter));

        n = seqvar_size(haystack);
        i = intvar_toll(iter);
        bug_on(i > INT_MAX || n > INT_MAX || i < 0 || n < 0);
        if (i >= n) {
                /* leave stack the way we found it */
                push(fr, haystack);
                push(fr, iter);
                goto endloop;
        }

        needle = var_getattr(haystack, iter);
        if (needle == ErrorVar) {
                /* Can happen if item removed in middle of loop */
                res = RES_ERROR;
                VAR_DECR_REF(haystack);
                VAR_DECR_REF(iter);
                goto endloop;
        }

        /* clear old 'needle' */
        VAR_DECR_REF(pop(fr));

        /* replace old iter */
        /*
         * FIXME Much more efficient if I turn the iterator struct in
         * var.c into an object and use that, rather than creating and
         * destroying like a gazillion integers here.
         */
        VAR_DECR_REF(iter);
        iter = intvar_new(i + 1LL);

        push(fr, needle);
        push(fr, haystack);
        push(fr, iter);
        return RES_OK;

endloop:
        /* end of loop or error */
        bug_on(i != 0 && needle == NullVar);
        fr->ppii += ii.arg2;
        return res;
}

static int
do_b_if(Frame *fr, instruction_t ii)
{
        int status;
        Object *v = pop(fr);
        bool cond = !var_cmpz(v, &status);
        if (!status && (bool)ii.arg1 == cond)
                fr->ppii += ii.arg2;
        VAR_DECR_REF(v);
        return status;
}

static int
do_b(Frame *fr, instruction_t ii)
{
        fr->ppii += ii.arg2;
        return 0;
}

static int
do_ternary(Frame *fr, instruction_t ii)
{
        int status;
        Object *choice;
        Object *b = pop(fr);
        Object *a = pop(fr);
        Object *sel = pop(fr);
        if (!var_cmpz(sel, &status)) {
                choice = a;
                VAR_DECR_REF(b);
        } else {
                choice = b;
                VAR_DECR_REF(a);
        }
        VAR_DECR_REF(sel);
        push(fr, choice);
        return status;
}

static int
do_throw(Frame *fr, instruction_t ii)
{
        Object *exc = pop(fr);
        err_set_from_user(exc);
        return RES_EXCEPTION;
}

static int
do_bitwise_not(Frame *fr, instruction_t ii)
{
        return unary_op_common(fr, qop_bit_not);
}

static int
do_negate(Frame *fr, instruction_t ii)
{
        return unary_op_common(fr, qop_negate);
}

static int
do_logical_not(Frame *fr, instruction_t ii)
{
        return unary_op_common(fr, var_lnot);
}

static int
do_mul(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_mul);
}

static int
do_pow(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_pow);
}

static int
do_div(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_div);
}

static int
do_mod(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_mod);
}

static int
do_add(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_add);
}

static int
do_sub(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_sub);
}

static int
do_lshift(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_lshift);
}

static int
do_rshift(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_rshift);
}

static int
do_has(Frame *fr, instruction_t ii)
{
        Object *needle, *haystack, *res;
        bool ires;

        needle = pop(fr);
        haystack = pop(fr);

        ires = var_hasattr(haystack, needle);
        res = intvar_new((int)ires);

        VAR_DECR_REF(needle);
        VAR_DECR_REF(haystack);

        push(fr, res);
        return RES_OK;
}

static int
do_cmp(Frame *fr, instruction_t ii)
{
        Object *rval, *lval, *res;
        int cmp;

        rval = pop(fr);
        lval = pop(fr);

        cmp = var_compare(lval, rval);
        switch (ii.arg1) {
        case IARG_EQ:
                cmp = cmp == 0;
                break;
        case IARG_LEQ:
                cmp = cmp <= 0;
                break;
        case IARG_GEQ:
                cmp = cmp >= 0;
                break;
        case IARG_NEQ:
                cmp = cmp != 0;
                break;
        case IARG_LT:
                cmp = cmp < 0;
                break;
        case IARG_GT:
                cmp = cmp > 0;
                break;
        default:
                bug();
        }

        res = intvar_new(cmp);
        push(fr, res);
        VAR_DECR_REF(rval);
        VAR_DECR_REF(lval);
        return res ? 0 : -1;
}

static int
do_binary_and(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_bit_and);
}

static int
do_binary_or(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_bit_or);
}

static int
do_binary_xor(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, qop_xor);
}

static int
do_logical_or(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, logical_or);
}

static int
do_logical_and(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, logical_and);
}

static int
do_end(Frame *fr, instruction_t ii)
{
        /* dummy func, INSTR_END is handled in calling function */
        return 0;
}

/* return value is 0 or OPRES_* enum */
typedef int (*callfunc_t)(Frame *fr, instruction_t ii);

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
        bool e = err_occurred();

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
 *         (vm_exec_func), also causing recursion of execute_loop.
 *      3) ANY call to a user function causes recursion of execute_loop.
 *         I previously tried to limit these calls to the same instance,
 *         no matter how deeply nested the CALL_FUNC instructions got,
 *         but that made a dirty, confusing, error-prone mess of things,
 *         so I gave up and also recurse here.  It's just cleaner that way.
 *
 * Recursion means stack stress (the irl stack, not the user-data stack).
 * Users shouldn't write absurdly deep recursive functions.  In case they
 * do, VM_RECURSION_MAX is the limit at which this can happen.
 */
#define VM_RECURSION_MAX RECURSION_MAX

Object *
execute_loop(Frame *fr)
{
        static int recursion_count = 0;
        Object *retval;

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
                        /*
                         * IMPORTANT FIXME!! What was retval?
                         * If it was a stack variable, do I need to VAR_INCR_REF?
                         * What if it was a closure?
                         * This may be causing memory leaks or worse, vice-versa.
                         */
                        goto out;
                } else {
                        /* res should be either RES_ERROR or RES_EXCEPTION */
                        Object *tup;
                        struct block_t *bl;

                        if (!err_occurred()) {
                                err_setstr(RuntimeError,
                                           "instruction %hhd:%hhd:%hd unreported error",
                                           ii.code, ii.arg1, ii.arg2);
                        }

                        bl = NULL;
                        while (fr->n_blocks > 0) {
                                bl = vmframe_pop_block(fr);
                                if (bl->type == IARG_TRY)
                                        break;
                        }

                        if (!bl || bl->type != IARG_TRY) {
                                retval = ErrorVar;
                                goto out;
                        }

                        /*
                         * Still here, we have an exception handler.
                         * Unwind stack, push exception onto it, branch
                         * to handler.
                         */
                        vmframe_unwind_block(fr, bl);
                        tup = err_get();
                        bug_on(!tup);
                        push(fr, tup);
                        fr->ppii = bl->jmpto;
                }
        }
        /*
         * We hit INSTR_END without any RETURN.
         * Return null by default.
         */
        VAR_INCR_REF(NullVar);
        retval = NullVar;

out:
        bug_on(recursion_count < 0);
        recursion_count--;
        return retval;
}

/**
 * vm_exec_script - syntactic sugar wrapper to vm_exec_func
 * @top_level: Result of assemble()
 * @fr_old:    Frame of the script calling 'load', or NULL if this is
 *             the entry point.
 *
 * Return: result returned from script or ErrorVar if there was an error.
 */
Object *
vm_exec_script(Object *top_level, Frame *fr_old)
{
        Object *func, *ret;

        bug_on(!isvar_xptr(top_level));
        func = funcvar_new_user(top_level);
        ret = vm_exec_func(fr_old, func, 0, NULL, false);
        VAR_DECR_REF(func);
        return ret;
}

/**
 * vm_exec_func - Call a function--user-defined or internal--from a builtin
 *              callback
 * @fr_old:     Frame we're currently in
 * @func:       Function to call
 * @owner:      ``this'' to set
 * @arc:        Number of arguments being passed to the function
 * @argv:       Array of arguments
 * @have_dict:  True if last item in argv is dictionary
 *
 * Return: Return value of function being called or ErrorVar if execution
 *         failed.
 *
 * Note: This has a net-zero effect on reference counters for @argv,
 *       although they will temporarily be incremented.
 */
Object *
vm_exec_func(Frame *fr_old, Object *func,
             int argc, Object **argv, bool have_dict)
{
        Frame *fr;
        Object *res, *owner;

        if (isvar_method(func)) {
                Object *meth = func;
                if (methodvar_tofunc(meth, &func, &owner) == RES_ERROR)
                        return ErrorVar;

                bug_on(!func);
                bug_on(!owner);
                bug_on(!isvar_function(func));
        } else {
                owner = fr_old ? vm_get_this(fr_old) : GlobalObject;
                /* Keep references balanced here & above part of 'if' */
                VAR_INCR_REF(owner);
                VAR_INCR_REF(func);
        }

        fr = vmframe_alloc();
        fr->stack = fr_old ? fr_old->stackptr : vm_stack;
        fr->ap = argc;
        bug_on(argc > 0 && !argv);
        while (argc-- > 0) {
                /* vmframe_free below decrements these back */
                VAR_INCR_REF(argv[argc]);
                fr->stack[argc] = argv[argc];
        }

        if (function_prep_frame(func, fr, owner, have_dict) == ErrorVar) {
                /* frame only partly set up, we need to set this */
                fr->stackptr = fr->stack + fr->ap;
                vmframe_free(fr);
                return ErrorVar;
        }

        /*
         * XXX If this is all there is between function_prep_frame
         * and call_function, then it ought to all be in the same
         * function.  function.c needs low-level access to fr anyway.
         */
        fr->stackptr = fr->stack + fr->ap;

        if (fr->ex)
                fr->ppii = fr->ex->instr;

        res = call_function(fr, fr->func);
        vmframe_free(fr);

        if (!res) {
                VAR_INCR_REF(NullVar);
                res = NullVar;
        }

        VAR_DECR_REF(func);
        VAR_DECR_REF(owner);

        return res;
}

/*
 * Used for adding built-in symbols during early init.
 * @name should have been filtered through literal()
 * or literal_put().
 */
void
vm_add_global(Object *name, Object *var)
{
        /* moduleinit_vm should have been called first */
        int res;
        bug_on(!symbol_table);
        bug_on(!isvar_string(name));
        res = dict_setitem_exclusive(symbol_table, name, var);
        bug_on(res != RES_OK);
        (void)res;
}

Object *
vm_get_global(const char *name)
{
        Object *k = stringvar_new(name);
        Object *res = dict_getitem(symbol_table, k);
        VAR_DECR_REF(k);
        return res;
}

/**
 * vm_symbol_exists - Check if a global variable exists
 * @key: Name of the global variable.
 */
bool
vm_symbol_exists(Object *key)
{
        /*
         * XXX Give dict_getitem extern linkage so I don't have to
         * waste time with reference counters on dummy variables.
         */
        Object *val;
        val = dict_getitem(symbol_table, key);
        if (val)
                VAR_DECR_REF(val);
        return val != NULL;
}

void
moduleinit_vm(void)
{
        symbol_table = dictvar_new();

        vm_stack = emalloc(sizeof(Object *) * VM_STACK_SIZE);
        vm_stack_end = vm_stack + VM_STACK_SIZE - 1;
}

void
moduledeinit_vm(void)
{
        VAR_DECR_REF(symbol_table);
        /* XXX: any way to clear the stack vars? */
        efree(vm_stack);
}

