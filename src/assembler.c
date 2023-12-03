#include "instructions.h"
#include "egq.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

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

#define list2frame(li) container_of(li, struct as_frame_t, list)

enum {
        AE_GEN = 1,
        AE_BADEOF,
        AE_BADTOK,
        AE_EXPECT,
        AE_REDEF,
        AE_OVERFLOW,

        AE_PAR,
        AE_LAMBDA,
        AE_BRACK,
        AE_BRACE,

        AE_BREAK,
        AE_NOTIMPL,
        AE_ARGNAME,
        AE_BADINSTR,
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
 * @const_alloc: Bytes currently allocated for @x->rodata
 * @label_alloc: Bytes currently allocated for @x->label
 * @instr_alloc: Bytes currently allocated for @x->instr
 * @x:          Executable code being built up by this assembler.
 *
 * This wraps @x (the true intended result of this assembly, and will
 * be thrown away when we're done, leaving only @x remaining.
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
        size_t const_alloc;
        size_t label_alloc;
        size_t instr_alloc;
        struct executable_t *x;
};

/**
 * struct assemble_t - The top-level assembler, contains all the
 *                     function definitions in the same source file.
 * @prog:       The full array of tokens for input file
 * @oc:         Pointer into @prog
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
        struct opcode_t *prog;
        struct opcode_t *oc;
        int func;
        jmp_buf env;
        struct list_t active_frames;
        struct list_t finished_frames;
        struct as_frame_t *fr;
};

static void assemble_eval(struct assemble_t *a);
static void assemble_expression(struct assemble_t *a,
                                unsigned int flags, int skip);

static inline bool frame_is_top(struct assemble_t *a)
        { return a->active_frames.next == &a->fr->list; }

static void
as_frame_push(struct assemble_t *a, int funcno)
{
        struct as_frame_t *fr;

        fr = emalloc(sizeof(*fr));
        memset(fr, 0, sizeof(*fr));

        fr->funcno = funcno;
        fr->x = ecalloc(sizeof(*(fr->x)));
        list_init(&fr->x->list);
        fr->x->file_name = a->file_name;
        fr->x->file_line = a->oc->line;
        fr->x->n_label = JMP_INIT;

        list_init(&fr->list);
        list_add_tail(&fr->list, &a->active_frames);

        a->fr = fr;

        if (frame_is_top(a))
                fr->x->flags = FE_TOP;
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
                if (err && fr->x) {
                        struct executable_t *x = fr->x;
                        list_remove(&x->list);
                        if (x->instr)
                                free(x->instr);
                        if (x->rodata)
                                free(x->rodata);
                        if (x->label)
                                free(x->label);
                        free(fr->x);
                }
                free(fr);
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
        /* "minus one" should be fine */
        bug_on(a->oc < a->prog);
        a->oc--;
}

static int
as_lex(struct assemble_t *a)
{
        if (a->oc->t != EOF)
                a->oc++;
        return a->oc->t;
}

static int
as_errlex(struct assemble_t *a, int exp)
{
        as_lex(a);
        as_err_if(a, a->oc->t != exp, AE_EXPECT);
        return a->oc->t;
}

static int
symtab_seek(struct assemble_t *a, char *s)
{
        int i;
        struct as_frame_t *fr = a->fr;
        for (i = 0; i < fr->sp; i++) {
                if (s == (char *)fr->symtab[i])
                        return i;
        }
        return -1;
}

static int
arg_seek(struct assemble_t *a, char *s)
{
        int i;
        struct as_frame_t *fr = a->fr;
        for (i = 0; i < fr->argc; i++) {
                if (s == (char *)fr->argv[i])
                        return i;
        }
        return -1;
}

static int
clo_seek(struct assemble_t *a, char *s)
{
        int i;
        struct as_frame_t *fr = a->fr;
        for (i = 0; i < fr->cp; i++) {
                if (s == (char *)fr->clo[i])
                        return i;
        }
        return -1;
}

static void
add_instr(struct assemble_t *a, int code, int arg1, int arg2)
{
        int idx;
        struct as_frame_t *fr = a->fr;
        struct executable_t *x = fr->x;

        as_assert_array_pos(a, x->n_instr, &x->instr, &fr->instr_alloc);

        idx = x->n_instr++;
        instruction_t *ii = &x->instr[idx];

        bug_on((unsigned)code > 255);
        bug_on((unsigned)arg1 > 255);
        bug_on(arg2 >= 32768 || arg2 < -32768);

        ii->code = code;
        ii->arg1 = arg1;
        ii->arg2 = arg2;
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

/* writes instructions, doesn't update @a, use apop_scope for that */
static void
apop_scope_instruction_only(struct assemble_t *a)
{
        bug_on(a->fr->nest <= 0);
        int cur_fp = a->fr->sp;
        int prev_fp = a->fr->scope[a->fr->nest - 1];
        bug_on(cur_fp < prev_fp);
        while (cur_fp-- > prev_fp)
                add_instr(a, INSTR_POP_LOCAL, 0, 0);
}

static void
apop_scope(struct assemble_t *a)
{
        bug_on(a->fr->nest <= 0);
        while (a->fr->sp > a->fr->fp) {
                a->fr->sp--;
                add_instr(a, INSTR_POP_LOCAL, 0, 0);
        }
        a->fr->nest--;
        a->fr->fp = a->fr->scope[a->fr->nest];
}

/*
 * The assumption here is:
 *      1. @label is a return value from a prev. call to
 *         as_next_label
 *      2. You are inserting this BEFORE you add the next
 *         opcode.
 * If either are untrue, all hell will break loose when the disassembly
 * begins to execute.
 */
static void
as_set_label(struct assemble_t *a, int jmp)
{
        struct executable_t *x = a->fr->x;

        bug_on(jmp < JMP_INIT);
        bug_on(!x->label || x->n_label <= jmp);

        x->label[jmp - JMP_INIT] = x->n_instr;
}

static int
as_next_label(struct assemble_t *a)
{
        struct as_frame_t *fr = a->fr;
        struct executable_t *x = fr->x;
        as_assert_array_pos(a, x->n_label - JMP_INIT,
                            &fr->x->label, &fr->label_alloc);
        return x->n_label++;
}

/*
 * ie pointer to the execution struct of a function.
 * Different instances of functions have their own metadata,
 * but if a function was created as, perhaps, a return value
 * of another function, the *executable* part will always
 * point to this.
 */
static int
seek_or_add_const_xptr(struct assemble_t *a, void *p)
{
        int i;
        struct var_t *v;
        struct as_frame_t *fr = a->fr;
        struct executable_t *x = fr->x;
        for (i = 0; i < x->n_rodata; i++) {
                v = x->rodata[i];
                if (v->magic == Q_XPTR_MAGIC && v->xptr == p)
                        break;
        }

        if (i == x->n_rodata) {
                as_assert_array_pos(a, x->n_rodata + 1,
                                    &x->rodata, &fr->const_alloc);
                v = var_new();
                v->magic = Q_XPTR_MAGIC;
                v->xptr = p;
                x->rodata[x->n_rodata++] = v;
        }
        return i;
}

/* const from a token literal in the script */
static int
seek_or_add_const(struct assemble_t *a, struct opcode_t *oc)
{
        int i;
        struct as_frame_t *fr = a->fr;
        struct executable_t *x = fr->x;
        struct var_t *v;
        switch (oc->t) {
        case 'i':
                for (i = 0; i < x->n_rodata; i++) {
                        v = x->rodata[i];
                        if (v->magic == QINT_MAGIC && v->i == oc->i)
                                break;
                }
                break;

        case 'f':
                for (i = 0; i < x->n_rodata; i++) {
                        v = x->rodata[i];
                        if (v->magic == QFLOAT_MAGIC && v->f == oc->f)
                                break;
                }
                break;
        case 'u':
        case 'q':
                for (i = 0; i < x->n_rodata; i++) {
                        v = x->rodata[i];
                        if (v->magic == Q_STRPTR_MAGIC
                            && v->strptr == oc->s) {
                                break;
                        }
                }
                break;
        default:
                i = 0; /* satisfied, compiler? */
                bug();
        }

        if (i == x->n_rodata) {
                as_assert_array_pos(a, x->n_rodata + 1,
                                    &x->rodata, &fr->const_alloc);

                v = var_new();
                switch (oc->t) {
                case 'i':
                        qop_assign_int(v, oc->i);
                        break;
                case 'f':
                        qop_assign_float(v, oc->f);
                        break;
                case 'u':
                case 'q':
                        v->magic = Q_STRPTR_MAGIC;
                        v->strptr = oc->s;
                        break;
                }

                x->rodata[x->n_rodata++] = v;
        }
        return i;
}

static void
ainstr_push_const(struct assemble_t *a, struct opcode_t *oc)
{
        add_instr(a, INSTR_PUSH_CONST, 0, seek_or_add_const(a, oc));
}

static int
assemble_push_noinstr(struct assemble_t *a, char *name)
{
        if (a->fr->sp >= FRAME_STACK_MAX)
                as_err(a, AE_OVERFLOW);

        if (name) {
                if (symtab_seek(a, name) >= 0)
                        as_err(a, AE_REDEF);
                if (arg_seek(a, name) >= 0)
                        as_err(a, AE_REDEF);
                if (clo_seek(a, name) >= 0)
                        as_err(a, AE_REDEF);
        }

        a->fr->symtab[a->fr->sp++] = name;
        return a->fr->sp - 1;
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
        add_instr(a, INSTR_PUSH_ZERO, 0, 0);
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
                as_err_if(a, a->oc->t != 'u', AE_ARGNAME);
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
                        as_err_if(a, !deflt, AE_ARGNAME);

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
        add_instr(a, INSTR_DEFLIST, 0, 0);
        as_lex(a);
        if (a->oc->t == OC_RBRACK) /* empty array */
                return;
        as_unlex(a);

        do {
                assemble_eval(a);
                /* pop attr, pop array, addattr, push array */
                add_instr(a, INSTR_LIST_APPEND, 0, 0);
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RBRACK, AE_BRACK);
}

static void
assemble_objdef(struct assemble_t *a)
{
        add_instr(a, INSTR_DEFDICT, 0, 0);
        do {
                struct opcode_t *name;
                int namei;
                as_errlex(a, 'u');
                name = a->oc;
                namei = seek_or_add_const(a, name);
                as_errlex(a, OC_COLON);
                as_lex(a);

                /* TODO: support these */
                if (a->oc->t == OC_PRIV)
                        as_lex(a);
                if (a->oc->t == OC_CONST)
                        as_lex(a);
                as_unlex(a);
                assemble_eval(a);
                /*
                 * pop attr, pop array, addattr, push array
                 * name is always constant, the only other way to
                 * add an attribute is in function call.
                 */
                add_instr(a, INSTR_ADDATTR, 0, namei);
                as_lex(a);
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RBRACE, AE_BRACE);
}

static void assemble_eval1(struct assemble_t *a);

/*
 * @instr is INSTR_PUSH_PTR (for expression mode) and INSTR_PUSH_COPY
 * (for eval mode, where vars could be carelessly clobbered).
 */
static void
ainstr_push_symbol(struct assemble_t *a, int instr, struct opcode_t *name)
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
        } else {
                int namei = seek_or_add_const(a, name);
                add_instr(a, instr, IARG_PTR_SEEK, namei);
        }
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
        case 'u':
                ainstr_push_symbol(a, INSTR_PUSH_COPY, a->oc);
                break;

        case 'i':
        case 'f':
        case 'q':
                ainstr_push_const(a, a->oc);
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
                add_instr(a, INSTR_PUSH_COPY, IARG_PTR_THIS, 0);
                break;
        default:
                as_err(a, AE_BADTOK);
        }

        as_lex(a);
}

static void
assemble_eval9(struct assemble_t *a)
{
        if (a->oc->t == OC_LPAR) {
                as_lex(a);
                assemble_eval1(a);
                as_err_if(a, a->oc->t != OC_RPAR, AE_PAR);
                as_lex(a);
        } else {
                assemble_eval_atomic(a);
        }
}

static void
assemble_eval8(struct assemble_t *a)
{
        bool have_parent = false;
        int inbal = 0;

        assemble_eval9(a);

        while (a->oc->t == OC_PER ||
               a->oc->t == OC_LBRACK ||
               a->oc->t == OC_LPAR) {

                int namei;
                struct opcode_t *name;

                /*
                 * GETATTR 0 ... pop parent, get attribute from const,
                 *               push parent, push attribute
                 * GETATTR 1 ... pop parent, pop key, get attribute from key,
                 *               push parent, push attribute
                 *
                 *      Ether way we'll have pushed one more than are popped
                 *      For each OC_PER or OC_LBRACK iteration.
                 *      We can't just have the pops and pushes balance
                 *      like in a binary-add case, because we still need
                 *      the parent in case a function is called.
                 */

                switch (a->oc->t) {
                case OC_PER:
                        have_parent = true;
                        inbal++;
                        as_errlex(a, 'u');
                        namei = seek_or_add_const(a, a->oc);
                        add_instr(a, INSTR_GETATTR, IARG_ATTR_CONST, namei);
                        break;

                case OC_LBRACK:
                        have_parent = true;
                        inbal++;
                        as_lex(a);
                        switch (a->oc->t) {
                        case 'q':
                        case 'i':
                                name = a->oc;
                                if (as_lex(a) == OC_RBRACK) {
                                        namei = seek_or_add_const(a, name);
                                        as_unlex(a);
                                        add_instr(a, INSTR_GETATTR, IARG_ATTR_CONST, namei);
                                        break;
                                }
                                as_unlex(a);
                                /* expression, fall through */
                        case 'u':
                                /* need to evaluate index */
                                as_unlex(a);
                                assemble_eval1(a);
                                add_instr(a, INSTR_GETATTR, IARG_ATTR_STACK, -1);
                                break;
                        default:
                                as_err(a, AE_BADTOK);
                        }
                        as_errlex(a, OC_RBRACK);
                        break;

                case OC_LPAR:
                        /* CALL_FUNC pops twice if have_parent */
                        if (have_parent)
                                --inbal;
                        as_unlex(a);
                        assemble_call_func(a, have_parent);
                        have_parent = false;
                        break;
                }
                as_lex(a);
        }

        bug_on(inbal < 0);
        if (inbal)
                add_instr(a, INSTR_UNWIND, 0, inbal);
}

static void
assemble_eval7(struct assemble_t *a)
{
        int t = a->oc->t;
        if (t == OC_TILDE || t == OC_MINUS
            || t == OC_EXCLAIM || t == OC_PLUS) {
                int op;
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
        while (a->oc->t == OC_MUL
               || a->oc->t == OC_DIV
               || a->oc->t == OC_MOD) {
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
        int t;
        assemble_eval4(a);
        while ((t = a->oc->t) == OC_EQEQ
               || t == OC_LEQ
               || t == OC_GEQ
               || t == OC_NEQ
               || t == OC_LT
               || t == OC_GT) {
                int cmp;
                switch (t) {
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
        while (a->oc->t == OC_AND
               || a->oc->t == OC_OR
               || a->oc->t == OC_XOR) {
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

        if (a->oc->t == OC_OROR) {
                as_lex(a);
                assemble_eval2(a);
                add_instr(a, INSTR_LOGICAL_OR, 0, 0);
        } else if (a->oc->t == OC_ANDAND) {
                as_lex(a);
                assemble_eval2(a);
                add_instr(a, INSTR_LOGICAL_AND, 0, 0);
        }
}

static void
assemble_eval(struct assemble_t *a)
{
        as_lex(a);
        assemble_eval1(a);
        as_unlex(a);
}

static void
assemble_assign(struct assemble_t *a)
{
        assemble_eval(a);
        add_instr(a, INSTR_ASSIGN, 0, 0);
}

/* FIXME: huge DRY violation w/ eval8 */
static void
assemble_ident_helper(struct assemble_t *a, unsigned int flags)
{
        bool have_parent = false;
        int last_t = 0;
        /* see comment in assemble_eval8, same ish-- */
        int inbal = 0;

        as_lex(a);

        /*
         * check some fast-path easy cases:
         *      thing = that
         * instead of thing.child.granchild... = that
         */
        if (a->oc->t == OC_SEMI)
                return; /* empty statement? are we ok with this? */
        if (a->oc->t == OC_EQ) {
                assemble_assign(a);
                goto done;
        }

        for (;;) {
                int namei;
                struct opcode_t *name;
                switch (a->oc->t) {
                case OC_PER:
                        have_parent = true;
                        as_errlex(a, 'u');
                        namei = seek_or_add_const(a, a->oc);
                        if (as_lex(a) == OC_EQ) {
                                assemble_eval(a);
                                add_instr(a, INSTR_SETATTR, IARG_ATTR_CONST, namei);
                                goto done;
                        }

                        as_unlex(a);
                        inbal++;
                        add_instr(a, INSTR_GETATTR, IARG_ATTR_CONST, namei);
                        break;

                case OC_LBRACK:
                        have_parent = true;
                        as_lex(a);
                        switch (a->oc->t) {
                        case 'q':
                        case 'i':
                                /*
                                 * this is inelegant, but it reduces the number of
                                 * stack operations in the final assembly
                                 */
                                name = a->oc;
                                if (as_lex(a) == OC_RBRACK) {
                                        namei = seek_or_add_const(a, name);
                                        if (as_lex(a) == OC_EQ) {
                                                assemble_eval(a);
                                                add_instr(a, INSTR_SETATTR, IARG_ATTR_CONST, namei);
                                                goto done;
                                        }
                                        as_unlex(a);
                                        inbal++;
                                        add_instr(a, INSTR_GETATTR, IARG_ATTR_CONST, namei);
                                        break;
                                }
                                as_unlex(a);
                                /* fall through, we need to fully eval */

                        case 'u':
                                /* need to evaluate index */
                                as_unlex(a);
                                assemble_eval(a);

                                if (as_lex(a) == OC_RBRACK) {
                                        if (as_lex(a) == OC_EQ) {
                                                assemble_eval(a);
                                                add_instr(a, INSTR_SETATTR, IARG_ATTR_STACK, -1);
                                                goto done;
                                        }
                                        as_unlex(a);
                                }
                                as_unlex(a);

                                inbal++;
                                add_instr(a, INSTR_GETATTR, IARG_ATTR_STACK, -1);
                                break;
                        default:
                                as_err(a, AE_BADTOK);
                        }
                        as_errlex(a, OC_LBRACK);
                        break;

                case OC_LPAR:
                        if (have_parent)
                                --inbal;
                        as_unlex(a);
                        assemble_call_func(a, have_parent);
                        /* we're not assigning anything */
                        add_instr(a, INSTR_POP, 0, 0);
                        have_parent = false;
                        break;

                case OC_EQ:
                        /*
                         * Academically, "some_func_call() = x" should be
                         * valid, but it looks wrong and I won't allow it.
                         */
                        as_err_if(a, last_t == OC_RPAR, AE_BADINSTR);

                        assemble_assign(a);
                        goto done;

                case OC_SEMI:
                        /* should end in a function or an assignment */
                        if (last_t != OC_RPAR)
                                as_err(a, AE_BADTOK);
                        as_unlex(a);
                        goto done;

                case OC_PLUSPLUS:
                        add_instr(a, INSTR_INCR, 0, 0);
                        goto done;

                case OC_MINUSMINUS:
                        add_instr(a, INSTR_DECR, 0, 0);
                        goto done;

                default:
                        as_err(a, AE_BADTOK);
                }

                last_t = a->oc->t;
                as_lex(a);
        }

done:
        bug_on(inbal < 0);
        if (inbal)
                add_instr(a, INSTR_UNWIND, 0, inbal);

        if (!!(flags & FE_FOR))
                as_errlex(a, OC_RPAR);
        else
                as_errlex(a, OC_SEMI);
}

static void
assemble_this(struct assemble_t *a, unsigned int flags)
{
        add_instr(a, INSTR_PUSH_PTR, IARG_PTR_THIS, 0);
        assemble_ident_helper(a, flags);
}

static void
assemble_identifier(struct assemble_t *a, unsigned int flags)
{
        ainstr_push_symbol(a, INSTR_PUSH_PTR, a->oc);
        assemble_ident_helper(a, flags);
}

static void
assemble_let(struct assemble_t *a)
{
        struct opcode_t *name;
        bool top;
        int namei;
        as_errlex(a, 'u');
        name = a->oc;
        as_lex(a);
        if (a->oc->t == OC_CONST) {
                /* FIXME: I need to support this */
                as_lex(a);
        }

        top = frame_is_top(a);
        if (top) {
                /*
                 * For global scope, stack is for temporary evaluation
                 * only; we store 'let' variables in a symbol table.
                 */
                namei = seek_or_add_const(a, name);
                add_instr(a, INSTR_SYMTAB, 0, namei);
        } else {
                /*
                 * Function scope: we declare these by merely pushing
                 * them onto the stack.  We keep a symbol table during
                 * assembly only; we know where they lie in the stack,
                 * so we can remove the names from the instruction-set
                 * altogether, and refer to them relative to AP.
                 *
                 * XXX REVIST: Is this a good idea?  It kills our
                 * ability to add an interactive mode.
                 * (no it doesn't, interactive mode 'interacts' from the
                 * top level, see above)
                 */
                namei = assemble_push_noinstr(a, name->s);
                add_instr(a, INSTR_PUSH_LOCAL, 0, 0);
        }

        switch (a->oc->t) {
        case OC_SEMI:
                /* emtpy declaration */
                return;
        case OC_EQ:
                if (top) {
                        add_instr(a, INSTR_PUSH_PTR, IARG_PTR_SEEK, namei);
                        assemble_eval(a);
                        add_instr(a, INSTR_ASSIGN, 0, 0);
                } else {
                        add_instr(a, INSTR_PUSH_PTR, IARG_PTR_AP, namei);
                        assemble_eval(a);
                        add_instr(a, INSTR_ASSIGN, 0, 0);
                }
                as_errlex(a, OC_SEMI);
                break;
        default:
                as_err(a, AE_BADTOK);
        }
}

static void
assemble_return(struct assemble_t *a)
{
        as_lex(a);
        if (a->oc->t == OC_SEMI) {
                add_instr(a, INSTR_PUSH_ZERO, 0, 0);
                add_instr(a, INSTR_RETURN_VALUE, 0, 0);
        } else {
                as_unlex(a);
                assemble_eval(a);
                add_instr(a, INSTR_RETURN_VALUE, 0, 0);
                as_errlex(a, OC_SEMI);
        }
}

/* skip provided, because 'break' goes outside 'if' scope */
static void
assemble_if(struct assemble_t *a, int skip)
{
        /*
         * TODO: some way to peek for ||, for an early branch
         * if true.
         */
        int jmpelse = as_next_label(a);
        assemble_eval(a);
        add_instr(a, INSTR_B_IF, 0, jmpelse);
        assemble_expression(a, 0, skip);
        as_lex(a);
        if (a->oc->t == OC_ELSE) {
                as_set_label(a, jmpelse);
                assemble_expression(a, 0, skip);
        } else {
                as_set_label(a, jmpelse);
                as_unlex(a);
        }
}

static void
assemble_while(struct assemble_t *a)
{
        int start = as_next_label(a);
        int skip  = as_next_label(a);

        as_set_label(a, start);

        as_errlex(a, OC_LPAR);
        assemble_eval(a);
        as_errlex(a, OC_RPAR);

        add_instr(a, INSTR_B_IF, 0, skip);
        assemble_expression(a, 0, skip);
        add_instr(a, INSTR_B, 0, start);

        as_set_label(a, skip);
}

static void
assemble_do(struct assemble_t *a)
{
        int start = as_next_label(a);
        int skip  = as_next_label(a);

        as_set_label(a, start);
        assemble_expression(a, 0, skip);
        as_errlex(a, OC_WHILE);
        assemble_eval(a);
        add_instr(a, INSTR_B_IF, 1, start);
        as_set_label(a, skip);
}

static void
assemble_for(struct assemble_t *a)
{
        int start = as_next_label(a);
        int then = as_next_label(a);
        int skip = as_next_label(a);
        int iter = as_next_label(a);
        as_errlex(a, OC_LPAR);

        /* initializer */
        assemble_expression(a, 0, skip);

        as_set_label(a, start);
        as_lex(a);
        if (a->oc->t == OC_SEMI) {
                /* empty condition, always true */
                add_instr(a, INSTR_B, 0, then);
        } else {
                as_unlex(a);
                assemble_eval(a);
                as_errlex(a, OC_SEMI);
                add_instr(a, INSTR_B_IF, 0, skip);
                add_instr(a, INSTR_B, 0, then);
        }
        as_set_label(a, iter);
        assemble_expression(a, FE_FOR, -1);
        add_instr(a, INSTR_B, 0, start);
        as_set_label(a, then);
        assemble_expression(a, 0, skip);
        add_instr(a, INSTR_B, 0, iter);
        as_set_label(a, skip);
}

static void
assemble_load(struct assemble_t *a)
{
        /* 12/2023: VM is not reentrant yet. */
        as_err(a, AE_NOTIMPL);
}

/* flags are same here as in expression */
static void
assemble_expression(struct assemble_t *a, unsigned int flags, int skip)
{
        int brace = 0;
        bool pop = false;

        RECURSION_INCR();

        as_lex(a);
        if (a->oc->t == OC_LBRACE) {
                brace++;
                pop = true;
                apush_scope(a);
        } else {
                /* single line statement */
                as_unlex(a);
        }

        do {
                as_lex(a);
                if (a->oc->t == EOF) {
                        as_err_if(a, skip >= 0, AE_BADEOF);
                        break;
                }

                switch (a->oc->t) {
                case 'u':
                        assemble_identifier(a, flags);
                        break;
                case OC_THIS:
                        /* not a saucy challenge */
                        assemble_this(a, flags);
                        break;
                case OC_SEMI:
                        /* empty statement */
                        break;
                case OC_RBRACE:
                        as_err_if(a, !brace, AE_BRACE);
                        brace--;
                        break;
                case OC_RPAR:
                        as_err_if(a, !(flags & FE_FOR), AE_BADTOK);
                        as_err_if(a, brace, AE_BRACE);
                        as_unlex(a);
                        break;
                case OC_LPAR:
                        as_unlex(a);
                        assemble_eval(a);
                        as_errlex(a, OC_SEMI);
                        add_instr(a, INSTR_POP, 0, 0);
                        break;
                case OC_LET:
                        as_err_if(a, !!(flags & FE_FOR), AE_BADTOK);
                        assemble_let(a);
                        break;
                case OC_RETURN:
                        assemble_return(a);
                        break;
                case OC_BREAK:
                        as_err_if(a, skip < 0, AE_BREAK);
                        apop_scope_instruction_only(a);
                        add_instr(a, INSTR_B, 0, skip);
                        break;
                case OC_IF:
                        assemble_if(a, skip);
                        break;
                case OC_WHILE:
                        assemble_while(a);
                        break;
                case OC_FOR:
                        assemble_for(a);
                        break;
                case OC_DO:
                        assemble_do(a);
                        break;
                case OC_LOAD:
                        /*
                         * TODO: If we are in a function or loop statement,
                         * we can't do this.  But I do want to do this if we
                         * are in an if statement, so we can conditionally
                         * load, eg.
                         *      if (!__gbl__.haschild("thing"))
                         *              load "thing";
                         */
                        assemble_load(a);
                        break;
                default:
                        as_err(a, AE_BADTOK);
                }
        } while (brace);

        RECURSION_DECR();

        if (pop)
                apop_scope(a);
}

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
                if (ii->code == INSTR_B || ii->code == INSTR_B_IF) {
                        int arg2 = ii->arg2 - JMP_INIT;

                        bug_on(ii->arg1 != 0 && ii->arg1 != 1);
                        bug_on(arg2 >= fr->label_alloc);
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
assemble_first_pass(struct assemble_t *a)
{
        while (a->oc->t != EOF)
                assemble_expression(a, FE_TOP, -1);
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

/*
 * Since data going into executable_t won't be resized anymore,
 * ie. the pointers won't change from further reallocs, it's safe to
 * move them into their permanent struct.
 */
static void
assemble_third_pass(struct assemble_t *a)
{
        struct list_t *li;
        list_foreach(li, &a->finished_frames) {
                struct as_frame_t *fr = list2frame(li);
                struct executable_t *x = fr->x;
                if (!(x->flags & FE_TOP))
                        list_add_tail(&q_.executables, &x->list);
        }
}

#ifdef NDEBUG

static int
assemble_fourth_pass(struct assemble_t *a)
{
        if (q_.opt.disassemble)
                warning("Disassembly unavailable in release mode");
        return 0;
}

#else

static int
assemble_fourth_pass(struct assemble_t *a)
{
        FILE *fp;

        if (!q_.opt.disassemble)
                return 0;

        fp = fopen(q_.opt.disassemble_outfile, "w");
        if (!fp)
                return -1;
        /* If disassembly requested, run it */
        disassemble_start(fp, a->file_name);
        struct list_t *li;
        list_foreach(li, &a->finished_frames) {
                struct as_frame_t *fr = list2frame(li);
                struct executable_t *x = fr->x;
                disassemble(fp, x);
        }
        fclose(fp);
        return 0;
}

#endif

static struct executable_t *
assemble_fifth_pass(struct assemble_t *a)
{
        struct as_frame_t *fr = list2frame(a->finished_frames.next);
        bug_on(&fr->list == &a->finished_frames);
        bug_on(!(fr->x->flags & FE_TOP));

        return fr->x;
}

static struct assemble_t *
new_assembler(const char *source_file_name, struct opcode_t *token_arr)
{
        struct assemble_t *a = ecalloc(sizeof(*a));
        a->file_name = (char *)source_file_name;
        a->prog = token_arr;
        a->oc = a->prog;
        /* don't let the first ones be zero, that looks bad */
        a->func = FUNC_INIT;
        list_init(&a->active_frames);
        list_init(&a->finished_frames);
        as_frame_push(a, 0);

        /* first alex() is @0 */
        a->oc--;
        return a;
}

static void
free_assembler(struct assemble_t *a, int err)
{
        as_delete_frames(a, err);
        free(a);
}

/* token_arr MUST be EOF-terminated */
struct executable_t *
assemble(const char *source_file_name, struct opcode_t *token_arr)
{
        struct assemble_t *a;
        struct executable_t *ex;
        int res;

        a = new_assembler(source_file_name, token_arr);

        if ((res = setjmp(a->env)) != 0) {
                const char *msg;
                switch (res) {
                default:
                case AE_GEN:
                        msg = "Undefined error";
                        break;
                case AE_BADEOF:
                        msg = "Unexpected termination";
                        break;
                case AE_BADTOK:
                        msg = "Invalid token";
                        break;
                case AE_EXPECT:
                        msg = "Expected token missing";
                        break;
                case AE_REDEF:
                        msg = "Redefinition of local variable";
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
                case AE_BREAK:
                        msg = "Unexpected break";
                        break;
                case AE_NOTIMPL:
                        msg = "Not implemented yet";
                        break;
                case AE_ARGNAME:
                        msg = "Malformed argument name";
                        break;
                case AE_BADINSTR:
                        msg = "Bad instruction";
                        break;
                }
                warning("Assembler returned error code %d (%s)", res, msg);
                warning("near line=%d tok=%s", a->oc->line, a->oc->s);
                ex = NULL;
        } else {
                assemble_first_pass(a);
                assemble_second_pass(a);
                assemble_third_pass(a);
                if (assemble_fourth_pass(a) < 0)
                        warning("Could not disassemble %s", a->file_name);
                ex = assemble_fifth_pass(a);
        }

        free_assembler(a, res);
        return ex;
}

