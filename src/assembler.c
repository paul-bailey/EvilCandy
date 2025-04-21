/*
 * 2025 update... I've been reading Aho/Ullman and now I see just how
 * hillbilly this file is.  Please don't look at it, it's embarrassing.
 *
 * FIXME: This whole file!  Because it doesn't separate the parsing phase
 * from the code-generation phase, not a single optimization can be made.
 * No loop invariants, no reduction of computations when constants are
 * used, none of that.
 *
 * The entry point is assemble()
 *
 * XXX  REVISIT: when using gcc with -O3, this file
 *      takes forever and a day to compile.  Is it ME?
 *      Do I need to rethink all the recursion, or should I trust
 *      that gcc is taking its time to make things awesome?
 *
 * I use a recursive descent parser.
 * For an expression like
 *              let a = (x + y.z() * 2.0);
 * the parser's entry point is assemble_expression().
 * The part to the right of the "="
 *              (x + y.z() * 2.0)
 * is evaluated starting at assemble_eval().  Since this is what can be
 * evaluated and reduced to a single datum during runtime, it's what the
 * documentation refers to as VALUE.
 */
#include <evilcandy.h>
#include <token.h>
#include <xptr.h>
#include <typedefs.h>
#include <setjmp.h>

/*
 * The @flags arg used in some of the functions below.
 * @FE_FOR: We're in that middle part of a for loop between two
 *          semicolons.  Only used by assembler.
 * There used to be more, but they went obsolete.
 */
enum { FE_FOR = 0x01 };

/* TODO: define instr_t and use sizeof() here */
#define INSTR_SIZE      sizeof(instruction_t)
#define DATA_ALIGN_SIZE 8

#define PAD_ALIGN(x) \
        (DATA_ALIGN_SIZE - (((x) * INSTR_SIZE) & (DATA_ALIGN_SIZE-1)))

#define as_assert_array_pos(a, idx, arr, alloc_bytes) do { \
        if (assert_array_pos(idx, (void **)arr, \
                             alloc_bytes, sizeof(**arr)) < 0) { \
                as_err(a, AE_OVERFLOW); \
        } \
} while (0)

#define as_err(a, e) longjmp((a)->env, e)
#define as_err_if(a, cond, e) \
        do { if (cond) as_err(a, e); } while (0)
#define as_badeof(a) do { \
        DBUG("Bad EOF, trapped in assembly.c line %d", __LINE__); \
        err_setstr(ParserError, "Unexpected termination"); \
        as_err(a, AE_GEN); \
} while (0)

#define list2frame(li) container_of(li, struct as_frame_t, list)

enum {
        AE_GEN = 1,
        AE_BADTOK,
        AE_EXPECT,
        AE_OVERFLOW,

        AE_PAR,
        AE_LAMBDA,
        AE_BRACK,
        AE_BRACE,

        AE_PARSER,
};

enum {
        NFRAME     = 32,
};

enum {
        FUNC_INIT = 1,
        JMP_INIT = 0,
};

/**
 * struct as_frame_t - Temporary frame during assembly
 * @funcno:     Temporary magic number identifying this during the first
 *              pass before jump labels are resolved.
 * @symtab:     Symbol table of stack variables.  The final instruction
 *              will not need this and use offsets from the stack
 *              instead.
 * @fp:         Pointer into @symtab defining current scope
 * @sp:         Pointer to current top of @symtab
 * @argv:       Symbol table of argument names, in order of argument
 * @argc:       Pointer to current top of @argv
 * @clo:        Symbol table of closure names
 * @cp:         Pointer to current top of @clo
 * @scope:      Current {...} scope within the function
 * @nest:       Pointer to current top of @scope
 * @list:       Link to sibling frames
 * @x:          Executable code being built up by this assembler.
 *
 * This wraps @x (the true intended result of this assembly, and will
 * be thrown away when we're done, leaving only @x remaining.
 *
 * One of these frames is allocated for each function, and one for the
 * top-levle script.  Internal scope (if, while, anything in a {...}
 * block) is managed by scope[].
 */
struct as_frame_t {
        int funcno;
        char *symtab[FRAME_STACK_MAX];
        int sp;
        int fp;
        char *argv[FRAME_ARG_MAX];
        int argc;
        char *clo[FRAME_CLOSURE_MAX];
        int cp;
        int scope[FRAME_NEST_MAX];
        int nest;
        struct list_t list;
        struct xptrvar_t *x;
};

/**
 * struct assemble_t - The top-level assembler, contains all the
 *                     function definitions in the same source file.
 * @prog:       The token state machine
 * @oc:         Pointer into current parsed token in @prog
 * @func:       Label number for next function
 * @env:        Buffer to longjmp from in case of error
 * @active_frames:
 *              Linked list of frames that have not been fully parsed.
 *              Because functions can be declared and defined in the
 *              middle of wrapper functions, this is not necessarily
 *              size one.
 * @finished:frames:
 *              Linked list of frames that have been fully parsed.
 * @fr:         Current active frame, should be last member of
 *              @active frames
 */
struct assemble_t {
        char *file_name;
        FILE *fp;
        struct token_state_t *prog;
        struct token_t *oc;
        int func;
        jmp_buf env;
        struct list_t active_frames;
        struct list_t finished_frames;
        struct as_frame_t *fr;
};

static void assemble_eval(struct assemble_t *a);
static void assemble_expression(struct assemble_t *a,
                                unsigned int flags, int skip);
static int assemble_expression_simple(struct assemble_t *a,
                                unsigned int flags, int skip);

/*
 * See comments above get_tok().
 * We cannot naively have something like
 *      old_tokptr = a->oc;
 *      as_lex();
 *      next_tokptr = a->oc;
 * because as_lex() doesn't always merely increment the pointer a->oc.
 * It might also move the token array with realloc, invalidating the old
 * pointer.  So we occasionally have to declare a local struct token_t
 * and copy a->oc's contents to it.
 *
 * @src is assumed to be a->oc, but we'll keep it general.
 */
static inline token_pos_t
as_savetok(struct assemble_t *a, struct token_t *dst)
{
        memcpy(dst, a->oc, sizeof(*dst));
        return token_get_pos(a->prog);
}

static void
as_frame_push(struct assemble_t *a, int funcno)
{
        struct as_frame_t *fr;

        fr = emalloc(sizeof(*fr));
        memset(fr, 0, sizeof(*fr));

        fr->funcno = funcno;
        fr->x = (struct xptrvar_t *)xptrvar_new(a->file_name,
                                                a->oc ? a->oc->line : 1);

        list_init(&fr->list);
        list_add_tail(&fr->list, &a->active_frames);

        a->fr = fr;
}

/*
 * This is so dirty, but we have to because we need to stuff
 * future frame with arg defs while adding instructions to old
 * frame.
 */
static void
as_frame_swap(struct assemble_t *a)
{
        struct as_frame_t *fr = a->fr;

        bug_on(list_is_empty(&a->active_frames));
        bug_on(a->active_frames.prev != &fr->list);
        bug_on(a->fr->list.next != &a->active_frames);
        bug_on(a->fr->list.prev == &a->active_frames);

        fr = list2frame(a->fr->list.prev);
        list_remove(&fr->list);

        bug_on(list_is_empty(&a->active_frames));
        list_add_tail(&fr->list, &a->active_frames);
        a->fr = fr;
}

/* conclude what you started with as_frame_take() */
static void
as_frame_restore(struct assemble_t *a, struct as_frame_t *fr)
{
        list_add_tail(&fr->list, &a->active_frames);
        a->fr = fr;
}

/* Where swap can't be used due to recursion
 * going back to child instead of grandparent
 */
static struct as_frame_t *
as_frame_take(struct assemble_t *a)
{
        struct as_frame_t *parent, *child;

        child = a->fr;
        bug_on(!child);
        parent = list2frame(child->list.prev);

        /*
         * If we're the immediate child of top-level, there's no good
         * reason to be doing this, so tell caller "no"
         */
        if (&parent->list == &a->active_frames)
                return NULL;

        list_remove(&child->list);
        a->fr = parent;

        return child;
}

/*
 * Doesn't destroy it, it just removes it from active list.
 * We'll iterate through these when we're done.
 */
static void
as_frame_pop(struct assemble_t *a)
{
        struct list_t *prev;
        struct as_frame_t *fr = a->fr;
        bug_on(list_is_empty(&a->active_frames));

        list_remove(&fr->list);
        bug_on(list_is_empty(&a->active_frames));

        prev = a->active_frames.prev;

        /*
         * first to start will be last to finish, so prepending these
         * instead of appending them will make it easier to put the entry
         * point first.
         */
        list_add_front(&fr->list, &a->finished_frames);

        a->fr = list2frame(prev);
}

/* if err, delete executable too, all that work for nothing */
static void
as_delete_frame_list(struct list_t *parent_list, int err)
{
        struct list_t *li, *tmp;
        list_foreach_safe(li, tmp, parent_list) {
                struct as_frame_t *fr = list2frame(li);
                list_remove(&fr->list);
                if (err && fr->x)
                        VAR_DECR_REF((struct var_t *)fr->x);
                efree(fr);
        }
}

static void
as_delete_frames(struct assemble_t *a, int err)
{
        as_delete_frame_list(&a->active_frames, err);
        as_delete_frame_list(&a->finished_frames, err);
}

static void
as_unlex(struct assemble_t *a)
{
        unget_tok(a->prog, &a->oc);
}

static int
as_lex(struct assemble_t *a)
{
        int ret = get_tok(a->prog, &a->oc);
        if (ret == TOKEN_ERROR)
                as_err(a, AE_PARSER);
        return ret;
}

static int
as_errlex(struct assemble_t *a, int exp)
{
        as_lex(a);
        if (a->oc->t != exp) {
                /* TODO: replace 'exp' with a string representation */
                err_setstr(ParserError,
                           "file '%s' line '%d': expected %X-class token but got '%s'",
                           a->file_name, a->oc->line, exp, a->oc->s);
                as_err(a, AE_EXPECT);
        }
        return a->oc->t;
}

static inline token_pos_t
as_swap_pos(struct assemble_t *a, token_pos_t pos)
{
        token_pos_t ret = token_swap_pos(a->prog, pos);
        as_unlex(a);
        as_lex(a);
        return ret;
}

/* check if next token is semicolon but do not take it. */
static int
peek_semi(struct assemble_t *a)
{
        int res;
        as_lex(a);
        res = a->oc->t == OC_SEMI;
        /*
         * Prevent unlex from doubling back on older token in case of
         * EOF.  Since no one calls peek_semi() unless they expect either
         * a semicolon or more input, this is an error.
         */
        if (a->oc->t == EOF)
                as_badeof(a);
        as_unlex(a);
        return res;
}

static int
symtab_seek(struct assemble_t *a, const char *s)
{
        int i;
        struct as_frame_t *fr = a->fr;
        for (i = 0; i < fr->sp; i++) {
                if (s && fr->symtab[i] && !strcmp(s, fr->symtab[i]))
                        return i;
        }
        return -1;
}

static int
arg_seek(struct assemble_t *a, const char *s)
{
        int i;
        struct as_frame_t *fr = a->fr;
        for (i = 0; i < fr->argc; i++) {
                if (s && fr->argv[i] && !strcmp(s, fr->argv[i]))
                        return i;
        }
        return -1;
}

static int
clo_seek(struct assemble_t *a, const char *s)
{
        int i;
        struct as_frame_t *fr = a->fr;
        for (i = 0; i < fr->cp; i++) {
                if (s && fr->clo[i] && !strcmp(s, fr->clo[i]))
                        return i;
        }
        return -1;
}

static void
add_instr(struct assemble_t *a, int code, int arg1, int arg2)
{
        instruction_t ii;

        bug_on((unsigned)code > 255);
        bug_on((unsigned)arg1 > 255);
        bug_on(arg2 >= 32768 || arg2 < -32768);

        ii.code = code;
        ii.arg1 = arg1;
        ii.arg2 = arg2;

        xptr_add_instr((struct var_t *)a->fr->x, ii);
}

static void
apush_scope(struct assemble_t *a)
{
        struct as_frame_t *fr = a->fr;
        if (fr->nest >= FRAME_NEST_MAX)
                as_err(a, AE_OVERFLOW);
        fr->scope[fr->nest++] = fr->fp;
        fr->fp = fr->sp;
}

static void
apop_scope(struct assemble_t *a)
{
        /* Don't add the instructions, that's implicit in POP_BLOCK */
        bug_on(a->fr->nest <= 0);
        while (a->fr->sp > a->fr->fp)
                a->fr->sp--;
        a->fr->nest--;
        a->fr->fp = a->fr->scope[a->fr->nest];
}

/*
 * The assumption here is:
 *      1. @jmp is a return value from a prev. call to as_next_label
 *      2. You are inserting this BEFORE you add the next opcode.
 * If either are untrue, all hell will break loose when the disassembly
 * begins to execute.
 */
static void
as_set_label(struct assemble_t *a, int jmp)
{
        xptr_set_label((struct var_t *)a->fr->x, jmp);
}

static int
as_next_label(struct assemble_t *a)
{
        return xptr_next_label((struct var_t *)a->fr->x);
}

/*
 * ie pointer to the execution struct of a function.
 * Different instances of functions have their own metadata,
 * but if a function was created as, perhaps, a return value
 * of another function, the *executable* part will always
 * point to this.
 */
static int
seek_or_add_const_xptr(struct assemble_t *a, struct xptrvar_t *p)
{
        return xptr_add_rodata((struct var_t *)a->fr->x,
                               (struct var_t *)p);
}

/* completes seek_or_add_const/seek_or..._int */

/* const from a token literal in the script */
static int
seek_or_add_const(struct assemble_t *a, struct token_t *oc)
{
        struct as_frame_t *fr = a->fr;
        struct xptrvar_t *x = fr->x;

        /* oc->t can only be a sort that would have filled in oc->v */
        bug_on(oc->v == NULL);
        VAR_INCR_REF(oc->v);
        return xptr_add_rodata((struct var_t *)x, oc->v);
}

static void
ainstr_load_const(struct assemble_t *a, struct token_t *oc)
{
        add_instr(a, INSTR_LOAD_CONST, 0, seek_or_add_const(a, oc));
}

/*
 * like ainstr_load_const but from an integer, not token, since
 * loading zero is common enough.
 */
static void
ainstr_load_const_int(struct assemble_t *a, long long ival)
{
        int idx = xptr_add_rodata((struct var_t *)a->fr->x,
                                  intvar_new(ival));
        add_instr(a, INSTR_LOAD_CONST, 0, idx);
}

/*
 * Make sure our assembler SP matches what the VM will see,
 * so instruction args that de-reference stack variables will
 * be correct.
 *
 * @name is name of variable being declared, or NULL if you are
 * declaring a 'ghost' variable which the user will not see,
 * eg. see assemble_foreach().
 */
static int
fakestack_declare(struct assemble_t *a, char *name)
{
        if (a->fr->sp >= FRAME_STACK_MAX)
                as_err(a, AE_OVERFLOW);

        if (name) {
                if (symtab_seek(a, name) >= 0)
                        goto redef;
                if (arg_seek(a, name) >= 0)
                        goto redef;
                if (clo_seek(a, name) >= 0)
                        goto redef;
        }

        a->fr->symtab[a->fr->sp++] = name;
        return a->fr->sp - 1;

redef:
        err_setstr(ParserError, "Redefining variable ('%s')", name);
        as_err(a, AE_GEN);
        return 0;
}

static void
assemble_function(struct assemble_t *a, bool lambda, int funcno)
{
        if (lambda) {
                /* peek if brace */
                int t = as_lex(a);
                as_unlex(a);
                if (t == OC_LBRACE) {
                        assemble_expression(a, 0, -1);
                        as_err_if(a, a->oc->t != OC_LAMBDA, AE_LAMBDA);
                } else {
                        assemble_eval(a);
                        as_lex(a);
                        as_err_if(a, a->oc->t != OC_LAMBDA, AE_LAMBDA);
                        add_instr(a, INSTR_RETURN_VALUE, 0, 0);
                        /* we know we have return so we can skip */
                        return;
                }
        } else {
                assemble_expression(a, 0, -1);
        }
        /*
         * This is often unreachable to the VM,
         * but in case expression reached end
         * without hitting "return", we need a BL.
         */
        ainstr_load_const_int(a, 0LL);
        add_instr(a, INSTR_RETURN_VALUE, 0, 0);
}

static void
assemble_funcdef(struct assemble_t *a, bool lambda)
{
        int funcno = a->func++;

        /* need to be corrected later */
        add_instr(a, INSTR_DEFFUNC, 0, funcno);
        as_errlex(a, OC_LPAR);

        as_frame_push(a, funcno);

        do {
                bool closure = false;
                bool deflt = false;
                char *name;
                as_lex(a);
                if (a->oc->t == OC_RPAR)
                        break;
                if (a->oc->t == OC_COLON) {
                        closure = true;
                        as_lex(a);
                }
                if (a->oc->t != 'u') {
                        err_setstr(ParserError,
                                "Function argument is not an identifier");
                        as_err(a, AE_GEN);
                }
                name = a->oc->s;
                as_lex(a);
                if (a->oc->t == OC_EQ) {
                        deflt = true;

                        as_frame_swap(a);
                        assemble_eval(a);
                        as_frame_swap(a);

                        as_lex(a);
                }
                if (closure) {
                        if (!deflt) {
                                err_setstr(ParserError, "Malformed argument name");
                                as_err(a, AE_GEN);
                        }

                        /* pop-pop-push, need to balance here */
                        as_frame_swap(a);
                        add_instr(a, INSTR_ADD_CLOSURE, 0, 0);
                        as_frame_swap(a);

                        as_err_if(a, a->fr->cp >= FRAME_CLOSURE_MAX, AE_OVERFLOW);
                        a->fr->clo[a->fr->cp++] = name;
                } else {
                        if (deflt) {
                                as_frame_swap(a);
                                add_instr(a, INSTR_ADD_DEFAULT, 0, a->fr->argc);
                                as_frame_swap(a);
                        }

                        as_err_if(a, a->fr->argc >= FRAME_ARG_MAX, AE_OVERFLOW);
                        a->fr->argv[a->fr->argc++] = name;
                }
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RPAR, AE_PAR);

        assemble_function(a, lambda, funcno);
        as_frame_pop(a);
}

static void
assemble_arraydef(struct assemble_t *a)
{
        int n_items = 0;

        as_lex(a);
        if (a->oc->t == OC_RBRACK) /* empty array */
                goto done;
        as_unlex(a);

        do {
                assemble_eval(a);
                as_lex(a);
                n_items++;
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RBRACK, AE_BRACK);

done:
        add_instr(a, INSTR_DEFLIST, 0, n_items);
}

static void
assemble_tupledef(struct assemble_t *a)
{
        int n_items = 0;
        as_lex(a);
        if (a->oc->t == OC_RPAR) {
                /* empty tuple */
                goto done;
        }
        as_unlex(a);

        do {
                assemble_eval(a);
                as_lex(a);
                n_items++;
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RPAR, AE_PAR);

        if (n_items == 1) {
                /* not a tuple, just something wrapped in parentheses */
                return;
        }
done:
        add_instr(a, INSTR_DEFTUPLE, 0, n_items);
}

static void
assemble_objdef(struct assemble_t *a)
{
        /* TODO: not too hard to support `set' notation here */
        add_instr(a, INSTR_DEFDICT, 0, 0);
        as_lex(a);
        if (a->oc->t == OC_RBRACE) /* empty dict */
                return;
        as_unlex(a);
        do {
                int namei;
                unsigned attrarg = 0;

                as_lex(a);
                if (a->oc->t == OC_CONST) {
                        as_lex(a);
                        attrarg = IARG_FLAG_CONST;
                }
                if (a->oc->t != 'u' && a->oc->t != 'q') {
                        err_setstr(ParserError,
                                "Dictionary key must be either an identifier or string");
                        as_err(a, AE_EXPECT);
                }
                namei = seek_or_add_const(a, a->oc);
                as_lex(a);
                if (a->oc->t != OC_COLON) {
                        err_setstr(ParserError, "Expected: ':'");
                        as_err(a, AE_EXPECT);
                }
                assemble_eval(a);
                /* REVISIT: why not just SETATTR?  */
                add_instr(a, INSTR_ADDATTR, attrarg, namei);
                as_lex(a);
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RBRACE, AE_BRACE);
}

static void assemble_eval1(struct assemble_t *a);

static void assemble_eval_atomic(struct assemble_t *a);

/*
 * helper to ainstr_load_symbol, @name is not in local namespace,
 * check enclosing function before resorting to IARG_PTR_SEEK
 */
static int
maybe_closure(struct assemble_t *a, const char *name, token_pos_t pos)
{
        /*
         * Check for closure.  When we started parsing this (child)
         * function, the parent-function parsing was at the build-a-
         * function-variable stage.  So we're able to switch back to
         * the parent to check if variable is in *its* scope...
         * evaluate it and add the command to add a closure.
         *
         * FIXME: Note the recursive nature of this.  If the variable
         * is not in the parent scope either, the call to
         * assemble_eval_atomic will call us again for the grandparent,
         * and so on until the highest-level scope that is still inside
         * a function.  That means if the closure is in, say, a great-
         * grandparent, and the parent/grandparent scopes don't use it,
         * we'd wastefully add closures to those functions as well.
         */
        struct as_frame_t *this_frame;
        bool success = false;

        this_frame = as_frame_take(a);
        if (!this_frame)
                return -1;

        if (symtab_seek(a, name) >= 0 ||
            arg_seek(a, name) >= 0 ||
            clo_seek(a, name) >= 0)  {

                pos = as_swap_pos(a, pos);
                assemble_eval_atomic(a);
                as_swap_pos(a, pos);

                /* back to identifier */
                add_instr(a, INSTR_ADD_CLOSURE, 0, 0);
                success = true;
        }

        as_frame_restore(a, this_frame);

        if (success) {
                as_err_if(a, a->fr->cp >= FRAME_CLOSURE_MAX, AE_OVERFLOW);
                a->fr->clo[a->fr->cp++] = (char *)name;
        }

        /* try this again */
        return clo_seek(a, name);
}

/*
 * ainstr_load_or_assign - common to ainstr_load_symbol and ainstr_assign
 * @instr: either INSTR_LOAD, INSTR_INCR, INSTR_DECR, or INSTR_ASSIGN(_XXX)
 * @name:  name of symbol, token assumed to be saved from a->oc already.
 */
static void
ainstr_load_or_assign(struct assemble_t *a, struct token_t *name, int instr, token_pos_t pos)
{
        int idx;
        /*
         * Note: in our implementation, FP <= AP <= SP,
         * that is, AP is the *top* of the argument stack.
         * We don't know how many args the caller actually pushed,
         * so we use AP instead of FP for our local-variable
         * de-reference, and FP for our argument de-reference.
         */
        if ((idx = symtab_seek(a, name->s)) >= 0) {
                add_instr(a, instr, IARG_PTR_AP, idx);
        } else if ((idx = arg_seek(a, name->s)) >= 0) {
                add_instr(a, instr, IARG_PTR_FP, idx);
        } else if ((idx = clo_seek(a, name->s)) >= 0) {
                add_instr(a, instr, IARG_PTR_CP, idx);
        } else if (!strcmp(name->s, "__gbl__")) {
                add_instr(a, instr, IARG_PTR_GBL, 0);
        } else if ((idx = maybe_closure(a, name->s, pos)) >= 0) {
                add_instr(a, instr, IARG_PTR_CP, idx);
        } else {
                int namei = seek_or_add_const(a, name);
                add_instr(a, instr, IARG_PTR_SEEK, namei);
        }
}

static void
ainstr_load_symbol(struct assemble_t *a, struct token_t *name, token_pos_t pos)
{
        ainstr_load_or_assign(a, name, INSTR_LOAD, pos);
}

static void
assemble_call_func(struct assemble_t *a, bool have_parent)
{
        int argc = 0;
        as_errlex(a, OC_LPAR);
        as_lex(a);

        if (a->oc->t != OC_RPAR) {
                as_unlex(a);
                do {
                        assemble_eval(a);
                        argc++;
                        as_lex(a);
                } while (a->oc->t == OC_COMMA);
        }

        as_err_if(a, a->oc->t != OC_RPAR, AE_PAR);

        /*
         * stack from top is: argn...arg1, arg0, func, parent
         * CALL_FUNC knows how much to pop based on parent arg
         */
        if (have_parent) {
                add_instr(a, INSTR_CALL_FUNC, IARG_WITH_PARENT, argc);
        } else {
                add_instr(a, INSTR_CALL_FUNC, IARG_NO_PARENT, argc);
        }
}

static void
assemble_eval_atomic(struct assemble_t *a)
{
        switch (a->oc->t) {
        case 'u': {
                struct token_t namesav;
                token_pos_t pos = as_savetok(a, &namesav);
                ainstr_load_symbol(a, &namesav, pos);
                break;
        }

        case 'i':
        case 'b':
        case 'f':
        case 'q':
        case OC_TRUE:
        case OC_FALSE:
                ainstr_load_const(a, a->oc);
                break;
        case OC_LPAR:
                assemble_tupledef(a);
                break;

        case OC_NULL:
                /*
                 * we don't need to save empty var in rodata,
                 * regular push operation pushes empty by default.
                 * This is still part of the evaluation, so no need
                 * for fakestack_declare().
                 */
                add_instr(a, INSTR_PUSH_LOCAL, 0, 0);
                break;

        case OC_FUNC:
                assemble_funcdef(a, false);
                break;
        case OC_LBRACK:
                assemble_arraydef(a);
                break;
        case OC_LBRACE:
                assemble_objdef(a);
                break;
        case OC_LAMBDA:
                assemble_funcdef(a, true);
                break;
        case OC_THIS:
                add_instr(a, INSTR_LOAD, IARG_PTR_THIS, 0);
                break;
        default:
                as_err(a, AE_BADTOK);
        }

        as_lex(a);
}

/*
 * Check for indirection: things like a.b, a['b'], a[b], a(b)...
 *
 * This part is tricky, for a number of reasons.  One, as we walk down
 * the element path of a.b.c.d..., we need to keep track of the immediate
 * parent of our current element, and for that matter keep track of
 * whether we have a parent or not.  (ie. "a(b)" instead of "a.a(b)").
 * All this is because primitive types' builtin methods do not know who
 * the parent is unless it was passed as an argument to call_func.
 *
 * Second, by function-call time, the stack will be different beneath us
 * depending on whether we have a parent or not.  So we need to include
 * this as a truth statement into the instruction for calling functions
 * (IARG_WITH_PARENT/IARG_NO_PARENT).
 *
 * Third, to keep track of the most current parent (ie. "b", not "a" when
 * we are at the "c" of "a.b.c..."), INSTR_GETATTR pushes the parent back
 * on the stack under the new attribute, therefore the stack grows by one
 * for every generation we descend.
 *
 * The best solution I could think up is to add INSTR_SHIFT_DOWN, a
 * pop-pop-push operation which effectively deletes the second-to-last
 * stack item and moves the last one down by one.  Doing this for each
 * descent ought to keep the stack properly balanced.  We use the
 * have_parent boolean to prevent us from doing this the first time.
 */
static void
assemble_eval8(struct assemble_t *a)
{
#define GETATTR_SHIFT(arg1, arg2) do {                  \
        if (have_parent)                                \
                add_instr(a, INSTR_SHIFT_DOWN, 0, 0);   \
        add_instr(a, INSTR_GETATTR, arg1, arg2);        \
        have_parent = true;                             \
} while (0)

        bool have_parent = false;

        assemble_eval_atomic(a);

        while (!!(a->oc->t & TF_INDIRECT)) {
                int namei;
                struct token_t name;

                switch (a->oc->t) {
                case OC_PER:
                        as_errlex(a, 'u');
                        namei = seek_or_add_const(a, a->oc);
                        GETATTR_SHIFT(IARG_ATTR_CONST, namei);
                        break;

                case OC_LBRACK:
                        as_lex(a);
                        switch (a->oc->t) {
                        case 'q':
                        case 'i':
                                /*
                                 * same optimization check as in
                                 * assemble_ident_helper
                                 */
                                as_savetok(a, &name);
                                if (as_lex(a) == OC_RBRACK) {
                                        namei = seek_or_add_const(a, &name);
                                        as_unlex(a);
                                        GETATTR_SHIFT(IARG_ATTR_CONST, namei);
                                        break;
                                }
                                as_unlex(a);
                                /* expression, fall through */
                        case 'u':
                        case OC_LPAR:
                                /* need to evaluate index */
                                as_unlex(a);
                                assemble_eval(a);
                                GETATTR_SHIFT(IARG_ATTR_STACK, -1);
                                break;
                        default:
                                as_err(a, AE_BADTOK);
                        }
                        as_errlex(a, OC_RBRACK);
                        break;

                case OC_LPAR:
                        /* CALL_FUNC pops twice if have_parent */
                        as_unlex(a);
                        assemble_call_func(a, have_parent);
                        have_parent = false;
                        break;
                default:
                        as_err(a, AE_BADTOK);
                }
                as_lex(a);
        }
        if (have_parent)
                add_instr(a, INSTR_SHIFT_DOWN, 0, 0);

#undef GETATTR_SHIFT
}

static void
assemble_eval7(struct assemble_t *a)
{
        if (!!(a->oc->t & TF_UNARY)) {
                int op, t = a->oc->t;
                if (t == OC_TILDE)
                        op = INSTR_BITWISE_NOT;
                else if (t == OC_MINUS)
                        op = INSTR_NEGATE;
                else if (t == OC_EXCLAIM)
                        op = INSTR_LOGICAL_NOT;
                else /* +, do nothing*/
                        op = -1;

                as_lex(a);
                assemble_eval8(a);

                if (op >= 0)
                        add_instr(a, op, 0, 0);
        } else {
                assemble_eval8(a);
        }
}

static void
assemble_eval6(struct assemble_t *a)
{
        assemble_eval7(a);
        while (!!(a->oc->t & TF_MULDIVMOD)) {
                int op;
                if (a->oc->t == OC_MUL)
                        op = INSTR_MUL;
                else if (a->oc->t == OC_DIV)
                        op = INSTR_DIV;
                else
                        op = INSTR_MOD;
                as_lex(a);
                assemble_eval7(a);
                add_instr(a, op, 0, 0);
        }
}

static void
assemble_eval5(struct assemble_t *a)
{
        assemble_eval6(a);
        while (a->oc->t == OC_PLUS || a->oc->t == OC_MINUS) {
                int op;
                if (a->oc->t == OC_PLUS)
                        op = INSTR_ADD;
                else
                        op = INSTR_SUB;
                as_lex(a);
                assemble_eval6(a);
                add_instr(a, op, 0, 0);
        }
}

static void
assemble_eval4(struct assemble_t *a)
{
        assemble_eval5(a);
        while (a->oc->t == OC_LSHIFT || a->oc->t == OC_RSHIFT) {
                int op;
                if (a->oc->t == OC_LSHIFT)
                        op = INSTR_LSHIFT;
                else
                        op = INSTR_RSHIFT;

                /* TODO: peek if we can do fast eval */
                as_lex(a);
                assemble_eval5(a);
                add_instr(a, op, 0, 0);
        }
}

static void
assemble_eval3(struct assemble_t *a)
{
        assemble_eval4(a);
        while (!!(a->oc->t & TF_RELATIONAL)) {
                int cmp;
                switch (a->oc->t) {
                case OC_EQEQ:
                        cmp = IARG_EQ;
                        break;
                case OC_LEQ:
                        cmp = IARG_LEQ;
                        break;
                case OC_GEQ:
                        cmp = IARG_GEQ;
                        break;
                case OC_NEQ:
                        cmp = IARG_NEQ;
                        break;
                case OC_LT:
                        cmp = IARG_LT;
                        break;
                case OC_GT:
                        cmp = IARG_GT;
                        break;
                default:
                        bug();
                        cmp = 0;
                }
                as_lex(a);
                assemble_eval4(a);

                add_instr(a, INSTR_CMP, cmp, 0);
        }
}

static void
assemble_eval2(struct assemble_t *a)
{
        assemble_eval3(a);
        while (!!(a->oc->t & TF_BITWISE)) {
                int op;
                if (a->oc->t == OC_AND) {
                        op = INSTR_BINARY_AND;
                } else if (a->oc->t == OC_OR) {
                        op = INSTR_BINARY_OR;
                } else {
                        op = INSTR_BINARY_XOR;
                }
                as_lex(a);
                assemble_eval3(a);
                add_instr(a, op, 0, 0);
        }
}

static void
assemble_eval1(struct assemble_t *a)
{
        assemble_eval2(a);

        while (!!(a->oc->t & TF_LOGICAL)) {
                int code = a->oc->t == OC_OROR
                        ? INSTR_LOGICAL_OR : INSTR_LOGICAL_AND;

                as_lex(a);
                assemble_eval2(a);
                add_instr(a, code, 0, 0);
        }
}

/*
 * Sister function to assemble_expression.  This and its
 * assemble_evalN descendants form a recursive-descent parser that
 * builds up the instructions for evaluating the VALUE part of an
 * expression (see big comment in assemble_expression).
 */
static void
assemble_eval(struct assemble_t *a)
{
        as_lex(a);
        assemble_eval1(a);
        as_unlex(a);
}

/* t is '+=', '/=', etc. */
static int
asgntok2instr(int t)
{
        switch (t) {
        case OC_PLUSEQ:
                return INSTR_ADD;
        case OC_MINUSEQ:
                return INSTR_SUB;
        case OC_MULEQ:
                return INSTR_MUL;
        case OC_DIVEQ:
                return INSTR_DIV;
        case OC_MODEQ:
                return INSTR_MOD;
        case OC_XOREQ:
                return INSTR_BINARY_XOR;
        case OC_LSEQ:
                return INSTR_LSHIFT;
        case OC_RSEQ:
                return INSTR_RSHIFT;
        case OC_OREQ:
                return INSTR_BINARY_OR;
        case OC_ANDEQ:
                return INSTR_BINARY_AND;
        default:
                bug();
                return 0;
        }
}

/*
 * If an assignment involves an additional operation, eg.  '+=' instead
 * of just '=', perform the operation.  Calling code will then perform
 * the assignment (SETATTR, ASSIGN, etc.)
 */
static void
assemble_preassign(struct assemble_t *a, int t)
{
        /* first check the ones that don't call assemble_eval */
        switch (t) {
        case OC_PLUSPLUS:
                ainstr_load_const_int(a, 1);
                add_instr(a, INSTR_ADD, 0, 0);
                break;

        case OC_MINUSMINUS:
                ainstr_load_const_int(a, 1);
                add_instr(a, INSTR_SUB, 0, 0);
                break;

        default:
                bug_on(t == OC_EQ || !(t & TF_ASSIGN));
                assemble_eval(a);
                add_instr(a, asgntok2instr(t), 0, 0);
        }
}

/* FIXME: huge DRY violation w/ eval8 */
/*
 * TODO: mild lift, but this should be wrapped within the eval
 * stage as well.  A line like "x = y;" should evaluate to the
 * value of "y" while also having the side-effect of assigning
 * "x".  But as-is, there is no 'value' for "x = y;", and we
 * do not even 'evaluate' until after the '='.
 */
static void
assemble_ident_helper(struct assemble_t *a)
{
        /* See comment above eval8(). We're doing the same thing here. */
        bool have_parent = false;
        int last_t = 0;

        if (peek_semi(a))
                return;

        as_lex(a);
        for (;;) {
                int namei;
                struct token_t name;

                /*
                 * FIXME: What about increment, decrement?
                 * like this, "foo.bar++" is not possible.
                 */

                /*
                 * FIXME: For the SETATTR cases below, I should permit
                 * more than '=', but any of the TF_ASSIGN tokens.
                 * Requires a new family of instructions to parallel
                 * the assignment instructions, ie.
                 *      INSTR_ASSIGN_LS  <=> INSTR_SETATTR_LS
                 * and so on..
                 */


                /*
                 * These macros sorta bury the program flow, but the code
                 * is repetitive and barely readable anyway.  I could
                 * probably simplify it greatly with some recursion, but
                 * we've recursion enough.
                 */
#define GETATTR_SHIFT(arg1, arg2)                                       \
                do {                                                    \
                        as_unlex(a);                                    \
                        if (have_parent)                                \
                                add_instr(a, INSTR_SHIFT_DOWN, 0, 0);   \
                        add_instr(a, INSTR_GETATTR, arg1, arg2);        \
                        have_parent = true;                             \
                } while (0)

#define SETATTR_SHIFT(arg1, arg2)                                       \
                do {                                                    \
                        if (t_ == OC_EQ) {                              \
                                assemble_eval(a);                       \
                        } else {                                        \
                                add_instr(a, INSTR_GETATTR, arg1, arg2); \
                                assemble_preassign(a, t_);              \
                        }                                               \
                        if (have_parent)                                \
                                add_instr(a, INSTR_SHIFT_DOWN, 0, 0);   \
                        add_instr(a, INSTR_SETATTR, arg1, arg2);        \
                } while (0)

#define SETATTR_SHIFT_IF_ASGN(arg1, arg2)                               \
                do {                                                    \
                        int t_ = as_lex(a);                             \
                        if (!!(t_ & TF_ASSIGN)) {                       \
                                SETATTR_SHIFT(arg1, arg2);              \
                                goto done;                              \
                        }                                               \
                } while (0)

#define GETSETATTR_SHIFT(arg1, arg2)                            \
                do {                                            \
                        SETATTR_SHIFT_IF_ASGN(arg1, arg2);      \
                        GETATTR_SHIFT(arg1, arg2);              \
                } while (0)

                switch (a->oc->t) {
                case OC_PER:
                        as_errlex(a, 'u');
                        namei = seek_or_add_const(a, a->oc);
                        GETSETATTR_SHIFT(IARG_ATTR_CONST, namei);
                        break;

                case OC_LBRACK:
                        as_lex(a);
                        switch (a->oc->t) {
                        case 'q':
                        case 'i':
                                /*
                                 * Try to optimize... "[" + LITERAL could
                                 * hypothetically be something weird like
                                 *
                                 *      thing["name\n".strip()]
                                 *
                                 * but 99% of the time it's just going to be
                                 *
                                 *      thing["name"]...
                                 *
                                 * So we'll see if we can avoid making the
                                 * VM evaluate this.
                                 */
                                as_savetok(a, &name);
                                if (as_lex(a) == OC_RBRACK) {
                                        /* ...the 99% scenario */
                                        namei = seek_or_add_const(a, &name);
                                        GETSETATTR_SHIFT(IARG_ATTR_CONST, namei);
                                        break;
                                }
                                as_unlex(a);
                                /* ...the 1% scenario, fall through and eval */

                        case 'u':
                        case OC_LPAR:
                                /* need to evaluate index */
                                as_unlex(a);
                                assemble_eval(a);

                                if (as_lex(a) == OC_RBRACK) {
                                        SETATTR_SHIFT_IF_ASGN(IARG_ATTR_STACK, -1);
                                        as_unlex(a);
                                }
                                GETATTR_SHIFT(IARG_ATTR_STACK, -1);
                                break;
                        default:
                                /*
                                 * XXX REVISIT:
                                 *    - I don't allow 'f', because subscript must
                                 *      be an integer (if array) or string (if a
                                 *      dictionary).  A float's .toint method cannot
                                 *      be accessed from the literal expression
                                 *      itself, eg. "1.0.toint()", because the
                                 *      parser perceives a double decimal and
                                 *      throws an error.
                                 *      But I may not be so strict in the future,
                                 *      since I noticed that Python isn't.
                                 */
                                as_err(a, AE_BADTOK);
                        }
                        as_errlex(a, OC_RBRACK);
                        break;

                case OC_LPAR:
                        as_unlex(a);
                        assemble_call_func(a, have_parent);
                        /* we're not assigning anything */
                        /*
                         * FIXME: pop & no 'have parent' should be only
                         * if peek_semi()==true.  Else, SHIFT_DOWN but in
                         * reverse (ugh, another new instruction), then
                         * continue iterating through this loop.
                         */
                        add_instr(a, INSTR_POP, 0, 0);
                        have_parent = false;
                        break;

                default:
                        as_err(a, AE_BADTOK);
                }

                last_t = a->oc->t;

                if (peek_semi(a)) {
                        if (last_t != OC_RPAR)
                                as_err(a, AE_BADTOK);
                        goto done;
                }

                as_lex(a);
        }

done:
        if (have_parent)
                add_instr(a, INSTR_SHIFT_DOWN, 0, 0);
        ;
#undef GETATTR_SHIFT
#undef SETATTR_SHIFT
#undef SETATTR_SHIFT_IF_ASGN
#undef GETSETATTR_SHIFT
}

static void
assemble_this(struct assemble_t *a, unsigned int flags)
{
        /*
         * Cf. assemble_identifier below.
         * We do not allow
         *      this = value...
         */
        add_instr(a, INSTR_LOAD, IARG_PTR_THIS, 0);
        assemble_ident_helper(a);
}

static void
assemble_identifier(struct assemble_t *a, unsigned int flags)
{
        struct token_t name;
        token_pos_t pos = as_savetok(a, &name);

        /* need to peek */
        as_lex(a);
        if (a->oc->t == OC_EQ) {
                /*
                 * x = value;
                 * Don't load, INSTR_ASSIGN knows where from frame
                 * pointer to store 'value'
                 */
                assemble_eval(a);
                ainstr_load_or_assign(a, &name, INSTR_ASSIGN, pos);
        } else if (!!(a->oc->t & TF_ASSIGN)) {
                /*
                 * x++;
                 * x += value;
                 * ...
                 */
                ainstr_load_symbol(a, &name, pos);
                assemble_preassign(a, a->oc->t);
                ainstr_load_or_assign(a, &name, INSTR_ASSIGN, pos);
        } else {
                /*
                 * x(args);
                 * x[i] [= value];
                 * x.big(damn)[mess].of.stuff...
                 * ...
                 * Here we are not modifying x directly.  We are either
                 * calling a function or modifying one of x's descendants.
                 */
                as_unlex(a);
                ainstr_load_symbol(a, &name, pos);
                assemble_ident_helper(a);
        }
}

/* commont to assemble_declarator_stmt and assemble_foreach */
static int
assemble_declare(struct assemble_t *a, struct token_t *name, bool global)
{
        int namei;
        bug_on(global && name == NULL);
        if (global) {
                namei = seek_or_add_const(a, name);
                add_instr(a, INSTR_SYMTAB, 0, namei);
        } else {
                namei = fakestack_declare(a, name ? name->s : NULL);
                add_instr(a, INSTR_PUSH_LOCAL, 0, 0);
        }
        return namei;
}

static void
assemble_declarator_stmt(struct assemble_t *a, int tok, unsigned int flags)
{
        struct token_t name;
        token_pos_t pos;
        int namei;

        if (!!(flags & FE_FOR)) {
                char *what = tok == OC_LET ? "let" : "global";
                err_setstr(ParserError,
                        "'%s' not allowed as third part of 'for' statement",
                        what);
                as_err(a, AE_BADTOK);
        }

        as_lex(a);
        if (a->oc->t != 'u') {
                char *what = tok == OC_LET ? "let" : "global";
                err_setstr(ParserError,
                           "'%s' must be followed by an identifier", what);
                as_err(a, AE_EXPECT);
        }
        pos = as_savetok(a, &name);
        namei = assemble_declare(a, &name, tok == OC_GBL);

        /* if no assign, return early */
        if (peek_semi(a))
                return;

        /* for initializers, only '=', not '+=' or such */
        as_errlex(a, OC_EQ);

        /* XXX: is the extra LOAD/POP necessary? */
        ainstr_load_symbol(a, &name, pos);
        assemble_eval(a);
        add_instr(a, INSTR_ASSIGN,
                  tok == OC_LET ? IARG_PTR_AP : IARG_PTR_SEEK, namei);
        add_instr(a, INSTR_POP, 0, 0);
}

static void
assemble_return(struct assemble_t *a)
{
        if (peek_semi(a)) {
                ainstr_load_const_int(a, 0);
                add_instr(a, INSTR_RETURN_VALUE, 0, 0);
        } else {
                assemble_eval(a);
                add_instr(a, INSTR_RETURN_VALUE, 0, 0);
        }
}

/* skip provided, because 'break' goes outside 'if' scope */
static void
assemble_if(struct assemble_t *a, int skip)
{
        int true_jmpend = as_next_label(a);
        int jmpelse = as_next_label(a);
        /*
         * The 'if' of 'else if' is technically the start of its own
         * expression, so we could do this recursively and more simply,
         * but let's instead be friendlier to the stack.
         */
        while (a->oc->t == OC_IF) {
                int jmpend = as_next_label(a);
                assemble_eval(a);
                add_instr(a, INSTR_B_IF, 0, jmpelse);
                assemble_expression(a, 0, skip);
                add_instr(a, INSTR_B, 0, true_jmpend);
                as_set_label(a, jmpelse);

                as_lex(a);
                if (a->oc->t == OC_ELSE) {
                        jmpelse = jmpend;
                        as_lex(a);
                } else {
                        as_unlex(a);
                        as_set_label(a, jmpend);
                        goto done;
                }
        }

        /* final else */
        as_unlex(a);
        as_set_label(a, jmpelse);
        assemble_expression(a, 0, skip);

done:
        as_set_label(a, true_jmpend);
}

static void
assemble_while(struct assemble_t *a)
{
        int start = as_next_label(a);
        int skip  = as_next_label(a);

        add_instr(a, INSTR_PUSH_BLOCK, IARG_LOOP, 0);
        apush_scope(a);

        as_set_label(a, start);

        as_errlex(a, OC_LPAR);
        assemble_eval(a);
        as_errlex(a, OC_RPAR);

        add_instr(a, INSTR_B_IF, 0, skip);
        assemble_expression(a, 0, skip);
        add_instr(a, INSTR_B, 0, start);

        apop_scope(a);
        add_instr(a, INSTR_POP_BLOCK, 0, 0);

        as_set_label(a, skip);
}

static void
assemble_do(struct assemble_t *a)
{
        int start = as_next_label(a);
        int skip  = as_next_label(a);

        add_instr(a, INSTR_PUSH_BLOCK, IARG_LOOP, 0);
        apush_scope(a);

        as_set_label(a, start);
        assemble_expression(a, 0, skip);
        as_errlex(a, OC_WHILE);
        assemble_eval(a);
        add_instr(a, INSTR_B_IF, 1, start);

        apop_scope(a);
        add_instr(a, INSTR_POP_BLOCK, 0, 0);

        as_set_label(a, skip);
}

static void
assemble_foreach(struct assemble_t *a)
{
        struct token_t needletok;
        int skip    = as_next_label(a);
        int iter    = as_next_label(a);

        apush_scope(a);

        /* save name of the 'needle' in 'for(needle, haystack)' */
        as_errlex(a, 'u');
        as_savetok(a, &needletok);

        as_errlex(a, OC_COMMA);

        /* declare 'needle', push placeholder onto the stack */
        assemble_declare(a, &needletok, false);

        /* push 'haystack' onto the stack */
        assemble_eval(a);
        as_errlex(a, OC_RPAR);
        fakestack_declare(a, NULL);

        /* maybe replace 'haystack' with its keys */
        add_instr(a, INSTR_FOREACH_SETUP, 0, 0);

        /* push 'i' iterator onto the stack beginning at zero */
        ainstr_load_const_int(a, 0LL);
        fakestack_declare(a, NULL);

        add_instr(a, INSTR_PUSH_BLOCK, IARG_LOOP, 0);

        as_set_label(a, iter);
        add_instr(a, INSTR_FOREACH_ITER, 0, skip);
        assemble_expression(a, 0, skip);
        add_instr(a, INSTR_B, 0, iter);

        add_instr(a, INSTR_POP_BLOCK, 0, 0);
        apop_scope(a);

        as_set_label(a, skip);
}

/*
 * skip_else here is for the unusual case if break is encountered inside
 * in the `else' of a `for...else' block, otherwise it isn't used.
 */
static void
assemble_for_cstyle(struct assemble_t *a, int skip_else)
{
        int start   = as_next_label(a);
        int then    = as_next_label(a);
        int skip    = as_next_label(a);
        int iter    = as_next_label(a);
        int forelse = as_next_label(a);

        add_instr(a, INSTR_PUSH_BLOCK, IARG_LOOP, 0);
        apush_scope(a);

        /* initializer */
        assemble_expression(a, 0, skip);

        as_set_label(a, start);
        as_lex(a);
        if (a->oc->t == EOF) {
                as_badeof(a);
        } else if (a->oc->t == OC_SEMI) {
                /* empty condition, always true */
                add_instr(a, INSTR_B, 0, then);
        } else {
                as_unlex(a);
                assemble_eval(a);
                as_errlex(a, OC_SEMI);
                add_instr(a, INSTR_B_IF, 0, forelse);
                add_instr(a, INSTR_B, 0, then);
        }
        as_set_label(a, iter);
        if (!assemble_expression_simple(a, FE_FOR, -1)) {
                /*
                 * user tried to make a {...} compound statement
                 * instead of something simple like 'i++'.  I'm not
                 * sure if we should allow this or not, but I'm also
                 * not sure if I can parse that properly.
                 */
                as_err(a, AE_BADTOK);
        }
        as_errlex(a, OC_RPAR);

        add_instr(a, INSTR_B, 0, start);
        as_set_label(a, then);
        assemble_expression(a, 0, skip);
        add_instr(a, INSTR_B, 0, iter);

        as_set_label(a, forelse);

        as_lex(a);
        if (a->oc->t == EOF) {
                as_badeof(a);
        } else if (a->oc->t == OC_ELSE) {
                assemble_expression(a, 0, skip_else);
        } else {
                as_unlex(a);
        }

        add_instr(a, INSTR_POP_BLOCK, 0, 0);
        apop_scope(a);

        as_set_label(a, skip);
}

static void
assemble_for(struct assemble_t *a, int skip_else)
{
        /* do some peeking to see which kind of 'for'
         * statement this is.
         */
        as_errlex(a, OC_LPAR);
        as_lex(a);
        if (a->oc->t == 'u') {
                as_lex(a);
                if (a->oc->t == OC_COMMA) {
                        /*
                         * for ( identifier , ...
                         *      it's the Python-like for loop
                         */
                        as_unlex(a);
                        as_unlex(a);
                        assemble_foreach(a);
                        return;
                }
                as_unlex(a);
        }
        as_unlex(a);

        /*
         * for ( ???...
         *      it's the C-style for loop
         */
        assemble_for_cstyle(a, skip_else);
}

/*
 * parse the stmt of 'stmt' + ';'
 * return true if semicolon expected, false if not (because
 * we recursed into a '{...}' statement which requires no semicolon).
 */
static int
assemble_expression_simple(struct assemble_t *a, unsigned int flags, int skip)
{
        as_lex(a);
        switch (a->oc->t) {
        case EOF:
                if (skip >= 0)
                        as_badeof(a);
                return 0;
        case 'u':
                assemble_identifier(a, flags);
                break;
        case OC_THIS:
                /* not a saucy challenge */
                assemble_this(a, flags);
                break;
        case OC_SEMI:
                /* empty statement */
                as_unlex(a);
                break;
        case OC_LPAR:
                /*
                 * value expression, eg. '(x + y)' or more likely an IIFE
                 * '(function() {...})'.  In both cases I evaluate the
                 * statement and throw away the result. In the IIFE case,
                 * it will likeyly be followed by an additional '(args)',
                 * but eval8 will parse that within the assemble_eval()
                 * call below, resulting in whatever side effect the
                 * function has, while in the former case, the programmer
                 * just wasted time.  Allow it, maybe a line like '(1);'
                 * could come in handy to someone as a sort of NOP, in
                 * ways that the empty ';' statement above will not.
                 */
                as_unlex(a);
                assemble_eval(a);
                /* throw result away */
                add_instr(a, INSTR_POP, 0, 0);
                break;
        case OC_RPAR:
                if (!(flags & FE_FOR)) {
                        as_err(a, AE_PAR);
                }
                as_unlex(a);
                break;
        case OC_LET:
        case OC_GBL:
                assemble_declarator_stmt(a, a->oc->t, flags);
                break;
        case OC_RETURN:
                assemble_return(a);
                break;
        case OC_BREAK:
                if (skip < 0) {
                        err_setstr(ParserError, "Unexpected break");
                        as_err(a, AE_GEN);
                }
                add_instr(a, INSTR_BREAK, 0, 0);
                add_instr(a, INSTR_B, 0, skip);
                break;
        case OC_IF:
                assemble_if(a, skip);
                return 0;
        case OC_WHILE:
                assemble_while(a);
                return 0;
        case OC_FOR:
                assemble_for(a, skip);
                return 0;
        case OC_LBRACE:
                as_unlex(a);
                assemble_expression(a, flags, skip);
                return 0;
        case OC_DO:
                assemble_do(a);
                return 0;
        default:
                DBUG("Got token %X ('%s')\n", a->oc->t, a->oc->s);
                as_err(a, AE_BADTOK);
        }
        return 1;
}

static int as_recursion = 0;

#define AS_RECURSION_MAX RECURSION_MAX

#define AS_RECURSION_INCR() do { \
        if (as_recursion >= AS_RECURSION_MAX) \
                fail("Recursion overflow"); \
        as_recursion++; \
} while (0)

#define AS_RECURSION_DECR() do { \
        bug_on(as_recursion <= 0); \
        as_recursion--; \
} while (0)

/*
 * assemble_expression - Parser for the top-level expresison
 * @flags: If FE_FOR, we're in the iterator part of a for loop header.
 * @skip: Jump label to add B instruction for in case of 'break'
 *
 * This covers block expressions and single-line expressions
 *
 *      single-line expr:       EXPR ';'
 *      block:                  '{' EXPR EXPR ... '}'
 *
 * Valid single-line expressions are
 *
 * #1   empty declaration:      let IDENTIFIER
 * #2   assignmment:            IDENTIFIER '=' VALUE
 * #3   decl. + assign:         let IDENTIFER '=' VALUE
 * #4   limited eval:           IDENTIFIER '(' ARGS... ')'
 * #5     ""     "" :           '(' VALUE ')'
 * #6   emtpy expr:             IDENTIFER
 * #7   program flow:           if '(' VALUE ')' EXPR
 * #8     ""     "" :           if '(' VALUE ')' EXPR else EXPR
 * #9     ""     "" :           while '(' VALUE ')' EXPR
 * #10    ""     "" :           do EXPR while '(' VALUE ')'
 * #11    ""     "":            for '(' EXPR... ')' EXPR
 * #12  return nothing:         return
 * #13  return something:       return VALUE
 * #10  break:                  break
 * #11  load:                   load
 * #12  nothing:
 *
 * TODO: #13 exception          try '(' IDENTIFIER ',' VALUE* ')' EXPR
 *              handling:                   catch '(' IDENTIFIER ')' EXPR
 *
 * See Documentation.rst for the details.
 */
static void
assemble_expression(struct assemble_t *a, unsigned int flags, int skip)
{
        AS_RECURSION_INCR();

        as_lex(a);
        if (a->oc->t == OC_LBRACE) {
                /* compound statement */
                apush_scope(a);
                add_instr(a, INSTR_PUSH_BLOCK, IARG_BLOCK, 0);

                for (;;) {
                        int exp;

                        /* peek for end of compound statement */
                        as_lex(a);
                        if (a->oc->t == OC_RBRACE)
                                break;
                        as_unlex(a);

                        exp = assemble_expression_simple(a, flags, skip);
                        if (!exp) {
                                if (a->oc->t == EOF)
                                        as_badeof(a);
                                continue;
                        }

                        as_lex(a);
                        if (a->oc->t != OC_SEMI) {
                                err_setstr(ParserError,
                                           "Expected ';' but got '%s'", a->oc->s);
                                as_err(a, AE_BADTOK);
                        }
                }
                add_instr(a, INSTR_POP_BLOCK, 0, 0);
                apop_scope(a);
        } else if (a->oc->t != EOF) {
                /* single line statement */
                int exp;

                as_unlex(a);
                exp = assemble_expression_simple(a, flags, skip);
                if (exp)
                        as_errlex(a, OC_SEMI);
        }

        AS_RECURSION_DECR();
}

/*
 * FIXME: This and resolve_jump_labels are all
 * tangled up with xptr.c code.
 */
static void
resolve_func_label(struct assemble_t *a,
                   struct as_frame_t *fr,
                   instruction_t *ii)
{
        struct list_t *li;
        bug_on(ii->arg2 == fr->funcno);
        list_foreach(li, &a->finished_frames) {
                struct as_frame_t *sib = list2frame(li);
                if (sib == fr)
                        continue;
                if (sib->funcno == ii->arg2) {
                        ii->arg2 = seek_or_add_const_xptr(a, sib->x);
                        return;
                }
        }
        bug();
}

static void
resolve_jump_labels(struct assemble_t *a, struct as_frame_t *fr)
{
        int i, n = fr->x->n_instr;
        struct as_frame_t *frsav = a->fr;
        a->fr = fr;
        for (i = 0; i < n; i++) {
                instruction_t *ii = &fr->x->instr[i];
                if (ii->code == INSTR_B
                    || ii->code == INSTR_B_IF
                    || ii->code == INSTR_FOREACH_ITER) {
                        int arg2 = ii->arg2 - JMP_INIT;

                        bug_on(ii->arg1 != 0 && ii->arg1 != 1);
                        bug_on(arg2 >= fr->x->n_label);
                        /*
                         * minus one because pc will have already
                         * been incremented.
                         */
                        ii->arg2 = fr->x->label[arg2] - i - 1;
                        continue;
                }
                if (ii->code == INSTR_DEFFUNC)
                        resolve_func_label(a, fr, ii);
        }
        a->fr = frsav;
}

/*
 * create the instruction sequences, one for the top-level file
 * and one for each function definition
 */
static void
assemble_first_pass(struct assemble_t *a, bool toeof)
{
        do {
                assemble_expression(a, 0, -1);
        } while (toeof && a->oc->t != EOF);
        add_instr(a, INSTR_END, 0, 0);

        list_remove(&a->fr->list);
        list_add_front(&a->fr->list, &a->finished_frames);
}

/* resolve local jump addresses */
static void
assemble_second_pass(struct assemble_t *a)
{
        struct list_t *li;
        list_foreach(li, &a->finished_frames)
                resolve_jump_labels(a, list2frame(li));
}

static struct xptrvar_t *
as_top_executable(struct assemble_t *a)
{
        struct as_frame_t *fr = list2frame(a->finished_frames.next);
        /*
         * fr->x could be NULL if last call to
         * assemble_next encountered an error
         */
        bug_on(!!fr->x && &fr->list == &a->finished_frames);

        return fr->x;
}

/*
 * new_assembler - Start an assembler state machine for a new input stream
 * @source_file_name: Name of input stream, for error reporting;
 *              must not be NULL
 * @fp:         FILE handle associated with @source_file_name
 *
 * Return: Handle to the assembler state machine.  This is never NULL; an
 *      error would be thrown before returning if that's the case.
 */
static struct assemble_t *
new_assembler(const char *source_file_name, FILE *fp)
{
        struct assemble_t *a;
        struct token_state_t *prog;

        prog = token_state_new(fp, notdir(source_file_name));
        if (!prog) /* no tokens, just eof */
                return NULL;

        a = ecalloc(sizeof(*a));
        a->file_name = (char *)source_file_name;
        a->fp = fp;
        a->prog = prog;
        a->oc = NULL;
        /* don't let the first ones be zero, that looks bad */
        a->func = FUNC_INIT;
        list_init(&a->active_frames);
        list_init(&a->finished_frames);
        as_frame_push(a, 0);
        return a;
}

/**
 * free_assembler - Free an assembler state machine
 * @a:          Handle to the assembler state machine
 * @err:        Zero to keep the executable opcodes in memory,
 *              non-zero to delete those also.
 */
static void
free_assembler(struct assemble_t *a, int err)
{
        as_delete_frames(a, err);
        token_state_free(a->prog);
        efree(a);
}

/* Tell user where they screwed up */
static void
assemble_splash_error(struct assemble_t *a)
{
        char *emsg;
        struct var_t *exc;
        int col;
        char *line = NULL;

        err_get(&exc, &emsg);
        bug_on(!exc || !emsg);
        err_print(stderr, exc, emsg);
        efree(emsg);
        fprintf(stderr, "in file '%s' near line '%d'\n",
                a->file_name, a->oc->line);
        line = token_get_this_line(a->prog, &col);
        if (line) {
                fprintf(stderr, "Expected error location:\n");
                fprintf(stderr, "\t%s\t", line);
                while (col-- > 0)
                        fputc(' ', stderr);
                fprintf(stderr, "^\n");
        }
}

/**
 * assemble_next - Parse input and convert into an array of pseudo-
 *                 assembly instructions
 * @a:          Handle to the assembler state machine
 * @toeof:      `true' to parse an entire input stream.  `false' to parse
 *              a single full statement; this may contain sub-statements
 *              if, for example, it's a program flow statement or it
 *              contains a function definition.
 * @status:     stores RES_OK if all is well (ex could still be NULL if
 *              normal EOF), RES_ERROR if an assembler error occurred.
 *              This may not be NULL
 *
 * Return: Either...
 *      a) Array of executable instructions for the top-level scope,
 *         which happens to be all you need.  The instructions for any
 *         functions defined in the script exist out there in RAM
 *         somewhere, but they will be reached eventually, since they are
 *         referenced by top-level instructions.  GC will happen when the
 *         last variable referencing them is destroyed.
 *      b) NULL if @a is already at end of input
 */
static struct xptrvar_t *
assemble_next(struct assemble_t *a, bool toeof, int *status)
{
        struct xptrvar_t *ex;
        int res;

        if (a->oc && a->oc->t == EOF) {
                *status = RES_OK;
                return NULL;
        }

        if ((res = setjmp(a->env)) != 0) {
                const char *msg;

                switch (res) {
                default:
                case AE_GEN:
                        msg = "Assembly error";
                        break;
                case AE_BADTOK:
                        msg = "Invalid token";
                        break;
                case AE_EXPECT:
                        msg = "Expected token missing";
                        break;
                case AE_OVERFLOW:
                        msg = "Frame overflow";
                        break;
                case AE_PAR:
                        msg = "Unbalanced parenthesis";
                        break;
                case AE_LAMBDA:
                        msg = "Unbalanced lambda";
                        break;
                case AE_BRACK:
                        msg = "Unbalanced bracket";
                        break;
                case AE_BRACE:
                        msg = "Unbalanced brace";
                        break;
                case AE_PARSER:
                        /* Parser already set error message */
                        msg = NULL;
                        break;
                }

                if (msg && !err_occurred())
                        err_setstr(ParserError, "%s", msg);

                assemble_splash_error(a);

                /*
                 * TODO: probably more meticulous cleanup needed here,
                 * we don't know exactly where we failed.
                 */
                *status = RES_ERROR;
                ex = NULL;
        } else {
                assemble_first_pass(a, toeof);
                assemble_second_pass(a);

                *status = RES_OK;
                ex = as_top_executable(a);
        }

        return ex;
}

/**
 * assemble - Parse input and convert into byte code
 * @filename:   Name of file, for usage later by serializer and disassembler
 * @fp:         Handle to the open source file, at its starting position.
 * @toeof:      true to parse an entire input stream.
 *              false to parse a single statement.
 *              Use true for scripts and false for interactive TTY mode.
 * @status:     stores RES_OK if all is well or RES_ERROR if an assembler
 *              error occurred.
 *
 * Return: Either...
 *      a) A struct xptrvar_t which is ready for passing to the VM.
 *      b) NULL if the input is already at EOF or if there was an error
 *         (check status).
 */
struct var_t *
assemble(const char *filename, FILE *fp, bool toeof, int *status)
{
        int localstatus;
        struct xptrvar_t *ret;
        struct assemble_t *a;

        a = new_assembler(filename, fp);
        if (!a)
                return NULL;
        ret = assemble_next(a, toeof, &localstatus);

        /* status cannot be OK if ret is NULL and toeof is true */
        bug_on(toeof && ret == NULL && localstatus == RES_OK);
        bug_on(localstatus == RES_OK && err_occurred());

        if (status)
                *status = localstatus;

        free_assembler(a, localstatus == RES_ERROR);

        return (struct var_t *)ret;
}

