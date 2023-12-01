#include "egq.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

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
        NSYM    = FRAME_STACK_MAX,
        NNEST   = FRAME_NEST_MAX,
        /* technically not the same thing, but... */
        NARG    = FRAME_CLOSURE_MAX,
        NCLO    = FRAME_CLOSURE_MAX,
        NFRAME  = 32,
        NCONST  = 128,
};

enum {
        FUNC_INIT = 1,
        JMP_INIT = 1,
};

/* TODO: define instr_t and use sizeof() here */
#define INSTR_SIZE      2
#define DATA_ALIGN_SIZE 8

#define PAD_ALIGN(x) \
        (DATA_ALIGN_SIZE - (((x) * INSTR_SIZE) & (DATA_ALIGN_SIZE-1)))

struct as_frame_t {
        FILE *filp;
        char template[32];
        char *symtab[NSYM];
        char *argv[NARG];
        char *clo[NCLO];
        struct opcode_t *consts[NCONST];
        int scope[NNEST];
        int nconst;
        int nest;
        int fp;
        int cp;
        int argc;
        int sp;
        int isc[NSYM / 32 + 1];
        int jmp;                /* next branch label number */

        /*
         * doesn't print, used for resolving local jumps
         * within function
         */
        int line;
        struct list_t list;
};

struct assemble_t {
        FILE *filp;
        struct opcode_t *prog;  /* the full ``ns_t'' program */
        struct opcode_t *oc;    /* pointer into prog */
        int func;               /* next function label number */
        jmp_buf env;
        struct list_t active_frames;
        struct list_t finished_frames;
        struct as_frame_t *fr;
};

static void assemble_eval(struct assemble_t *a);
static void assemble_expression(struct assemble_t *a,
                                unsigned int flags, int skip);

#define list2frame(li) \
        container_of(li, struct as_frame_t, list)

static void
assemble_err_if(struct assemble_t *a, bool cond, int err)
{
        if (cond) {
                if (!err)
                        err = 1;
                longjmp(a->env, err);
        }
}

static void
assemble_err(struct assemble_t *a, int err)
{
        assemble_err_if(a, true, err);
}

static void
assemble_push_miniframe(struct assemble_t *a)
{
        struct as_frame_t *fr;
        int fd;

        fr = emalloc(sizeof(*fr));
        memset(fr, 0, sizeof(*fr));

        sprintf(fr->template, "egq%d.XXXXXXX", a->func);
        fd = mkstemp(fr->template);
        if (fd < 0)
                fail("mkstemp");
        fr->filp = fdopen(fd, "w+");
        fr->jmp = JMP_INIT;

        list_init(&fr->list);
        list_add_tail(&fr->list, &a->active_frames);

        a->fr = fr;
        a->filp = a->fr->filp;
}

/*
 * This is so dirty, but we have to because we need to stuff
 * future frame with arg defs while adding instructions to old
 * frame.
 */
static void
assemble_swap_miniframe(struct assemble_t *a)
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
        a->filp = a->fr->filp;
}

/*
 * Doesn't destroy it, it just removes it from active list.
 * We'll iterate through these when we're done.
 */
static void
assemble_pop_miniframe(struct assemble_t *a)
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
        a->filp = a->fr->filp;
}

static void
assemble_delete_miniframes_list(struct list_t *parent_list)
{
        struct list_t *li, *tmp;
        list_foreach_safe(li, tmp, parent_list) {
                struct as_frame_t *fr = list2frame(li);
                list_remove(&fr->list);
                fclose(fr->filp);
                if (unlink(fr->template) < 0) {
                        warning("unlink %s failed (%s)",
                                fr->template, strerror(errno));
                }
                free(fr);
        }
}
static void
assemble_delete_miniframes(struct assemble_t *a)
{
        assemble_delete_miniframes_list(&a->active_frames);
        assemble_delete_miniframes_list(&a->finished_frames);
}

static void
aunlex(struct assemble_t *a)
{
        /* "minus one" should be fine */
        bug_on(a->oc < a->prog);
        a->oc--;
}

static int
alex(struct assemble_t *a)
{
        if (a->oc->t != EOF)
                a->oc++;
        return a->oc->t;
}

static int
aelex(struct assemble_t *a, int exp)
{
        alex(a);
        assemble_err_if(a, a->oc->t != exp, AE_EXPECT);
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
apush_scope(struct assemble_t *a)
{
        struct as_frame_t *fr = a->fr;
        if (fr->nest >= NNEST)
                assemble_err(a, AE_OVERFLOW);
        fr->scope[fr->nest++] = fr->fp;
        fr->fp = fr->sp;
}

static void
apop_scope(struct assemble_t *a)
{
        bug_on(a->fr->nest <= 0);
        a->fr->nest--;
        a->fr->fp = a->fr->scope[a->fr->nest];
}

static void
assemble_directive(struct assemble_t *a, char *directive, ...)
{
        va_list ap;
        va_start(ap, directive);
        vfprintf(a->filp, directive, ap);
        va_end(ap);
        putc('\n', a->filp);
}

static void
assemble_directive_const(struct assemble_t *a, struct opcode_t *oc)
{
        switch (oc->t) {
        case 'i':
                fprintf(a->filp, ".const 0x%016llx", oc->i);
                break;
        case 'f':
                fprintf(a->filp, ".const %.8le", oc->f);
                break;
        case 'q':
        case 'u':
                fprintf(a->filp, ".const @%p // ", oc->s);
                print_escapestr(a->filp, oc->s, '"');
                break;
        default:
                bug();
        }
        putc('\n', a->filp);
}

static void
acomment(struct assemble_t *a, char *comment)
{
        fprintf(a->filp, "%16s// %s\n", "", comment);
}

static void
alabel(struct assemble_t *a, int label)
{
        fprintf(a->filp, "%d:\n", label);
}

static void
alabel_func(struct assemble_t *a, int funcno)
{
        fprintf(a->filp, "func%06d:\n", funcno);
}

static void
ainstr_start(struct assemble_t *a, const char *instr)
{
        fprintf(a->filp, "%8s%-8d%-15s ", "", a->fr->line, instr);
        a->fr->line++;
}

static void
ainstr(struct assemble_t *a, const char *instr, const char *argfmt, ...)
{
        va_list ap;
        ainstr_start(a, instr);
        if (argfmt) {
                va_start(ap, argfmt);
                vfprintf(a->filp, argfmt, ap);
                va_end(ap);
        }
        putc('\n', a->filp);
}

static int
seek_or_add_const(struct assemble_t *a, struct opcode_t *oc)
{
        int i;
        struct as_frame_t *fr = a->fr;
        switch (oc->t) {
        case 'i':
                for (i = 0; i < fr->nconst; i++) {
                        if (fr->consts[i]->i == oc->i)
                                break;
                }
                break;

        case 'f':
                for (i = 0; i < fr->nconst; i++) {
                        if (fr->consts[i]->f == oc->f)
                                break;
                }
                break;
        case 'u':
        case 'q':
                for (i = 0; i < fr->nconst; i++) {
                        if (fr->consts[i]->s == oc->s)
                                break;
                }
                break;
        default:
                i = 0; /* satisfied, compiler? */
                bug();
        }

        if (i == fr->nconst) {
                assemble_err_if(a, fr->nconst >= NCONST, AE_OVERFLOW);
                fr->consts[fr->nconst++] = oc;
        }
        return i;
}

static void
ainstr_push_const(struct assemble_t *a, struct opcode_t *oc)
{
        ainstr(a, "PUSH_CONST", "%d", seek_or_add_const(a, oc));
}

static void
assemble_push_noinstr(struct assemble_t *a, char *name)
{
        if (a->fr->sp >= NSYM)
                assemble_err(a, AE_OVERFLOW);

        if (name) {
                if (symtab_seek(a, name) >= 0)
                        assemble_err(a, AE_REDEF);
                if (arg_seek(a, name) >= 0)
                        assemble_err(a, AE_REDEF);
        }

        a->fr->symtab[a->fr->sp++] = name;
}

static void
ainstr_declare(struct assemble_t *a, bool isc, char *name)
{
        assemble_push_noinstr(a, name);

        ainstr_start(a, "PUSH");
        fprintf(a->filp, "%16s// '%s'", "", name);
        putc('\n', a->filp);
}

static void
assemble_rodata(struct assemble_t *a)
{
        int i;
        struct as_frame_t *fr;

        fr = a->fr;
        if (fr->nconst) {
                int align = PAD_ALIGN(fr->line);
                while (align-- > 0) {
                        ainstr(a, "NOP", NULL);
                }
                assemble_directive(a, ".rodata");
        }
        for (i = 0; i < fr->nconst; i++)
                assemble_directive_const(a, fr->consts[i]);
}

static void
assemble_function(struct assemble_t *a, bool lambda, int funcno)
{
        putc('\n', a->filp);
        assemble_directive(a, ".funcstart %d // near line %d",
                           funcno, a->oc->line);
        alabel_func(a, funcno);
        if (lambda) {
                /* peek if brace */
                int t = alex(a);
                aunlex(a);
                if (t == OC_LBRACE) {
                        assemble_expression(a, 0, -1);
                        assemble_err_if(a, a->oc->t != OC_LAMBDA, AE_LAMBDA);
                } else {
                        assemble_eval(a);
                        alex(a);
                        assemble_err_if(a, a->oc->t != OC_LAMBDA, AE_LAMBDA);
                        ainstr(a, "LOAD", "REG_LLVAL");
                        ainstr(a, "RETURN_VALUE", NULL);
                        /* we know we have return so we can skip */
                        goto skip;
                }
        } else {
                assemble_expression(a, 0, -1);
        }
        /*
         * This is often unreachable to the VM,
         * but in case expression reached end
         * without hitting "return", we need a BL.
         */
        ainstr(a, "LOAD_CONST", "=0");
        ainstr(a, "RETURN_VALUE", NULL);
skip:
        assemble_rodata(a);
        assemble_directive(a, ".funcend %d", funcno);
        putc('\n', a->filp);
}

static void
assemble_funcdef(struct assemble_t *a, bool lambda)
{
        int funcno = a->func++;

        ainstr(a, "DEFFUNC", "@func%06d", funcno);
        aelex(a, OC_LPAR);

        assemble_push_miniframe(a);

        do {
                bool closure = false;
                bool deflt = false;
                char *name;
                alex(a);
                if (a->oc->t == OC_RPAR)
                        break;
                if (a->oc->t == OC_COLON) {
                        closure = true;
                        alex(a);
                }
                assemble_err_if(a, a->oc->t != 'u', AE_ARGNAME);
                name = a->oc->s;
                alex(a);
                if (a->oc->t == OC_EQ) {
                        deflt = true;

                        assemble_swap_miniframe(a);
                        assemble_eval(a);
                        assemble_swap_miniframe(a);

                        alex(a);
                }
                if (closure) {
                        assemble_err_if(a, !deflt, AE_ARGNAME);

                        /* pop-pop-push, need to balance here */
                        assemble_swap_miniframe(a);
                        ainstr(a, "ADD_CLOSURE", NULL);
                        assemble_swap_miniframe(a);

                        assemble_err_if(a, a->fr->cp >= NCLO, AE_OVERFLOW);
                        a->fr->clo[a->fr->cp++] = name;
                } else {
                        if (deflt) {
                                assemble_swap_miniframe(a);
                                ainstr(a, "ADD_DEFAULT", "%d", a->fr->argc);
                                assemble_swap_miniframe(a);
                        }

                        assemble_err_if(a, a->fr->argc >= NARG, AE_OVERFLOW);
                        a->fr->argv[a->fr->argc++] = name;
                }
        } while (a->oc->t == OC_COMMA);
        assemble_err_if(a, a->oc->t != OC_RPAR, AE_PAR);

        assemble_function(a, lambda, funcno);
        assemble_pop_miniframe(a);
}

static void
assemble_arraydef(struct assemble_t *a)
{
        ainstr(a, "DEFLIST", NULL);
        alex(a);
        if (a->oc->t == OC_RBRACK) /* empty array */
                return;
        aunlex(a);

        acomment(a, "getting list initializers");
        do {
                assemble_eval(a);
                /* pop attr, pop array, addattr, push array */
                ainstr(a, "LIST_APPEND", NULL);
        } while (a->oc->t == OC_COMMA);
        assemble_err_if(a, a->oc->t != OC_RBRACK, AE_BRACK);
        acomment(a, "list defined, pushed to stack");
}

static void
assemble_objdef(struct assemble_t *a)
{
        acomment(a, "start dictionary init");
        ainstr(a, "DEFDICT", NULL);
        do {
                struct opcode_t *name;
                int namei;
                aelex(a, 'u');
                name = a->oc;
                namei = seek_or_add_const(a, name);
                aelex(a, OC_COLON);
                alex(a);

                /* TODO: support these */
                if (a->oc->t == OC_PRIV)
                        alex(a);
                if (a->oc->t == OC_CONST)
                        alex(a);
                aunlex(a);
                assemble_eval(a);
                /*
                 * pop attr, pop array, addattr, push array
                 * name is always constant, the only other way to
                 * add an attribute is in function call.
                 */
                ainstr(a, "ADDATTR", "%-16d// '%s'", namei, name->s);
                alex(a);
        } while (a->oc->t == OC_COMMA);
        assemble_err_if(a, a->oc->t != OC_RBRACE, AE_BRACE);
        acomment(a, "end dictionary init");
}

static void assemble_eval1(struct assemble_t *a);

static void
ainstr_push_symbol(struct assemble_t *a, struct opcode_t *name)
{
        int idx;

        /*
         * Note: in our implementation, FP <= AP <= SP,
         * that is, AP is the *top* of the argument stack.
         * We don't know how many args the caller actually pushed,
         * so we use AP instead of FP for our local-variable
         * de-reference.
         */
        if ((idx = symtab_seek(a, name->s)) >= 0) {
                ainstr(a, "PUSH_PTR", "AP, %-12d// '%s'", idx, name->s);
        } else if ((idx = arg_seek(a, name->s)) >= 0) {
                ainstr(a, "PUSH_PTR", "FP, %-12d// '%s'", idx, name->s);
        } else if ((idx = clo_seek(a, name->s)) >= 0) {
                ainstr(a, "PUSH_PTR", "CP, %-12d// '%s'", idx, name->s);
        } else if (!strcmp(name->s, "__gbl__")) {
                ainstr(a, "PUSH_PTR", "GBL");
        } else {
                int namei = seek_or_add_const(a, name);
                ainstr(a, "PUSH_PTR", "SEEK, %-10d// '%s'", namei, name->s);
        }
}

static void
assemble_call_func(struct assemble_t *a, bool have_parent)
{
        int argc = 0;
        aelex(a, OC_LPAR);
        alex(a);

        if (a->oc->t != OC_RPAR) {
                aunlex(a);
                do {
                        assemble_eval(a);
                        argc++;
                        alex(a);
                } while (a->oc->t == OC_COMMA);
        }

        assemble_err_if(a, a->oc->t != OC_RPAR, AE_PAR);

        /*
         * 1st arg is 1 for 'parent on stack', 0 otherwise
         * 2nd arg is # of args pushed onto stack.
         *
         * stack from top is: argn...arg1, arg0, func, parent
         *
         * CALL_FUNC knows how much to pop based on parent arg
         */
        if (have_parent) {
                ainstr(a, "CALL_FUNC", "NO_PARENT, %d", argc, argc);
        } else {
                ainstr(a, "CALL_FUNC", "PARENT, %d", argc, argc);
        }

        /*
         * For assembler purposes, we don't need these anymore.
         * In execution the args will have been put in a different frame.
         */
        while (argc > 0)
                argc--;
}

static void
assemble_eval_atomic(struct assemble_t *a)
{
        switch (a->oc->t) {
        case 'u':
                ainstr_push_symbol(a, a->oc);
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
                ainstr(a, "PUSH_PTR", "THIS");
                break;
        default:
                assemble_err(a, AE_BADTOK);
        }

        alex(a);
}

static void
assemble_eval9(struct assemble_t *a)
{
        if (a->oc->t == OC_LPAR) {
                alex(a);
                assemble_eval1(a);
                assemble_err_if(a, a->oc->t != OC_RPAR, AE_PAR);
                alex(a);
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
                        aelex(a, 'u');
                        namei = seek_or_add_const(a, a->oc);
                        ainstr(a, "GETATTR", "FROM_CONST, %d", namei);
                        break;

                case OC_LBRACK:
                        have_parent = true;
                        inbal++;
                        alex(a);
                        switch (a->oc->t) {
                        case 'q':
                        case 'i':
                                name = a->oc;
                                if (alex(a) == OC_RBRACK) {
                                        namei = seek_or_add_const(a, name);
                                        aunlex(a);
                                        ainstr(a, "GETATTR", "FROM_CONST, %d", namei);
                                        break;
                                }
                                aunlex(a);
                                /* expression, fall through */
                        case 'u':
                                /* need to evaluate index */
                                aunlex(a);
                                assemble_eval1(a);
                                ainstr(a, "GETATTR", "FROM_STACK, -1");
                                break;
                        default:
                                assemble_err(a, AE_BADTOK);
                        }
                        aelex(a, OC_RBRACK);
                        break;

                case OC_LPAR:
                        /* CALL_FUNC pops twice if have_parent */
                        if (have_parent)
                                --inbal;
                        aunlex(a);
                        assemble_call_func(a, have_parent);
                        have_parent = false;
                        break;
                }
                alex(a);
        }

        bug_on(inbal < 0);
        if (inbal)
                ainstr(a, "UNWIND", "%d", inbal);
}

static void
assemble_eval7(struct assemble_t *a)
{
        int t = a->oc->t;
        if (t == OC_TILDE || t == OC_MINUS
            || t == OC_EXCLAIM || t == OC_PLUS) {
                const char *op;
                if (t == OC_TILDE)
                        op = "BITWISE_NOT";
                else if (t == OC_MINUS)
                        op = "NEGATE";
                else if (t == OC_EXCLAIM)
                        op = "LOGICAL_NOT";
                else /* +, do nothing*/
                        op = NULL;
                alex(a);
                assemble_eval8(a);

                if (op)
                        ainstr(a, op, NULL);
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
                const char *op;
                if (a->oc->t == OC_MUL)
                        op = "MUL";
                else if (a->oc->t == OC_DIV)
                        op = "DIV";
                else
                        op = "MOD";
                alex(a);
                assemble_eval7(a);
                ainstr(a, op, NULL);
        }
}

static void
assemble_eval5(struct assemble_t *a)
{
        assemble_eval6(a);
        while (a->oc->t == OC_PLUS || a->oc->t == OC_MINUS) {
                const char *op;
                if (a->oc->t == OC_PLUS)
                        op = "ADD";
                else
                        op = "SUB";
                alex(a);
                assemble_eval6(a);
                ainstr(a, op, NULL);
        }
}

static void
assemble_eval4(struct assemble_t *a)
{
        assemble_eval5(a);
        while (a->oc->t == OC_LSHIFT || a->oc->t == OC_RSHIFT) {
                const char *op;
                if (a->oc->t == OC_LSHIFT)
                        op = "LSFHIT";
                else
                        op = "RSHIFT";

                alex(a);
                assemble_eval5(a);
                ainstr(a, op, NULL);
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
                const char *cmp;
                switch (t) {
                case OC_EQEQ:
                        cmp = "EQ";
                        break;
                case OC_LEQ:
                        cmp = "LEQ";
                        break;
                case OC_GEQ:
                        cmp = "GEQ";
                        break;
                case OC_NEQ:
                        cmp = "NEQ";
                        break;
                case OC_LT:
                        cmp = "LT";
                        break;
                case OC_GT:
                        cmp = "GT";
                        break;
                }
                alex(a);
                assemble_eval4(a);

                ainstr(a, "CMP", "%s", cmp);
        }
}

static void
assemble_eval2(struct assemble_t *a)
{
        assemble_eval3(a);
        while (a->oc->t == OC_AND
               || a->oc->t == OC_OR
               || a->oc->t == OC_XOR) {
                const char *op;
                if (a->oc->t == OC_AND) {
                        op = "BINARY_AND";
                } else if (a->oc->t == OC_OR) {
                        op = "BINARY_OR";
                } else {
                        op = "BINARY_XOR";
                }
                alex(a);
                assemble_eval3(a);
                ainstr(a, op, NULL);
        }
}

static void
assemble_eval1(struct assemble_t *a)
{
        assemble_eval2(a);

        if (a->oc->t == OC_OROR) {
                alex(a);
                assemble_eval2(a);
                ainstr(a, "LOGICAL_OR", NULL);
        } else if (a->oc->t == OC_ANDAND) {
                alex(a);
                assemble_eval2(a);
                ainstr(a, "LOGICAL_AND", NULL);
        }
}

static void
assemble_eval(struct assemble_t *a)
{
        alex(a);
        assemble_eval1(a);
        aunlex(a);
}

static void
assemble_assign(struct assemble_t *a)
{
        /*
         * FIXME: Need to pass down whether this is a 'const' or not
         * If global, fine, it has a flag.  If stack, the only way
         * we know right now is if it's here.
         */
        assemble_eval(a);
        ainstr(a, "POP", NULL);
        ainstr(a, "ASSIGN", NULL);
}

/* FIXME: huge DRY violation w/ eval8 */
static void
assemble_ident_helper(struct assemble_t *a, unsigned int flags)
{
        bool have_parent = false;
        int last_t = 0;
        /* see comment in assemble_eval8, same ish-- */
        int inbal = 0;

        alex(a);

        /*
         * check some fast-path easy cases:
         *      thing = that
         * instead of thing.child.granchild... = that
         */
        if (a->oc->t == OC_SEMI)
                return; /* empty statement? are we ok with this? */
        if (a->oc->t == OC_EQ) {
                assemble_assign(a);
                return;
        }

        for (;;) {
                int namei;
                struct opcode_t *name;
                switch (a->oc->t) {
                case OC_PER:
                        have_parent = true;
                        aelex(a, 'u');
                        namei = seek_or_add_const(a, a->oc);
                        if (alex(a) == OC_EQ) {
                                assemble_eval(a);
                                ainstr(a, "SETATTR", "FROM_CONST, %d", namei);
                                goto done;
                        }

                        aunlex(a);
                        inbal++;
                        ainstr(a, "GETATTR", "FROM_CONST, %d", namei);
                        break;

                case OC_LBRACK:
                        have_parent = true;
                        alex(a);
                        switch (a->oc->t) {
                        case 'q':
                        case 'i':
                                /*
                                 * this is inelegant, but it reduces the number of
                                 * stack operations in the final assembly
                                 */
                                name = a->oc;
                                if (alex(a) == OC_RBRACK) {
                                        namei = seek_or_add_const(a, name);
                                        if (alex(a) == OC_EQ) {
                                                assemble_eval(a);
                                                ainstr(a, "SETATTR", "FROM_CONST, %d", namei);
                                                goto done;
                                        }
                                        aunlex(a);
                                        inbal++;
                                        ainstr(a, "GETATTR", "FROM_CONST, %d", namei);
                                        break;
                                }
                                aunlex(a);
                                /* fall through, we need to fully eval */

                        case 'u':
                                /* need to evaluate index */
                                aunlex(a);
                                assemble_eval(a);

                                if (alex(a) == OC_RBRACK) {
                                        if (alex(a) == OC_EQ) {
                                                assemble_eval(a);
                                                ainstr(a, "SETATTR", "FROM_STACK, -1");
                                                goto done;
                                        }
                                        aunlex(a);
                                }
                                aunlex(a);

                                inbal++;
                                ainstr(a, "GETATTR", "FROM_STACK, -1");
                                break;
                        default:
                                assemble_err(a, AE_BADTOK);
                        }
                        aelex(a, OC_LBRACK);
                        break;

                case OC_LPAR:
                        if (have_parent)
                                --inbal;
                        aunlex(a);
                        assemble_call_func(a, have_parent);
                        have_parent = false;
                        break;

                case OC_EQ:
                        /*
                         * Academically, "some_func_call() = x" should be
                         * valid, but it looks wrong and I won't allow it.
                         */
                        assemble_err_if(a, last_t == OC_RPAR, AE_BADINSTR);

                        assemble_assign(a);
                        goto done;

                case OC_SEMI:
                        /* should end in a function or an assignment */
                        if (last_t != OC_RPAR)
                                assemble_err(a, AE_BADTOK);
                        aunlex(a);
                        goto done;

                case OC_PLUSPLUS:
                        ainstr(a, "INCR", NULL);
                        goto done;

                case OC_MINUSMINUS:
                        ainstr(a, "DECR", NULL);
                        goto done;

                default:
                        assemble_err(a, AE_BADTOK);
                }

                last_t = a->oc->t;
                alex(a);
        }

done:
        bug_on(inbal < 0);
        if (inbal)
                ainstr(a, "UNWIND", "%d", inbal);

        if (!!(flags & FE_FOR))
                aelex(a, OC_RPAR);
        else
                aelex(a, OC_SEMI);
}

static void
assemble_this(struct assemble_t *a, unsigned int flags)
{
        ainstr(a, "PUSH_PTR", "THIS");
        assemble_ident_helper(a, flags);
}

static void
assemble_identifier(struct assemble_t *a, unsigned int flags)
{
        ainstr_push_symbol(a, a->oc);
        assemble_ident_helper(a, flags);
}

static void
assemble_let(struct assemble_t *a)
{
        char *name;
        bool isc = false;
        aelex(a, 'u');
        name = a->oc->s;
        alex(a);
        if (a->oc->t == OC_CONST) {
                isc = true;
                alex(a);
        }

        ainstr_declare(a, isc, name);

        switch (a->oc->t) {
        case OC_SEMI:
                /* emtpy declaration */
                return;
        case OC_EQ:
                assemble_eval(a);
                ainstr(a, "ASSIGN", "%16s// '%s'", "", name);
                aelex(a, OC_SEMI);
                break;
        default:
                assemble_err(a, AE_BADTOK);
        }
}

static void
assemble_return(struct assemble_t *a)
{
        alex(a);
        acomment(a, "return");
        if (a->oc->t == OC_SEMI) {
                ainstr(a, "LOAD_CONST", "=0");
                ainstr(a, "RETURN_VALUE", NULL);
        } else {
                aunlex(a);
                assemble_eval(a);
                ainstr(a, "POP", NULL);
                ainstr(a, "RETURN_VALUE", NULL);
                aelex(a, OC_SEMI);
        }
}

/* skip provided, because 'break' goes outside 'if' scope */
static void
assemble_if(struct assemble_t *a, int skip)
{
        int jmpelse = a->fr->jmp++;
        acomment(a, "if");
        assemble_eval(a);
        ainstr(a, "POP", NULL);
        ainstr(a, "B_IF_FALSE", "%d", jmpelse);
        acomment(a, "fall through");
        assemble_expression(a, 0, skip);
        alex(a);
        if (a->oc->t == OC_ELSE) {
                acomment(a, "else");
                alabel(a, jmpelse);
                assemble_expression(a, 0, skip);
                acomment(a, "end else");
        } else {
                alabel(a, jmpelse);
                aunlex(a);
                acomment(a, "end if");
        }
}

static void
assemble_while(struct assemble_t *a)
{
        int start = a->fr->jmp++;
        int skip = a->fr->jmp++;

        acomment(a, "while");
        alabel(a, start);
        aelex(a, OC_LPAR);
        assemble_eval(a);
        aelex(a, OC_RPAR);
        ainstr(a, "POP", NULL);
        ainstr(a, "B_IF_FALSE", "%d", skip);
        assemble_expression(a, 0, skip);
        ainstr(a, "B", "%d      // loop back to while", start);
        alabel(a, skip);
        acomment(a, "end while");
        fprintf(a->filp, "\n");
}

static void
assemble_do(struct assemble_t *a)
{
        int start = a->fr->jmp++;
        int skip = a->fr->jmp++;

        acomment(a, "do");
        alabel(a, start);
        assemble_expression(a, 0, skip);
        aelex(a, OC_WHILE);
        assemble_eval(a);
        ainstr(a, "POP", NULL);
        ainstr(a, "B_IF_TRUE", "%d", start);
        alabel(a, skip);
        fprintf(a->filp, "\n");
}

static void
assemble_for(struct assemble_t *a)
{
        int start = a->fr->jmp++;
        int then = a->fr->jmp++;
        int skip = a->fr->jmp++;
        int iter = a->fr->jmp++;
        aelex(a, OC_LPAR);

        acomment(a, "for");
        /* initializer */
        acomment(a, "'for' initializer");
        assemble_expression(a, 0, skip);

        acomment(a, "'for' evaluation");
        alabel(a, start);
        alex(a);
        if (a->oc->t == OC_SEMI) {
                /* empty condition, always true */
                acomment(a, "'for' always true");
                fprintf(a->filp, "B %d\n", then);
        } else {
                aunlex(a);
                assemble_eval(a);
                aelex(a, OC_SEMI);
                acomment(a, "'for' end evaluation");
                ainstr(a, "POP", NULL);
                ainstr(a, "B_IF_FALSE", "%d", skip);
                ainstr(a, "B", "%d", then);
        }
        acomment(a, "'for' iteration step");
        alabel(a, iter);
        assemble_expression(a, FE_FOR, -1);
        ainstr(a, "B", "%d", start);
        acomment(a, "'for' block");
        alabel(a, then);
        assemble_expression(a, 0, skip);
        ainstr(a, "B", "%d", iter);
        alabel(a, skip);
        acomment(a, "end 'for'");
        fprintf(a->filp, "\n");
}

static void
assemble_load(struct assemble_t *a)
{
        assemble_err(a, AE_NOTIMPL);
}

/* flags are same here as in expression */
static void
assemble_expression(struct assemble_t *a, unsigned int flags, int skip)
{
        int brace = 0;
        bool pop = false;

        RECURSION_INCR();

        alex(a);
        if (a->oc->t == OC_LBRACE) {
                brace++;
                pop = true;
                apush_scope(a);
        } else {
                /* single line statement */
                aunlex(a);
        }

        do {
                alex(a);
                if (a->oc->t == EOF) {
                        assemble_err_if(a, skip >= 0, AE_BADEOF);
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
                        assemble_err_if(a, !brace, AE_BRACE);
                        brace--;
                        break;
                case OC_RPAR:
                        assemble_err_if(a, !(flags & FE_FOR), AE_BADTOK);
                        assemble_err_if(a, brace, AE_BRACE);
                        aunlex(a);
                        break;
                case OC_LPAR:
                        aunlex(a);
                        assemble_eval(a);
                        aelex(a, OC_SEMI);
                        break;
                case OC_LET:
                        assemble_err_if(a, !!(flags & FE_FOR), AE_BADTOK);
                        assemble_let(a);
                        break;
                case OC_RETURN:
                        assemble_return(a);
                        break;
                case OC_BREAK:
                        assemble_err_if(a, skip < 0, AE_BREAK);
                        ainstr(a, "B", "%d // break", skip);
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
                        assemble_err(a, AE_BADTOK);
                }
        } while (brace);

        RECURSION_DECR();

        if (pop)
                apop_scope(a);
}

static void
assemble_first_pass(struct assemble_t *a)
{
        acomment(a, "PUSH_PTR arg1 enumerations");
        assemble_directive(a, ".define AP               0");
        assemble_directive(a, ".define FP               1");
        assemble_directive(a, ".define CP               2");
        assemble_directive(a, ".define SP               3");
        assemble_directive(a, ".define SEEK             4");
        assemble_directive(a, ".define GBL              5 // 2nd arg ignored");
        assemble_directive(a, ".define THIS             5 // 2nd arg ignored");
        putc('\n', a->filp);
        acomment(a, "GETATTR arg1 enumerations");
        assemble_directive(a, ".define FROM_CONST       0");
        assemble_directive(a, ".define FROM_STACK       1 // 2nd arg ignored");
        putc('\n', a->filp);
        acomment(a, "CALL_FUNC arg1 enumerations");
        assemble_directive(a, ".define NO_PARENT        0");
        assemble_directive(a, ".define PARENT           1");
        putc('\n', a->filp);
        assemble_directive(a, ".file '%s'", cur_ns->fname);
        putc('\n', a->filp);
        putc('\n', a->filp);
        assemble_directive(a, ".start");
        while (a->oc->t != EOF)
                assemble_expression(a, FE_TOP, -1);
        ainstr(a, "END", NULL);
        assemble_rodata(a);
        fprintf(a->filp, "// end near line %d\n", a->oc->line);

        list_remove(&a->fr->list);
        list_add_front(&a->fr->list, &a->finished_frames);
}

/*
 * consolidate frames into the single output file, done in a way that
 * function definitions don't fall in the middle of other functions,
 * thereby making the code easier to make sense of, and also improving
 * locality.
 */
static void
assemble_second_pass(struct assemble_t *a)
{
        struct list_t *li;
        FILE *fp = fopen(q_.opt.assemble_outfile, "w");
        if (!fp)
                fail("fopen");

        list_foreach(li, &a->finished_frames) {
                int c;
                struct as_frame_t *fr = list2frame(li);

                if (fseek(fr->filp, 0L, SEEK_SET) < 0)
                        fail("fseek");

                while ((c = getc(fr->filp)) != EOF)
                        putc(c, fp);
        }
        fclose(fp);
}

void
assemble(void)
{
        struct assemble_t *a;
        int res;

        a = ecalloc(sizeof(*a));
        a->prog = (struct opcode_t *)cur_ns->pgm.s;
        a->oc = a->prog - 1;
        /* don't let the first ones be zero, that looks bad */
        a->func = FUNC_INIT;
        list_init(&a->active_frames);
        list_init(&a->finished_frames);
        assemble_push_miniframe(a);
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
        } else {
                assemble_first_pass(a);
                assemble_second_pass(a);
        }

        assemble_delete_miniframes(a);
        free(a);
}

