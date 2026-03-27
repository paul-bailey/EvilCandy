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
 * fifty-mile-long switch statement with tons of gimmicks like a choice
 * between replacing it with an indefinitely recursible tail-call system
 * or a rat's nest of goto labels with a Gnu-specific array of goto
 * targets.  Well, more power to them, since they also have legions of
 * developers to check each other.  I'll play it safe and accept being
 * like 5% slower.
 */
#include <evilcandy.h>
#include <token.h>
#include <xptr.h>

/* XXX: Need to be made per-thread */
static struct vm_t {
        Object *globals;
        Object *locals;
        Object **stack;
        Object **stack_end;
        struct list_t active_frames;
        struct list_t free_frames;
} vm = { 0 };

#define PUSH_(fr, v) \
        do { *((fr)->stackptr)++ = (v); } while (0)
#define POP_(fr) (*--((fr)->stackptr))

#ifdef NDEBUG

static inline void push(Frame *fr, Object *v)
        { PUSH_(fr, v); }

static inline Object *pop(Frame *fr)
        { return POP_(fr); }

static inline Object *RODATA(Frame *fr, instruction_t ii)
        { return tuple_getitem_noref(fr->ex->_rodata, ii.arg2); }

#else /* DEBUG */

static void
push(Frame *fr, Object *v)
{
        bug_on(fr->stackptr >= vm.stack_end);
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
        bug_on(ii.arg2 >= seqvar_size(fr->ex->rodata));
        return tuple_getitem_noref(fr->ex->rodata, ii.arg2);
}

#endif /* DEBUG */

static inline Frame *
vm_current_frame(void)
{
        struct list_t *li = vm.active_frames.prev;
        if (li == &vm.active_frames)
                return NULL;
        return container_of(li, Frame, alloc_list);
}

static inline Frame *
vm_top_frame(void)
{
        struct list_t *li = vm.active_frames.next;
        if (li == &vm.active_frames)
                return NULL;
        return container_of(li, Frame, alloc_list);
}

static int
symbol_put(Frame *fr, Object *name, Object *v, Object *dict)
{
        int ret;

        bug_on(!isvar_string(name));
        if (!dict)
                goto err;
        ret = dict_setitem_replace(dict, name, v);
        if (ret != RES_OK && !err_occurred())
                goto err;

        return ret;

err:
        err_setstr(NameError,
                   "Symbol %N does not exist or is unassignable", name);
        return RES_ERROR;
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
 *      RECURSION_MAX, and they will rarely even exceed a small handful,
 *      even in long-running scripts.
 */

static Frame *
vmframe_alloc(Object *fn, Object *owner, Frame *fr_old)
{
        Frame *ret, *cur;
        struct list_t *li = vm.free_frames.next;
        if (li == &vm.free_frames) {
                ret = ecalloc(sizeof(*ret));
                list_init(&ret->alloc_list);
        } else {
                ret = container_of(li, Frame, alloc_list);
                list_remove(li);
                memset(ret, 0, sizeof(*ret));
                list_init(&ret->alloc_list);
        }

        if (fr_old) {
                ret->stack = fr_old->stackptr;
        } else if ((cur = vm_current_frame()) != NULL) {
                /* probably a destructor called from VAR_DECR_REF */
                ret->stack = cur->stackptr;
        } else {
                /* 1st ever call */
                ret->stack = vm.stack;
        }
        ret->owner = owner;
        ret->func  = fn;
        ret->ap = 0;
        ret->stackptr = ret->stack + ret->ap;
        VAR_INCR_REF(owner);
        VAR_INCR_REF(fn);

        list_add_tail(&ret->alloc_list, &vm.active_frames);
        return ret;
}

static void
vmframe_free(Frame *fr)
{
        /*
         * Note: fr->clo are the actual closure array, not a copy.
         * They're managed by their owning function object, so we don't
         * consume their references here.
         */
        list_remove(&fr->alloc_list);
        var_lock();
        {
                while (fr->stackptr > fr->stack) {
                        Object *v = pop(fr);
                        VAR_DECR_REF(v);
                }

                if (fr->owner)
                        VAR_DECR_REF(fr->owner);
                if (fr->func)
                        VAR_DECR_REF(fr->func);
                list_add_tail(&fr->alloc_list, &vm.free_frames);
        }
        var_unlock();
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
binary_op_common(Frame *fr, Object *(*op)(Object *, Object *))
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
        VAR_DECR_REF(v);
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
                bug_on((unsigned)ii.arg2 >= fr->n_locals);
                ppto = fr->stack + fr->ap + ii.arg2;
                break;
        case IARG_PTR_FP:
                ppto = fr->stack + ii.arg2;
                break;
        case IARG_PTR_CP:
                bug_on(!fr->clo);
                ppto = fr->clo + ii.arg2;
                break;
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

static int
do_format(Frame *fr, instruction_t ii)
{
        Object *str, *tup, *res;
        str = pop(fr);
        tup = pop(fr);
        res = string_format(str, tup);
        VAR_DECR_REF(str);
        VAR_DECR_REF(tup);
        if (res == ErrorVar)
                return RES_ERROR;
        push(fr, res);
        return RES_OK;
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

static int
do_load_local(Frame *fr, instruction_t ii)
{
        Object *p;
        switch (ii.arg1) {
        case IARG_PTR_AP:
                bug_on((unsigned)ii.arg2 >= fr->n_locals);
                p = fr->stack[fr->ap + ii.arg2];
                break;
        case IARG_PTR_FP:
                p = fr->stack[ii.arg2];
                break;
        case IARG_PTR_CP:
                bug_on(!fr->clo);
                p = fr->clo[ii.arg2];
                break;
        case IARG_PTR_THIS:
                bug_on(!fr || !fr->owner);
                p = fr->owner;
                break;
        default:
                bug();
                return RES_ERROR;
        }
        VAR_INCR_REF(p);
        push(fr, p);
        return RES_OK;
}

static int
do_load_arg(Frame *fr, instruction_t ii)
{
        Object *p;

        bug_on((short)ii.arg2 < 0);
        if (ii.arg2 >= fr->ap) {
                err_setstr(RangeError,
                           "requested argument #%hd but only %d provided",
                           ii.arg2, fr->ap);
                return RES_ERROR;
        }
        p = fr->stack[ii.arg2];
        VAR_INCR_REF(p);
        push(fr, p);
        return RES_OK;
}

static int
do_load_global(Frame *fr, instruction_t ii)
{
        Object *p, *name;

        name = RODATA(fr, ii);
        bug_on(!isvar_string(name));

        bug_on(!vm.globals);

        p = dict_getitem(vm.globals, name);
        if (!p) {
                err_setstr(NameError, "Symbol %N not found", name);
                return RES_ERROR;
        }

        push(fr, p);
        return RES_OK;
}

static int
do_load_name(Frame *fr, instruction_t ii)
{
        Object *p, *name;

        name = RODATA(fr, ii);
        bug_on(!isvar_string(name));

        if (vm.locals) {
                p = dict_getitem(vm.locals, name);
                if (p) {
                        push(fr, p);
                        return RES_OK;
                }
        }

        err_setstr(NameError, "Symbol %N not found", name);
        return RES_ERROR;
}

static int
do_pop(Frame *fr, instruction_t ii)
{
        Object *p;
        int n = ii.arg2;

        if (!n) /* artifact: treat zero like 1 */
                n++;

        while (n--) {
                p = pop(fr);

                if (ii.arg1 == IARG_POP_PRINT && p != NullVar) {
                        int ret;
                        Object *str;

                        /*
                         * n > 1 implies unpacking, which should never
                         * happen at the top level, so this would
                         * definitely be a bug.
                         */
                        bug_on(ii.arg2 > 1);

                        if (!vm.locals)
                                vm.locals = dictvar_new();
                        /* don't use symbol_put, '_' might not exist yet. */
                        ret = dict_setitem(vm.locals, STRCONST_ID(_), p);
                        bug_on(ret != 0);
                        (void)ret;

                        str = var_str(p);
                        fprintf(stderr, "%s\n", string_cstring(str));
                        VAR_DECR_REF(str);
                }
                VAR_DECR_REF(p);
        }
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
do_assign_local(Frame *fr, instruction_t ii)
{
        Object *from = pop(fr);
        return assign_complete(fr, ii, from);
}

static int
do_assign_global(Frame *fr, instruction_t ii)
{
        Object *val, *key;
        int ret;

        val = pop(fr);
        key = RODATA(fr, ii);
        ret = symbol_put(fr, key, val, vm.globals);
        VAR_DECR_REF(val);
        return ret;
}

static int
new_global_or_name(Frame *fr, instruction_t ii, Object *dict)
{
        int res;
        Object *name = RODATA(fr, ii);
        bug_on(!isvar_string(name));
        res = dict_setitem_exclusive(dict, name, NullVar);
        if (res != RES_OK)
                err_setstr(NameError, "Symbol %N already exists", name);
        return res;
}

static int
do_new_global(Frame *fr, instruction_t ii)
{
        return new_global_or_name(fr, ii, vm.globals);
}

static int
do_assign_name(Frame *fr, instruction_t ii)
{
        Object *val, *key;
        int ret;

        val = pop(fr);
        key = RODATA(fr, ii);
        ret = symbol_put(fr, key, val, vm.locals);
        VAR_DECR_REF(val);
        return ret;
}

static int
do_new_name(Frame *fr, instruction_t ii)
{
        if (!vm.locals)
                vm.locals = dictvar_new();
        return new_global_or_name(fr, ii, vm.locals);
}

static int
do_return_value(Frame *fr, instruction_t ii)
{
        return RES_RETURN;
}

static int
do_call_func(Frame *fr, instruction_t ii)
{
        Object *kwargs, *args, *func, *retval;

        kwargs = pop(fr);
        args = pop(fr);
        func = pop(fr);

        if (kwargs == NullVar) {
                VAR_DECR_REF(kwargs);
                kwargs = NULL;
        }

        bug_on(!isvar_array(args));
        retval = vm_exec_func(fr, func, args, kwargs);
        VAR_DECR_REF(args);
        VAR_DECR_REF(func);
        if (kwargs)
                VAR_DECR_REF(kwargs);

        if (retval == ErrorVar)
                return RES_ERROR;
        push(fr, retval);
        return RES_OK;
}

static int
do_deffunc(Frame *fr, instruction_t ii)
{
        Object *func;
        Object *x = pop(fr);
        bug_on(!isvar_xptr(x));
        func = funcvar_new_user(x);
        VAR_DECR_REF(x);
        push(fr, func);
        return 0;
}

static int
do_defclass(Frame *fr, instruction_t ii)
{
        Object *dict, *bases, *class;

        dict = pop(fr);
        bases = pop(fr);
        class = classvar_new(bases, dict);
        VAR_DECR_REF(bases);
        VAR_DECR_REF(dict);
        if (class == ErrorVar)
                return RES_ERROR;
        push(fr, class);
        return RES_OK;
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
do_list_extend(Frame *fr, instruction_t ii)
{
        enum result_t ret;
        Object *item = pop(fr);
        Object *list = pop(fr);
        ret = array_extend(list, item);
        if (ret == RES_OK)
                push(fr, list);
        else
                VAR_DECR_REF(list);
        VAR_DECR_REF(item);
        return ret;
}

static int
do_list_append(Frame *fr, instruction_t ii)
{
        enum result_t ret;
        Object *item = pop(fr);
        Object *list = pop(fr);
        ret = array_append(list, item);
        if (ret == RES_OK)
                push(fr, list);
        else
                VAR_DECR_REF(list);
        VAR_DECR_REF(item);
        return ret;
}

static int
do_cast_tuple(Frame *fr, instruction_t ii)
{
        Object *list, *tup;

        list = pop(fr);
        bug_on(!isvar_array(list));

        tup = tuplevar_from_stack(array_get_data(list),
                                  seqvar_size(list), false);
        push(fr, tup);

        VAR_DECR_REF(list);
        return RES_OK;
}

static int
do_defset(Frame *fr, instruction_t ii)
{
        Object *list, *set;
        list = pop(fr);
        bug_on(!isvar_array(list));

        set = setvar_new(list);
        VAR_DECR_REF(list);

        if (set == ErrorVar || !set)
                return RES_ERROR;
        push(fr, set);
        return RES_OK;
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
                if (dict_setitem(obj, k, v) != RES_OK) {
                        bug_on(!err_occurred());
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
do_defdict_k(Frame *fr, instruction_t ii)
{
        Object *keytup, **keys, **vals, *d;
        int i, n = ii.arg2;
        enum result_t res = RES_OK;

        keytup = pop(fr);
        bug_on(!isvar_tuple(keytup) || seqvar_size(keytup) != n);

        keys = tuple_get_data(keytup);
        vals = fr->stackptr - n;

        d = dictvar_new();
        for (i = 0; i < n; i++) {
                Object *k = keys[i];
                Object *v = vals[i];
                if (dict_setitem(d, k, v) != RES_OK) {
                        bug_on(!err_occurred());
                        while (i < n) {
                                VAR_DECR_REF(vals[i]);
                                i++;
                        }
                        res = RES_ERROR;
                        break;
                }
                VAR_DECR_REF(v);
        }

        VAR_DECR_REF(keytup);
        fr->stackptr -= n;
        if (res == RES_OK)
                push(fr, d);
        return res;
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
do_delattr(Frame *fr, instruction_t ii)
{
        Object *obj, *key;
        int res;

        key = pop(fr);
        obj = pop(fr);
        res = var_delattr(obj, key);
        VAR_DECR_REF(key);
        VAR_DECR_REF(obj);
        return res;
}

static int
do_foreach_setup(Frame *fr, instruction_t ii)
{
        Object *v = pop(fr);
        Object *it = iterator_get(v);
        if (!it) {
                err_iterable(v, NULL);
                VAR_DECR_REF(v);
                return RES_ERROR;
        }
        VAR_DECR_REF(v);

        push(fr, it);
        return RES_OK;
}

static int
do_copy(Frame *fr, instruction_t ii)
{
        int idx = ii.arg2;
        Object *x = fr->stackptr[-idx];
        VAR_INCR_REF(x);
        push(fr, x);
        return RES_OK;
}

static int
do_foreach_iter(Frame *fr, instruction_t ii)
{
        Object *iter, *needle;

        iter = pop(fr);
        needle = iterator_next(iter);
        if (needle) {
                push(fr, iter);
                push(fr, needle);
        } else {
                /* Leave the stack the way we found it */
                push(fr, iter);
                fr->ppii += ii.arg2;
        }
        return RES_OK;
}

static int
do_b_if(Frame *fr, instruction_t ii)
{
        Object *v = pop(fr);
        bool condx = !!(ii.arg1 & IARG_COND_COND);
        bool condy = !var_cmpz(v);
        if (condx == condy) {
                fr->ppii += ii.arg2;
                if (!!(ii.arg1 & IARG_COND_SAVEF)) {
                        push(fr, v);
                        VAR_INCR_REF(v);
                }
        }
        VAR_DECR_REF(v);
        return RES_OK;
}

static int
do_b(Frame *fr, instruction_t ii)
{
        fr->ppii += ii.arg2;
        return 0;
}

static int
do_throw(Frame *fr, instruction_t ii)
{
        Object *exc = pop(fr);
        err_set_from_user(exc);
        return RES_EXCEPTION;
}

static int
do_unpack(Frame *fr, instruction_t ii)
{
        Object *sq = pop(fr);
        Object *it, *x;
        unsigned short count;

        count = ii.arg2 & 0x7fffu;
        it = iterator_get(sq);
        if (!it) {
                err_iterable(sq, NULL);
                goto cant;
        }
        if (seqvar_size(sq) != count) {
                err_setstr(ValueError,
                           "expected %d items to unpack but got %d",
                           (int)count, (int)seqvar_size(sq));
                VAR_DECR_REF(it);
                goto cant;
        }

        VAR_DECR_REF(sq);

        for (x = iterator_next(it); x; x = iterator_next(it)) {
                /* keep reference pass it on to stack */
                push(fr, x);
                count--;
        }
        bug_on(count != 0);
        VAR_DECR_REF(it);
        return 0;

cant:
        VAR_DECR_REF(sq);
        return RES_ERROR;
}

static int
do_unpack_special(Frame *fr, instruction_t ii)
{
        Object *sq = pop(fr);
        Object *it, *x, *staro;
        unsigned short count, i, stari, nstar;

        count = ii.arg2 & 0xffu;
        stari = (ii.arg2 >> 8) & 0xffu;
        bug_on(stari >= count);
        it = iterator_get(sq);
        if (!it) {
                err_iterable(sq, NULL);
                goto cant;
        }
        nstar = seqvar_size(sq) - (count - 1);
        if (seqvar_size(sq) < count-1 || (short)nstar < 0) {
                err_setstr(ValueError,
                        "expected at least %d items to unpack but got %d",
                        (int)count, (int)seqvar_size(sq)-1);
                VAR_DECR_REF(it);
                goto cant;
        }
        VAR_DECR_REF(sq);

        for (i = 0; i < stari; i++) {
                x = iterator_next(it);
                bug_on(!x);
                push(fr, x);
        }
        staro = arrayvar_new(nstar);
        for (i = 0; i < nstar; i++) {
                x = iterator_next(it);
                bug_on(!x);
                array_setitem(staro, i, x);
                VAR_DECR_REF(x);
        }
        push(fr, staro);
        for (i = stari + 1; i < count; i++) {
                x = iterator_next(it);
                bug_on(!x);
                push(fr, x);
        }

        VAR_DECR_REF(it);
        return 0;
cant:
        VAR_DECR_REF(sq);
        return RES_ERROR;
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
do_cmp(Frame *fr, instruction_t ii)
{
        Object *rval, *lval, *res;
        int cmp;

        rval = pop(fr);
        lval = pop(fr);
        cmp = var_compare_iarg(lval, rval, ii.arg1);
        res = cmp ? VAR_NEW_REF(gbl.one) : VAR_NEW_REF(gbl.zero);
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
        return binary_op_common(fr, var_logical_or);
}

static int
do_logical_and(Frame *fr, instruction_t ii)
{
        return binary_op_common(fr, var_logical_and);
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

#if DBUG_CHECK_GHOST_ERRORS
static void
check_ghost_errors(int res)
{
        bool e = err_occurred();

        if (e && res == RES_OK)
                DBUG1("Ghost error slipped by");
        if (!e && res != RES_OK && res != RES_RETURN)
                DBUG1("Error return but none reported");
}
#else
# define check_ghost_errors(x_) do { (void)0; } while (0)
#endif

Object *
execute_loop(Frame *fr)
{
        Object *retval;

        RECURSION_DECLARE_FUNC();
        RECURSION_START_FUNC(RECURSION_MAX);

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
        retval = VAR_NEW_REF(NullVar);

out:
        RECURSION_END_FUNC();
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
        Object *func, *ret, *args;

        bug_on(!isvar_xptr(top_level));
        func = funcvar_new_user(top_level);
        args = arrayvar_new(0);

        /* XXX: DRY violation with do_import */
        function_setattr(func, IARG_FUNC_MINARGS, 2);
        function_setattr(func, IARG_FUNC_MAXARGS, 2);
        function_setattr(func, IARG_FUNC_OPTIND, 0);
        function_setattr(func, IARG_FUNC_KWIND, 1);

        ret = vm_exec_func(fr_old, func, args, NULL);
        VAR_DECR_REF(func);
        VAR_DECR_REF(args);
        return ret;
}

/**
 * vm_exec_func - Call a function--user-defined or internal--from a builtin
 *              callback
 * @fr_old:     Frame we're currently in
 * @func:       Function to call
 * @arc:        Number of arguments being passed to the function
 * @argv:       Array of arguments
 * @kwargs:     Dictionary of keyword args, which may be NULL.
 *
 * Return: Return value of function being called or ErrorVar if execution
 *         failed.
 *
 * Note: This has a net-zero effect on reference counters for @argv,
 *       although they will temporarily be incremented.
 *       @kwargs's reference will be consumed, if it exists.
 */
Object *
vm_exec_func(Frame *fr_old, Object *func, Object *args, Object *kwargs)
{
        Frame *fr;
        Object *res, *owner;

        if (isvar_class(func)) {
                return instancevar_new(func);
        } else if (isvar_method(func)) {
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

        fr = vmframe_alloc(func, owner, fr_old);
        res = function_call(fr, args, kwargs);
        vmframe_free(fr);

        if (!res)
                res = VAR_NEW_REF(NullVar);

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
        /* cfile_init_vm should have been called first */
        int res;
        bug_on(!isvar_string(name));
        /*
         * Hack alert!! This could get called during early init
         * before cfile_init_vm has been called, but we need it
         * anyway.
         */
        if (!vm.globals)
                vm.globals = dictvar_new();
        res = dict_setitem_exclusive(vm.globals, name, var);
        bug_on(res != RES_OK);
        (void)res;
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
        val = dict_getitem(vm.globals, key);
        if (val)
                VAR_DECR_REF(val);
        return val != NULL;
}

/* used by function.c to ensure that arguments can fit on stack */
bool
vm_pointers_in_stack(Object **start, Object **end)
{
        return start <= end && start >= vm.stack && end < vm.stack_end;
}

/**
 * vm_clear_frames_for_exit - used by UAPI exit() to clear all frames.
 *
 * Calling code had better call exit() before returning or all hell will
 * break loose.
 *
 * Note: this needs to be done before any cfile_deinitXXX() functions,
 * because we may actually reenter the VM to call some UAPI cleanups.
 * It should not have to be called during a normal end-of-file exit.
 *
 * Note: Some memory "leaks" have occurred.  A function might produce a
 * reference, call vm_exec_func(), then consume the reference when
 * vm_exec_func() returns.  So if the function being executed happens to
 * be the UAPI exit, those references are on the C stack somewhere,
 * without any breadcrumbs trail for us to free them.  I just don't care.
 * These aren't true leaks, because they're accounted for when exiting
 * due to normal end-of-input.  That's what DBUG_REPORT_VARS_ON_EXIT was
 * meant to troubleshoot, so just let it report something wrong when
 * exiting early.
 */
void
vm_clear_frames_for_exit(void)
{
        var_lock();
        {
                Frame *fr, *top;
                Object *ex = NULL;

                if ((top = vm_top_frame()) != NULL)
                        ex = (Object *)top->ex;

                while ((fr = vm_current_frame()) != NULL) {
                        Object *func = fr->func;
                        Object *owner = fr->owner;
                        vmframe_free(fr);

                        /*
                         * Hack alert! I just happen to know that some
                         * of these need to be consumed again, commented
                         * where normally consumed.
                         */

                        /* Normally done in vm_exec_func() */
                        if (func)
                                VAR_DECR_REF(func);

                        /* Normally done in vm_exec_func() */
                        if (owner)
                                VAR_DECR_REF(owner);

                }

                /* Normally done in main.c */
                if (ex)
                        VAR_DECR_REF(ex);
        }
        if (DBUG_REPORT_VARS_ON_EXIT)
                DBUG1("exiting early, \"#vars outstaning\" info below will be incorrect");

        var_unlock();

        /*
         * If above var_unlock() triggers any UAPI cleanups, they SHOULD
         * allocate and then just free frames again, so there SHOULD be
         * no need to wrap all this in a "while (active frames not empty)"
         * loop.
         */
}

/*
 * XXX: vm namespace is clean enough.  Any reason we can't put
 * vm in gbl and avoid having functions like this?
 */
Object *
vm_localdict(void)
{
        /* if requested, then it needs to exist */
        if (!vm.locals)
                vm.locals = dictvar_new();
        return vm.locals;
}

void
cfile_init_vm(void)
{
        if (!vm.globals)
                vm.globals = dictvar_new();

        if (!vm.free_frames.next)
                list_init(&vm.free_frames);
        if (!vm.active_frames.next)
                list_init(&vm.active_frames);

        vm.stack = emalloc(sizeof(Object *) * VM_STACK_SIZE);
        vm.stack_end = vm.stack + VM_STACK_SIZE - 1;
}

void
cfile_deinit_vm(void)
{
        struct list_t *li, *tmp;

        if (vm.globals)
                VAR_DECR_REF(vm.globals);
        if (vm.locals)
                VAR_DECR_REF(vm.locals);

        efree(vm.stack);

        /* For-real-this-time free the VM frames */
        list_foreach_safe(li, tmp, &vm.free_frames) {
                Frame *fr = container_of(li, Frame, alloc_list);
                list_remove(li);
                efree(fr);
        }
        vm.globals = NULL;
        vm.stack = vm.stack_end = NULL;
}


