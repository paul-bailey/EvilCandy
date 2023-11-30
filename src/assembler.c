#include "egq.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

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
        NSYM    = 32,
        NNEST   = 32,
        NARG    = 32,
        NFRAME  = 32,
        NCLO    = 32,
};

enum {
        FUNC_INIT = 1,
        JMP_INIT = 1,
};

struct miniframe_t {
        char *symtab[NSYM];
        char *argv[NARG];
        char *clo[NCLO];
        int scope[NNEST];
        int nest;
        int fp;
        int cp;
        int argc;
        int sp;
        int isc[NSYM / 32 + 1];
};

struct assemble_t {
        FILE *filp;
        struct opcode_t *prog;  /* the full ``ns_t'' program */
        struct opcode_t *oc;    /* pointer into prog */
        int jmp;                /* next branch label number */
        int func;               /* next function label number */
        jmp_buf env;
        struct miniframe_t *fr;
        struct miniframe_t *frame_stack[NFRAME];
        int n_frame;
};

static void assemble_eval(struct assemble_t *a);
static void assemble_expression(struct assemble_t *a,
                                unsigned int flags, int skip);

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
        struct miniframe_t *fr;

        if (a->n_frame >= NFRAME)
                assemble_err(a, AE_OVERFLOW);

        fr = emalloc(sizeof(*fr));
        memset(fr, 0, sizeof(*fr));

        a->frame_stack[a->n_frame] = a->fr;
        a->n_frame++;

        a->fr = fr;
}

static void
assemble_pop_miniframe(struct assemble_t *a)
{
        bug_on(a->n_frame <= 0);

        free(a->fr);

        a->n_frame--;
        a->fr = a->frame_stack[a->n_frame];
        a->frame_stack[a->n_frame] = NULL;
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
        struct miniframe_t *fr = a->fr;
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
        struct miniframe_t *fr = a->fr;
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
        struct miniframe_t *fr = a->fr;
        for (i = 0; i < fr->cp; i++) {
                if (s == (char *)fr->clo[i])
                        return i;
        }
        return -1;
}

static void
apush_scope(struct assemble_t *a)
{
        struct miniframe_t *fr = a->fr;
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
        fprintf(a->filp, "%16s%-15s ", "", instr);
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
        unsigned shift, ish;
        struct miniframe_t *fr = a->fr;
        assemble_push_noinstr(a, name);

        for (shift = fr->sp, ish = 0; shift >= 32; ish++, shift -= 32)
                ;
        fr->isc[ish] |= (1u << shift);

        ainstr_start(a, "PUSH");
        fprintf(a->filp, "%24s// '%s'", "", name);
        putc('\n', a->filp);
}

static void
ainstr_binary_op(struct assemble_t *a, const char *op)
{
        ainstr(a, op, NULL);
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
        assemble_directive(a, ".funcend %d", funcno);
        putc('\n', a->filp);
}

static void
assemble_funcdef(struct assemble_t *a, bool lambda)
{
        int skip = a->jmp++;
        int funcno = a->func++;

        assemble_push_miniframe(a);
        ainstr(a, "DEFFUNC", "@func%06d", funcno);
        aelex(a, OC_LPAR);

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
                        assemble_eval(a);
                }
                if (closure) {
                        assemble_err_if(a, !deflt, AE_ARGNAME);

                        /* pop-pop-push, need to balance here */
                        ainstr(a, "ADD_CLOSURE", NULL);

                        assemble_err_if(a, a->fr->cp >= NCLO, AE_OVERFLOW);
                        a->fr->clo[a->fr->cp++] = name;
                } else {
                        if (deflt) {
                                ainstr(a, "ADD_DEFAULT",
                                       "REG_LVAL, REG_RVAL, %d", a->fr->argc);
                        }

                        assemble_err_if(a, a->fr->argc >= NARG, AE_OVERFLOW);
                        a->fr->argv[a->fr->argc++] = name;
                }
        } while (a->oc->t == OC_COMMA);
        assemble_err_if(a, a->oc->t != OC_RPAR, AE_PAR);
        ainstr(a, "B", "%d", skip);

        assemble_function(a, lambda, funcno);
        alabel(a, skip);
        assemble_pop_miniframe(a);
}

static void
assemble_arraydef(struct assemble_t *a)
{
        ainstr(a, "DEFARRAY", NULL);
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
        ainstr(a, "DEFOBJ", NULL);
        acomment(a, "getting dictionary initializers");
        do {
                char *name;
                aelex(a, 'u');
                name = a->oc->s;
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
                ainstr(a, "ADDATTR", "='%s'", name);
                alex(a);
        } while (a->oc->t == OC_COMMA);
        assemble_err_if(a, a->oc->t != OC_RBRACE, AE_BRACE);
        acomment(a, "dictionary defined, pushed to stack");
}

static void assemble_eval1(struct assemble_t *a);

static void
ainstr_push_symbol(struct assemble_t *a, char *name)
{
        int idx;

        /*
         * Note: in our implementation, FP <= AP <= SP,
         * that is, AP is the *top* of the argument stack.
         * We don't know how many args the caller actually pushed,
         * so we use AP instead of FP for our local-variable
         * de-reference.
         */
        if ((idx = symtab_seek(a, name)) >= 0) {
                ainstr(a, "PUSH", "@(AP+%d) // local '%s'", idx, name);
        } else if ((idx = arg_seek(a, name)) >= 0) {
                ainstr(a, "PUSH", "@(FP+%d) // arg '%s'", idx, name);
        } else if ((idx = clo_seek(a, name)) >= 0) {
                ainstr(a, "PUSH", "@(CP+%d) // closure '%s'", idx, name);
        } else {
                ainstr(a, "PUSH_GLOBAL", "='%s' // from const", name);
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

        /* CALL_FUNC knows how much to pop based on parent arg */
        if (have_parent) {
                ainstr(a, "CALL_FUNC", "0, %d // no parent, %d args", argc, argc);
        } else {
                ainstr(a, "CALL_FUNC", "1, %d // parent, %d args", argc, argc);
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
                ainstr_push_symbol(a, a->oc->s);
                break;

        case 'i':
                ainstr(a, "PUSH_CONST", "=%llu", a->oc->i);
                break;

        case 'f':
                ainstr(a, "PUSH_CONST", "=%.8e", a->oc->f);
                break;

        case 'q':
                ainstr(a, "PUSH_CONST", "='%s'", a->oc->s);
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
                ainstr(a, "PUSH_GLOBAL", "THIS");
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
                        ainstr(a, "GETATTR", "0, ='%s'", a->oc->s);
                        break;

                case OC_LBRACK:
                        have_parent = true;
                        inbal++;
                        alex(a);
                        switch (a->oc->t) {
                        case 'q':
                                ainstr(a, "GETATTR", "0, ='%s'", a->oc->s);
                                break;
                        case 'i':
                                ainstr(a, "GETATTR", "0, ='%lli'", a->oc->i);
                                break;
                        case 'u':
                                /* need to evaluate index */
                                aunlex(a);
                                assemble_eval1(a);
                                ainstr(a, "GETATTR", "1");
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
        while (inbal > 0)
                inbal--;
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
                        ainstr(a, op, "REG_LVAL");
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
                ainstr_binary_op(a, op);
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
                ainstr_binary_op(a, op);
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
                if (a->oc->t == 'i') {
                        /*
                         * cheaty lookahead: if 'i' (no parenthesis or
                         * other higher-level escape), then it MUST be
                         * just this token, so we don't have to evaluate
                         * further
                         */
                        ainstr(a, op, "=%lli", a->oc->t);
                        alex(a);
                } else {
                        assemble_eval5(a);
                        ainstr_binary_op(a, op);
                }
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
                ainstr_binary_op(a, op);
        }
}

static void
assemble_eval1(struct assemble_t *a)
{
        assemble_eval2(a);

        if (a->oc->t == OC_OROR) {
                alex(a);
                assemble_eval2(a);
                ainstr_binary_op(a, "LOGICAL_OR");
        } else if (a->oc->t == OC_ANDAND) {
                alex(a);
                assemble_eval2(a);
                ainstr_binary_op(a, "LOGICAL_AND");
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
        ainstr(a, "POP", "REG_LVAL");
        ainstr(a, "ASSIGN", "@REG_CHILD, REG_LVAL");
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
                switch (a->oc->t) {
                case OC_PER:
                        have_parent = true;
                        inbal++;
                        aelex(a, 'u');
                        ainstr(a, "LOAD_CONST", "='%s'", a->oc->s);
                        ainstr(a, "GETATTR", "0 // from const");
                        break;

                case OC_LBRACK:
                        have_parent = true;
                        inbal++;
                        alex(a);
                        switch (a->oc->t) {
                        case 'q':
                                ainstr(a, "LOAD_CONST", "='%s'", a->oc->s);
                                ainstr(a, "GETATTR", "0 // from const");
                                break;
                        case 'i':
                                ainstr(a, "LOAD_CONST", "='%lli'", a->oc->i);
                                ainstr(a, "GETATTR", "0 // from const");
                                break;
                        case 'u':
                                /* need to evaluate index */
                                aunlex(a);
                                assemble_eval(a);
                                ainstr(a, "GETATTR", "1 // from stack");
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
        while (inbal > 0)
                inbal--;

        if (!!(flags & FE_FOR))
                aelex(a, OC_RPAR);
        else
                aelex(a, OC_SEMI);
}

static void
assemble_this(struct assemble_t *a, unsigned int flags)
{
        ainstr(a, "PUSH_GLOBAL", "THIS");
        assemble_ident_helper(a, flags);
}

static void
assemble_identifier(struct assemble_t *a, unsigned int flags)
{
        ainstr_push_symbol(a, a->oc->s);
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
                ainstr(a, "ASSIGN", "@%s", name);
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
                ainstr(a, "LOAD_CONST_INT", "=0");
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
        int jmpelse = a->jmp++;
        acomment(a, "if");
        assemble_eval(a);
        ainstr(a, "POP", NULL);
        ainstr(a, "B_IF_FALSE", "%d", jmpelse);
        acomment(a, "fall through");
        assemble_expression(a, 0, skip);
        alex(a);
        if (a->oc->t == OC_ELSE) {
                int jmpfinal = a->jmp++;
                acomment(a, "else");
                alabel(a, jmpelse);
                assemble_expression(a, 0, skip);
                ainstr(a, "B", "%d", jmpfinal);
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
        int start = a->jmp++;
        int skip = a->jmp++;

        acomment(a, "while");
        alabel(a, start);
        aelex(a, OC_LPAR);
        assemble_eval(a);
        aelex(a, OC_RPAR);
        ainstr(a, "POP", NULL);
        ainstr(a, "B_IF_FALSE", "%d", skip);
        assemble_expression(a, 0, skip);
        ainstr(a, "B", "%d", start);
        alabel(a, skip);
        fprintf(a->filp, "\n");
}

static void
assemble_do(struct assemble_t *a)
{
        int start = a->jmp++;
        int skip = a->jmp++;

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
        int start = a->jmp++;
        int then = a->jmp++;
        int skip = a->jmp++;
        int iter = a->jmp++;
        aelex(a, OC_LPAR);

        acomment(a, "for");
        /* initializer */
        assemble_expression(a, 0, skip);

        alabel(a, start);
        alex(a);
        if (a->oc->t == OC_SEMI) {
                /* empty condition, always true */
                fprintf(a->filp, "B %d\n", then);
        } else {
                aunlex(a);
                assemble_eval(a);
                aelex(a, OC_SEMI);
                ainstr(a, "POP", NULL);
                ainstr(a, "B_IF_FALSE", "%d", skip);
                ainstr(a, "B", "%d", then);
        }
        acomment(a, "iteration step");
        alabel(a, iter);
        assemble_expression(a, FE_FOR, -1);
        ainstr(a, "B", "%d", start);
        alabel(a, then);
        assemble_expression(a, 0, skip);
        alabel(a, skip);
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

void
assemble(void)
{
        struct assemble_t *a;
        int res;

        FILE *fp = fopen(q_.opt.assemble_outfile, "w");
        if (!fp)
                fail("fopen");

        a = ecalloc(sizeof(*a));
        a->prog = (struct opcode_t *)cur_ns->pgm.s;
        a->oc = a->prog - 1;
        a->filp = fp;
        /* don't let the first ones be zero, that looks bad */
        a->func = FUNC_INIT;
        a->jmp = JMP_INIT;
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
                assemble_directive(a, ".define REG_LVAL   r0");
                assemble_directive(a, ".define REG_RVAL   r1");
                assemble_directive(a, ".define REG_PARENT r2");
                assemble_directive(a, ".define REG_CHILD  r3");
                assemble_directive(a, ".define REG_CONST  r4");
                fprintf(a->filp, "\n");
                assemble_directive(a, ".file '%s'", cur_ns->fname);
                fprintf(a->filp, "\n\n");
                assemble_directive(a, ".start");
                while (a->oc->t != EOF)
                        assemble_expression(a, FE_TOP, -1);
                ainstr(a, "END", NULL);
                fprintf(a->filp, "// end near line %d\n", a->oc->line);
        }

        fclose(fp);

        while (a->n_frame)
                assemble_pop_miniframe(a);

        free(a);
}

