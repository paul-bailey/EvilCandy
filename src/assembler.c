/*
 * 2025 update... I've been reading Aho/Ullman and now I see just how
 * hillbilly this file is.  Please don't look at it, it's embarrassing.
 *
 * FIXME: This whole file!  It parses, compiles, and assembles all at the
 * same time.  Aside from a few optimizations and function-and-label
 * resolutions in assemble_post.c, it does not separate any of the three
 * stages apart from each other.  It doesn't distinguish them, either; it
 * calls everything assemble_this, assemble_that, when it should say
 * parse_this or compile_that.
 *
 * The entry point is assemble().  The result will be a compiled XptrType
 * object to execute.  Since files are thought of as big functions nesting
 * little functions, multiple XptrType objects will likely be compiled in
 * one go.  The entry-point XptrType object will be the one returned. (See
 * big comment in xptr.h how these link to each other.)
 *
 * For a statement like
 *              let a = (x + y.z() * 2.0);
 * the parser's entry point is assemble_stmt().
 * The part to the right of the "="
 *              (x + y.z() * 2.0)
 * is evaluated starting at assemble_expr().
 *
 * Some other frequent helper functions:
 *      add_instr
 *              Append a new instruction
 *      ainstr_XXXX
 *              Wrap add_instr with some other stuff that needs to get done
 *      as_XXXX
 *              Little helper function
 *      assemble_XXXX
 *              BIG helper function!
 *      [^a].*_XXXX
 *              Poorly chosen function name :)
 */
#include <evilcandy.h>
#include <token.h>
#include <xptr.h>
#include <setjmp.h>
#include "assemble_priv.h"

#if DBUG_PROFILE_LOAD_TIME
# include <time.h>
#endif

/*
 * The @flags arg used in some of the functions below.
 * @FE_FOR: We're in that middle part of a for loop between two
 *          semicolons.  Only used by assembler.
 * @FE_CONTINUE: We're the start of a loop where 'continue' may
 *          break us out.
 * @FE_TOP: We're the top-level statement in interactive mode.
 * @FEE_MASK: OR flags with this to turn it into one of the
 *          three mutually-exclusive arguments to
 *          assemble_primary_elements
 * @FEE_EVAL: Call to assemble_primary_elements from expression.
 *          Elements are to be treated as read-only, assignment
 *          operators are not allowed.
 * @FEE_ASGN: Call to assemble_primary_elements from statement.
 *          Check for assignment operators.
 * @FEE_DEL: Call to assemble_primary_elements from statement.
 *          At the final element in the 'a.b.c...' expression,
 *          Add instruction to delete that one.
 */
enum {
        FE_FOR          = 0x01,
        FE_CONTINUE     = 0x02,
        FE_TOP          = 0x04,

        /*
         * bits 4-5, three mutually-exclusive arguments to
         * assemble_primary_elements.
         */
        FEE_EVAL        = 0x00, /* use with FEE_MASK */
        FEE_ASGN        = 0x10,
        FEE_DEL         = 0x20,
        FEE_MASK        = 0x30,
};

#define as_err(a, e) longjmp((a)->env, e)
#define as_err_if(a, cond, e) \
        do { if (cond) as_err(a, e); } while (0)
#define as_badeof(a) do { \
        DBUG("Bad EOF, trapped in assembly.c line %d", __LINE__); \
        err_setstr(SyntaxError, "Unexpected termination"); \
        as_err(a, AE_GEN); \
} while (0)

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

enum { FUNC_INIT = 1 };

static void assemble_expr(struct assemble_t *a);
static void assemble_stmt(struct assemble_t *a, unsigned int flags,
                          int continueto);
static void assemble_expr5_atomic(struct assemble_t *a);
static int assemble_primary_elements(struct assemble_t *a,
                                     unsigned int flags);


/* i is ARRAY size, not number of bytes! */
/* XXX should be a buffer.h function */
static void
as_buffer_ptr_resize(struct buffer_t *b, int i)
{
        b->p = i * sizeof(void *);
}

static int
as_buffer_put_ptr(struct buffer_t *b, void *ptr)
{
        buffer_putd(b, (void *)&ptr, sizeof(void *));
        return as_buffer_ptr_size(b) - 1;
}

#define as_buffer_put_name(bf_, nm_) \
        as_buffer_put_ptr(bf_, (void *)(nm_))

static inline int
as_next_funcno(struct assemble_t *a)
{
        return a->func++;
}

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

static void
as_unlex(struct assemble_t *a)
{
        unget_tok(a->prog, &a->oc);
}

static int
as_lex(struct assemble_t *a)
{
        int ret = get_tok(a->prog, &a->oc);
        if (ret == RES_ERROR)
                as_err(a, AE_PARSER);
        return ret;
}

static int
as_errlex(struct assemble_t *a, int exp)
{
        as_lex(a);
        if (a->oc->t != exp) {
                err_setstr(SyntaxError,
                           "expected '%s' but got '%s' ('%s')",
                           token_name(exp), token_name(a->oc->t), a->oc->s);
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

static int
as_peek(struct assemble_t *a, bool may_be_eof)
{
        int res;
        as_lex(a);
        res = a->oc->t;
        if (!may_be_eof && a->oc->t == OC_EOF)
                as_badeof(a);
        as_unlex(a);
        return res;
}

static int
as_symbol_seek__(const char *s, struct buffer_t *b)
{
        if (s) {
                const char **lut = (const char **)b->s;
                size_t i, n = as_buffer_ptr_size(b);
                for (i = 0; i < n; i++) {
                        if (lut[i] && !strcmp(s, lut[i]))
                                return i;
                }
        }
        return -1;
}

static int
as_closure_seek(struct assemble_t *a, const char *s)
{
        return as_symbol_seek__(s, &a->fr->af_closures);
}

/* arg may be NULL, it's the arg1 for LOAD/ASSIGN commands & such */
static int
as_symbol_seek(struct assemble_t *a, const char *s, int *arg)
{
        struct as_frame_t *fr = a->fr;
        int i, targ;
        if ((i = as_symbol_seek__(s, &fr->af_locals)) >= 0) {
                targ = IARG_PTR_AP;
                goto found;
        }
        if ((i = as_symbol_seek__(s, &fr->af_args)) >= 0) {
                targ = IARG_PTR_FP;
                goto found;
        }
        if ((i = as_symbol_seek__(s, &fr->af_closures)) >= 0) {
                targ = IARG_PTR_CP;
                goto found;
        }
        return -1;
found:
        if (arg)
                *arg = targ;
        return i;
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

        buffer_putd(&a->fr->af_instr, &ii, sizeof(ii));
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
        unsigned long val = as_frame_ninstr(a->fr);
        if (val > 32767) {
                err_setstr(RangeError,
                           "Cannot compile: instruction set too large for jump labels");
                as_err(a, AE_GEN);
        }
        assemble_frame_set_label(a->fr, jmp, val);
}

static int
as_next_label(struct assemble_t *a)
{
        return assemble_frame_next_label(a->fr);
}

static int
as_seek_rodata_tok(struct assemble_t *a, struct token_t *oc)
{
        return assemble_seek_rodata(a, oc->v);
}

static void
ainstr_load_const(struct assemble_t *a, struct token_t *oc)
{
        int idx = as_seek_rodata_tok(a, oc);
        add_instr(a, INSTR_LOAD_CONST, 0, idx);
}

/*
 * like ainstr_load_const but from an integer, not token, since
 * loading zero is common enough.
 */
static void
ainstr_load_const_int(struct assemble_t *a, long long ival)
{
        Object *iobj;
        int idx;

        iobj = intvar_new(ival);
        idx = assemble_seek_rodata(a, iobj);
        VAR_DECR_REF(iobj);

        add_instr(a, INSTR_LOAD_CONST, 0, idx);
}

static void
ainstr_push_block(struct assemble_t *a, int arg1, int arg2)
{
        struct as_frame_t *fr = a->fr;
        if (fr->nest >= FRAME_NEST_MAX)
                as_err(a, AE_OVERFLOW);

        fr->scope[fr->nest++] = fr->fp;

        fr->fp = as_buffer_ptr_size(&fr->af_locals);
        add_instr(a, INSTR_PUSH_BLOCK, arg1, arg2);
}

static void
ainstr_pop_block(struct assemble_t *a)
{
        struct as_frame_t *fr = a->fr;
        bug_on(fr->nest <= 0);

        as_buffer_ptr_resize(&fr->af_locals, fr->fp);
        fr->nest--;

        fr->fp = fr->scope[fr->nest];
        add_instr(a, INSTR_POP_BLOCK, 0, 0);
}

static void
ainstr_return_null(struct assemble_t *a)
{
        /*
         * Identical to PUSH_LOCAL and RETURN_VALUE, but this is one
         * instruction fewer.
         */
        add_instr(a, INSTR_END, 0, 0);
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
        if (name && as_symbol_seek(a, name, NULL) >= 0) {
                err_setstr(SyntaxError, "Redefining variable ('%s')", name);
                as_err(a, AE_GEN);
                return 0;
        }

        return as_buffer_put_name(&a->fr->af_locals, name);
}

/*
 * Parse either "i" of "x[i]" or "i:j:k" of "x[i:j:k]".
 * Return token state such that next as_lex() ought to point at
 * closing right bracket "]".
 */
static void
assemble_slice(struct assemble_t *a)
{
        int endmarker = OC_RBRACK;
        int i;

        for (i = 0; i < 3; i++) {
                as_lex(a);
                if (a->oc->t == OC_COLON || a->oc->t == endmarker) {
                        /*
                         * ie. something like [:j] instead of [i:j:k]
                         * Use default for unprovided values:
                         *      i=0, j=null k=1
                         * where 'null' is interpreted as "length(x)"
                         */
                        if (i == 0) {
                                if (a->oc->t == endmarker) {
                                        err_setstr(SyntaxError,
                                                   "Empty subscript");
                                        as_err(a, AE_GEN);
                                }
                                ainstr_load_const_int(a, 0);
                        } else if (i == 1) {
                                add_instr(a, INSTR_PUSH_LOCAL, 0, 0);
                        } else {
                                ainstr_load_const_int(a, 1);
                        }
                } else {
                        /* value provided */
                        as_unlex(a);
                        assemble_expr(a);
                        as_lex(a);
                }

                if (a->oc->t == endmarker) {
                        as_unlex(a);
                        if (i == 0) /* not a slice, just a subscript */
                                return;
                } else if (i != 2 && a->oc->t != OC_COLON) {
                        err_setstr(SyntaxError,
                                   "Expected: either ':' or '%s'",
                                   token_name(endmarker));
                        as_err(a, AE_GEN);
                }
        }
        add_instr(a, INSTR_DEFTUPLE, 0, 3);
}

static void
assemble_function(struct assemble_t *a, bool lambda, int funcno)
{
        if (lambda) {
                /* peek if brace */
                int t = as_lex(a);
                as_unlex(a);
                if (t == OC_LBRACE) {
                        assemble_stmt(a, 0, 0);
                        as_err_if(a, a->oc->t != OC_LAMBDA, AE_LAMBDA);
                } else {
                        assemble_expr(a);
                        as_lex(a);
                        as_err_if(a, a->oc->t != OC_LAMBDA, AE_LAMBDA);
                        add_instr(a, INSTR_RETURN_VALUE, 0, 0);
                        add_instr(a, INSTR_END, 0, 0);
                        /* we know we have return so we can skip */
                        return;
                }
        } else {
                assemble_stmt(a, 0, 0);
        }
        /*
         * This is often unreachable to the VM, but in case statement
         * reached end without hitting "return", we need to preven VM
         * from overrunning instruction set.
         */
        ainstr_return_null(a);
}

static void
assemble_funcdef(struct assemble_t *a, bool lambda)
{
        int funcno = as_next_funcno(a);
        int minargs = 0;
        int optarg = -1;
        int kwarg = -1;
        Object *id_o;

        /* placeholder for XptrType, resolved in assemble_frame_to_xptr() */
        id_o = idvar_new(funcno);
        add_instr(a, INSTR_DEFFUNC, 0, assemble_seek_rodata(a, id_o));
        VAR_DECR_REF(id_o);
        assemble_frame_push(a, funcno);

        as_errlex(a, OC_LPAR);
        do {
                enum { NORMAL, OPTIND, KWIND } kind;
                as_lex(a);
                if (a->oc->t == OC_RPAR)
                        break;

                if (kwarg >= 0) {
                        err_setstr(SyntaxError,
                                "You may not declare arguments after keyword argument");
                        as_err(a, AE_GEN);
                }

                if (a->oc->t == OC_MUL) {
                        kind = OPTIND;
                        if (optarg >= 0) {
                                err_setstr(SyntaxError,
                                        "You may only declare one variadic argument");
                                as_err(a, AE_GEN);
                        }
                        optarg = as_buffer_ptr_size(&a->fr->af_args);
                        as_lex(a);
                } else if (a->oc->t == OC_POW) {
                        kind = KWIND;
                        kwarg = as_buffer_ptr_size(&a->fr->af_args);
                        as_lex(a);
                } else {
                        kind = NORMAL;
                        if (optarg >= 0) {
                                err_setstr(SyntaxError,
                                        "You may not declare normal argument after variadic argument");
                                as_err(a, AE_GEN);
                        }
                }

                if (a->oc->t != OC_IDENTIFIER) {
                        err_setstr(SyntaxError,
                                "Function argument is not an identifier");
                        as_err(a, AE_GEN);
                }

                if (kind == OPTIND) {
                        as_frame_swap(a);
                        add_instr(a, INSTR_FUNC_SETATTR, IARG_FUNC_OPTIND, optarg);
                        as_frame_swap(a);
                } else if (kind == KWIND) {
                        as_frame_swap(a);
                        add_instr(a, INSTR_FUNC_SETATTR, IARG_FUNC_KWIND, kwarg);
                        as_frame_swap(a);
                }

                char *name = a->oc->s;
                as_lex(a);
                minargs = as_buffer_put_name(&a->fr->af_args, name) + 1;
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RPAR, AE_PAR);

        bug_on(kwarg == optarg && kwarg >= 0);

        assemble_function(a, lambda, funcno);

        /* for user functions, minargs == maxargs */
        bug_on(minargs != as_buffer_ptr_size(&a->fr->af_args));

        as_frame_swap(a);
        add_instr(a, INSTR_FUNC_SETATTR, IARG_FUNC_MINARGS, minargs);
        add_instr(a, INSTR_FUNC_SETATTR, IARG_FUNC_MAXARGS, minargs);
        as_frame_swap(a);

        assemble_frame_pop(a);
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
                /*
                 * sloppy ",]", but allow it anyway,
                 * since everyone else does.
                 */
                if (as_peek(a, false) == OC_RBRACK) {
                        as_lex(a);
                        break;
                }
                assemble_expr(a);
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
        bool has_comma;
        as_lex(a);
        if (a->oc->t == OC_RPAR) {
                /* empty tuple */
                goto done;
        }
        as_unlex(a);

        has_comma = false;
        do {
                /*
                 * ie. ",)", actually necessary in this case, since
                 * there's no other way to express a tuple of length 1.
                 */
                if (as_peek(a, false) == OC_RPAR) {
                        as_lex(a);
                        has_comma = true;
                        break;
                }
                assemble_expr(a);
                as_lex(a);
                n_items++;
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RPAR, AE_PAR);

        /* "(x,)" is a tuple.  "(x)" is whatever x is */
        if (!has_comma && n_items == 1)
                return;
done:
        add_instr(a, INSTR_DEFTUPLE, 0, n_items);
}

static void
assemble_objdef(struct assemble_t *a)
{
        /* TODO: not too hard to support `set' notation here */
        int count = 0;
        as_lex(a);
        if (a->oc->t == OC_RBRACE) /* empty dict */
                goto skip;

        count = 0;
        as_unlex(a);
        do {
                as_lex(a);

                if (a->oc->t == OC_LBRACK) {
                        /* computed key */
                        assemble_expr(a);
                        as_errlex(a, OC_RBRACK);
                } else if (a->oc->t == OC_IDENTIFIER
                           || a->oc->t == OC_STRING) {
                        /* key is literal text */
                        ainstr_load_const(a, a->oc);
                } else if (a->oc->t == OC_RBRACE) {
                        /* comma after last elem */
                        break;
                } else {
                        err_setstr(SyntaxError,
                                "Dictionary key must be either an identifier or string");
                        as_err(a, AE_EXPECT);
                }
                as_lex(a);
                if (a->oc->t != OC_COLON) {
                        err_setstr(SyntaxError, "Expected: ':'");
                        as_err(a, AE_EXPECT);
                }
                assemble_expr(a);
                count++;
                as_lex(a);
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RBRACE, AE_BRACE);

skip:
        add_instr(a, INSTR_DEFDICT, 0, count);
}

static void
assemble_fstring(struct assemble_t *a)
{
        int count = 0;
        do {
                assemble_expr(a);
                count++;
                as_lex(a);
        } while (a->oc->t == OC_FSTRING_CONTINUE);

        if (a->oc->t != OC_FSTRING_END) {
                /*
                 * XXX bug? tokenizer should have trapped
                 * unterminated quote
                 */
                err_setstr(SyntaxError, "Expected: end of f-string");
                as_err(a, AE_EXPECT);
        }

        add_instr(a, INSTR_DEFTUPLE, 0, count);
        ainstr_load_const(a, a->oc);
        add_instr(a, INSTR_FORMAT, 0, 0);
}

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
         * assemble_expr5_atomic will call us again for the grandparent,
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

        if (as_symbol_seek(a, name, NULL) >= 0) {
                /*
                 * 'atomic' instead of assemble_expr(), because if we see
                 * 'x.y', the closure should be x, not its descendant y.
                 * This way user-defined classes can use mutable closures
                 * like dictionaries to pass the same private data to
                 * multiple methods of the same instantiation.
                 */
                pos = as_swap_pos(a, pos);
                assemble_expr5_atomic(a);
                as_swap_pos(a, pos);

                /* back to identifier */
                add_instr(a, INSTR_ADD_CLOSURE, 0, 0);
                success = true;
        }

        as_frame_restore(a, this_frame);

        if (success)
                as_buffer_put_name(&a->fr->af_closures, name);

        /* try this again */
        return as_closure_seek(a, name);
}

/*
 * ainstr_load/assign_symbol
 *
 * @name:  name of symbol, token assumed to be saved from a->oc already.
 * @instr: either INSTR_LOAD, or INSTR_ASSIGN
 * @pos:   Saved token position when saving name; needed to maybe pass to
 *         seek_or_add const
 */
static void
ainstr_load_or_assign(struct assemble_t *a, struct token_t *name,
                      int instr, token_pos_t pos)
{
        int idx, arg;
        if ((idx = as_symbol_seek(a, name->s, &arg)) >= 0) {
                add_instr(a, instr, arg, idx);
        } else if ((idx = maybe_closure(a, name->s, pos)) >= 0) {
                add_instr(a, instr, IARG_PTR_CP, idx);
        } else {
                int namei = as_seek_rodata_tok(a, name);
                add_instr(a, instr, IARG_PTR_SEEK, namei);
        }
}

static void
ainstr_load_symbol(struct assemble_t *a, struct token_t *name, token_pos_t pos)
{
        ainstr_load_or_assign(a, name, INSTR_LOAD, pos);
}

static void
ainstr_assign_symbol(struct assemble_t *a, struct token_t *name, token_pos_t pos)
{
        ainstr_load_or_assign(a, name, INSTR_ASSIGN, pos);
}

static void
assemble_call_func(struct assemble_t *a)
{
        int argc = 0;
        int kwind = -1;
        as_errlex(a, OC_LPAR);

        do {
                as_lex(a);
                if (a->oc->t == OC_RPAR)
                        break;
                if (a->oc->t == OC_MUL) {
                        /*
                         * Atomic, not full eval.
                         * Certain starred args are too ambiguous.
                         * Require caller to express them as:
                         *
                         *      *(x.y)            not   *x.y
                         *      *([...].sort())   not   *[...].sort()
                         */
                        as_lex(a);
                        assemble_expr5_atomic(a);
                        add_instr(a, INSTR_DEFSTAR, 0, 0);
                        argc++;
                } else {
                        if (a->oc->t == OC_IDENTIFIER) {
                                as_lex(a);
                                if (a->oc->t == OC_EQ) {
                                        kwind = argc;
                                        as_unlex(a);
                                        as_unlex(a);
                                        break;
                                }
                                as_unlex(a);
                        }
                        as_unlex(a);
                        assemble_expr(a);
                        argc++;
                        as_lex(a);
                }
        } while (a->oc->t == OC_COMMA);

        if (kwind >= 0) {
                int count = 0;
                do {
                        as_lex(a);
                        if (a->oc->t == OC_RPAR)
                                break;
                        if (a->oc->t != OC_IDENTIFIER) {
                                err_setstr(SyntaxError, "Malformed keyword argument");
                                as_err(a, AE_GEN);
                        }
                        ainstr_load_const(a, a->oc);
                        as_lex(a);
                        if (a->oc->t != OC_EQ) {
                                err_setstr(SyntaxError,
                                           "Normal arguments may not follow keyword arguments");
                                as_err(a, AE_GEN);
                        }
                        assemble_expr(a);
                        count++;
                        as_lex(a);
                } while (a->oc->t == OC_COMMA);
                add_instr(a, INSTR_DEFDICT, 0, count);
                argc++;
        }

        as_err_if(a, a->oc->t != OC_RPAR, AE_PAR);

        /* stack from top is: [kw], argn...arg1, arg0, func */
        add_instr(a, INSTR_CALL_FUNC,
                  kwind >= 0 ? IARG_HAVE_DICT : IARG_NO_DICT, argc);
}

static void
assemble_expr5_atomic(struct assemble_t *a)
{
        switch (a->oc->t) {
        case OC_IDENTIFIER: {
                struct token_t namesav;
                token_pos_t pos = as_savetok(a, &namesav);
                ainstr_load_symbol(a, &namesav, pos);
                break;
        }

        case OC_INTEGER:
        case OC_BYTES:
        case OC_FLOAT:
        case OC_COMPLEX:
        case OC_STRING:
        case OC_TRUE:
        case OC_FALSE:
                ainstr_load_const(a, a->oc);
                break;
        case OC_FSTRING_START:
                assemble_fstring(a);
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

/* Check for indirection: things like a.b, a['b'], a[b], a(b)... */
static void
assemble_expr4_elems(struct assemble_t *a)
{
        assemble_expr5_atomic(a);
        assemble_primary_elements(a, FEE_EVAL);
}

static void
assemble_expr3_unarypre(struct assemble_t *a)
{
        if (istok_unarypre(a->oc->t)) {
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
                assemble_expr4_elems(a);

                if (op >= 0)
                        add_instr(a, op, 0, 0);
        } else {
                assemble_expr4_elems(a);
        }
}

/* terminate array of these with .tok = -1 */
struct token_to_opcode_t {
        int tok;
        int opcode;
};

struct operator_state_t {
        const struct token_to_opcode_t *toktbl;
        bool loop;
        int opcode; /* >= zero means "toktbl->opcode is arg1" */
};

/* Helper to assemble_expr2_binary - the recursive part */
static void
assemble_binary_operators_r(struct assemble_t *a,
                            const struct operator_state_t *tbl)
{
        if (tbl->toktbl == NULL) {
                /* carry on to unarypre and atom */
                assemble_expr3_unarypre(a);
                return;
        }

        /*
         * XXX: Policy: changing assemble_binary_operators' tables means
         * making sure this is still OK every time.
         */
        bool logical = tbl->opcode < 0 &&
                       (tbl->toktbl->opcode == INSTR_LOGICAL_AND ||
                         tbl->toktbl->opcode == INSTR_LOGICAL_OR);
        int label = -1;
        bool have_operator = 0;

        assemble_binary_operators_r(a, tbl + 1);
        do {
                const struct token_to_opcode_t *t;
                for (t = tbl->toktbl; t->tok >= 0; t++) {
                        if (a->oc->t == t->tok)
                                break;
                }
                if (t->tok < 0)
                        break;

                if (!have_operator && logical)
                        label = as_next_label(a);
                have_operator = true;

                if (logical) {
                        int cond = (t->opcode != INSTR_LOGICAL_AND)
                                     | IARG_COND_SAVEF | IARG_COND_DELF;
                        add_instr(a, INSTR_B_IF, cond, label);
                        bug_on(label < 0);
                }

                as_lex(a);
                assemble_binary_operators_r(a, tbl + 1);

                if (!logical) {
                        if (tbl->opcode < 0)
                                add_instr(a, t->opcode, 0, 0);
                        else
                                add_instr(a, tbl->opcode, t->opcode, 0);
                }

        } while (tbl->loop);

        if (logical && have_operator) {
                as_set_label(a, label);
                bug_on(label < 0);
        }
}

/* Parse and compile operators with left- and right-side operands */
static void
assemble_expr2_binary(struct assemble_t *a)
{
        static const struct token_to_opcode_t POW_TOK2OP[] = {
                { .tok = OC_POW,    .opcode = INSTR_POW },
                { .tok = -1 }
        };
        static const struct token_to_opcode_t MULDIVMOD_TOK2OP[] = {
                { .tok = OC_MUL,    .opcode = INSTR_MUL },
                { .tok = OC_DIV,    .opcode = INSTR_DIV },
                { .tok = OC_MOD,    .opcode = INSTR_MOD },
                { .tok = -1 }
        };
        static const struct token_to_opcode_t ADDSUB_TOK2OP[] = {
                { .tok = OC_PLUS,   .opcode = INSTR_ADD },
                { .tok = OC_MINUS,  .opcode = INSTR_SUB },
                { .tok = -1 }
        };
        static const struct token_to_opcode_t SHIFT_TOK2OP[] = {
                { .tok = OC_LSHIFT, .opcode = INSTR_LSHIFT },
                { .tok = OC_RSHIFT, .opcode = INSTR_RSHIFT },
                { .tok = -1 }
        };
        static const struct token_to_opcode_t CMP1_TOK2OP[] = {
                { .tok = OC_HAS,    .opcode = IARG_HAS },
                { .tok = OC_LEQ,    .opcode = IARG_LEQ },
                { .tok = OC_GEQ,    .opcode = IARG_GEQ },
                { .tok = OC_LT,     .opcode = IARG_LT },
                { .tok = OC_GT,     .opcode = IARG_GT },
                { .tok = -1 }
        };
        static const struct token_to_opcode_t CMP2_TOK2OP[] = {
                { .tok = OC_EQEQ,   .opcode = IARG_EQ },
                { .tok = OC_NEQ,    .opcode = IARG_NEQ },
                { .tok = OC_EQ3,    .opcode = IARG_EQ3 },
                { .tok = OC_NEQ3,   .opcode = IARG_NEQ3 },
                { .tok = -1 }
        };
        static const struct token_to_opcode_t BIT_AND_TOK2OP[] = {
                { .tok = OC_AND,    .opcode = INSTR_BINARY_AND },
                { .tok = -1 }
        };
        static const struct token_to_opcode_t BIT_XOR_TOK2OP[] = {
                { .tok = OC_AND,    .opcode = INSTR_BINARY_XOR },
                { .tok = -1 }
        };
        static const struct token_to_opcode_t BIT_OR_TOK2OP[] = {
                { .tok = OC_OR,     .opcode = INSTR_BINARY_OR },
                { .tok = -1 }
        };
        static const struct token_to_opcode_t LOG_AND_TOK2OP[] = {
                { .tok = OC_ANDAND, .opcode = INSTR_LOGICAL_AND, },
                { .tok = -1 }
        };
        static const struct token_to_opcode_t LOG_OR_TOK2OP[] = {
                { .tok = OC_OROR,   .opcode = INSTR_LOGICAL_OR, },
                { .tok = -1 }
        };
        static const struct operator_state_t BINARY_OPERATORS[] = {
                { LOG_OR_TOK2OP,    true,  -1 },
                { LOG_AND_TOK2OP,   true,  -1 },
                { BIT_OR_TOK2OP,    true,  -1 },
                { BIT_XOR_TOK2OP,   true,  -1 },
                { BIT_AND_TOK2OP,   true,  -1 },
                { CMP2_TOK2OP,      true,  INSTR_CMP },
                { CMP1_TOK2OP,      true,  INSTR_CMP },
                { SHIFT_TOK2OP,     true,  -1 },
                { ADDSUB_TOK2OP,    true,  -1 },
                { MULDIVMOD_TOK2OP, true,  -1 },
                { POW_TOK2OP,       true,  -1 },
                { NULL },
        };
        assemble_binary_operators_r(a, BINARY_OPERATORS);
}

static void
assemble_expr1_ternary(struct assemble_t *a)
{
        assemble_expr2_binary(a);
        if (a->oc->t == OC_QUEST) {
                int b_end = as_next_label(a);
                int b_if_false = as_next_label(a);
                add_instr(a, INSTR_B_IF, IARG_COND_DELF, b_if_false);
                as_lex(a);
                assemble_expr2_binary(a);
                add_instr(a, INSTR_B, 0, b_end);
                as_set_label(a, b_if_false);
                if (a->oc->t != OC_COLON) {
                        err_setstr(SyntaxError,
                                   "Expected: ':' in ternary expression");
                        as_err(a, AE_GEN);
                }
                as_lex(a);
                assemble_expr2_binary(a);
                as_set_label(a, b_end);
        }
}

/**
 * assemble_expr - sister function to assemble_stmt.
 *
 * This and its assemble_exprN_XXX descendants form a recursive-descent
 * parser that builds up the instructions for evaluating the EXPR part of
 * a statement (see big comment in assemble_stmt).
 *
 * This has five levels of recursive descent:
 *
 *      ..... atom                 assemble_expr5_atomic()
 *       .... primary elements     assemble_expr4_elems()
 *        ... unary operators      assemble_expr3_unarypre()
 *         .. binary operators     assemble_expr2_binary()
 *          . ternary operators    assemble_expr1_ternary()
 *
 * In fact it recurses much deeper, however, since
 * 1. assemble_expr2_binary()'s helper recurses in on itself before
 *    descending into assemble_expr3_unarypre(), and
 * 2. assemble_expr5_atomic() could, and at the top level likely will,
 *    recurse into assemble_stmt() again.
 */
static void
assemble_expr(struct assemble_t *a)
{
        as_lex(a);
        assemble_expr1_ternary(a);
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
        /* first check the ones that don't call assemble_expr */
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
                bug_on(t == OC_EQ || !istok_assign(t));
                assemble_expr(a);
                add_instr(a, asgntok2instr(t), 0, 0);
        }
}

static int
maybe_modattr(struct assemble_t *a, unsigned int flags)
{
        if (flags == FEE_DEL) {
                int t = as_lex(a);
                if (istok_indirection(t)) {
                        as_unlex(a);
                        return 0;
                }
                add_instr(a, INSTR_DELATTR, 0, 0);
                return 1;
        } else if (flags == FEE_ASGN) {
                int t = as_lex(a);
                if (istok_assign(t)) {
                        if (t == OC_EQ) {
                                assemble_expr(a);
                        } else {
                                add_instr(a, INSTR_LOADATTR, 0, 0);
                                assemble_preassign(a, t);
                        }
                        add_instr(a, INSTR_SETATTR, 0, 0);
                        return 1;
                }
                as_unlex(a);
                return 0;
        } else {
                bug_on(flags != FEE_EVAL);
                return 0;
        }
}

/*
 * Descend the rabbit hole of 'a.b[c].d().e' monsters.
 * @may_assign: false if we are in the assemble_exprN() loop above
 *              true if called from assemble_ident|this() below
 *
 * Return: 1 if an evaluated item is dangling on the stack, 0 if not
 */
static int
assemble_primary_elements(struct assemble_t *a, unsigned int flags)
{
        flags &= FEE_MASK;
        while (istok_indirection(a->oc->t)) {
                switch (a->oc->t) {
                case OC_PER:
                        as_errlex(a, OC_IDENTIFIER);
                        ainstr_load_const(a, a->oc);
                        if (maybe_modattr(a, flags))
                                return 0;
                        add_instr(a, INSTR_GETATTR, 0, 0);
                        break;

                case OC_LBRACK:
                        assemble_slice(a);
                        if (as_lex(a) == OC_RBRACK) {
                                if (maybe_modattr(a, flags))
                                        return 0;
                                as_unlex(a);
                        }
                        add_instr(a, INSTR_GETATTR, 0, 0);
                        as_errlex(a, OC_RBRACK);
                        break;

                case OC_LPAR:
                        as_unlex(a);
                        assemble_call_func(a);
                        /*
                         * XXX: I don't know a faster way to do this
                         * without spaghettifying the code.
                         */
                        if (flags == FEE_DEL &&
                            !istok_indirection(as_peek(a, false))) {
                                err_setstr(SyntaxError,
                                           "Trying to delete function call");
                                as_err(a, AE_GEN);
                        }
                        break;

                default:
                        as_err(a, AE_BADTOK);
                }

                as_lex(a);
        }

        if (!!flags && a->oc->t == OC_SEMI)
                as_unlex(a);

        return 1;
}

static int
assemble_primary_elements__(struct assemble_t *a)
{
        as_lex(a);
        if (a->oc->t == OC_SEMI) {
                as_unlex(a);
                return 1;
        }
        return assemble_primary_elements(a, FEE_ASGN);
}

/* return 1 if item left on the stack, 0 if not */
static int
assemble_this(struct assemble_t *a, unsigned int flags)
{
        /*
         * Cf. assemble_identifier below.
         * We do not allow
         *      this = value...
         */
        add_instr(a, INSTR_LOAD, IARG_PTR_THIS, 0);
        return assemble_primary_elements__(a);
}

/* return 1 if item left on the stack, 0 if not */
static int
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
                assemble_expr(a);
                ainstr_assign_symbol(a, &name, pos);
                return 0;
        } else if (istok_assign(a->oc->t)) {
                /*
                 * x++;
                 * x += value;
                 * ...
                 */
                ainstr_load_symbol(a, &name, pos);
                assemble_preassign(a, a->oc->t);
                ainstr_assign_symbol(a, &name, pos);
                return 0;
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
                return assemble_primary_elements__(a);
        }
}

static void
assemble_delete(struct assemble_t *a)
{
        struct token_t name;
        token_pos_t pos;

        as_lex(a);
        pos = as_savetok(a, &name);

        as_lex(a);
        if (istok_indirection(a->oc->t)) {
                if (name.t != OC_THIS && name.t != OC_IDENTIFIER)
                        goto baddelete;
                ainstr_load_symbol(a, &name, pos);
                assemble_primary_elements(a, FEE_DEL);
        } else {
                /*
                 * Cannot delete variables, but we can reduce their
                 * memory footprint by reassigning them to NULL.
                 *
                 * XXX REVISIT: Deletion is not impossible, nor am I
                 * following EMCAScript, so maybe allow true deletion
                 * from the namespace after all; it would allow
                 * redeclaration, which could come in handy.
                 */
                if (name.t != OC_IDENTIFIER)
                        goto baddelete;

                as_unlex(a);
                ainstr_load_symbol(a, &name, pos);
                add_instr(a, INSTR_PUSH_LOCAL, 0, 0);
                ainstr_assign_symbol(a, &name, pos);
        }
        return;

baddelete:
        /* back up for more accurate error reporting */
        as_unlex(a);
        err_setstr(SyntaxError, "Invalid expression for delete");
        as_err(a, AE_GEN);
}

/* common to assemble_declarator_stmt and assemble_foreach */
static int
assemble_declare(struct assemble_t *a, struct token_t *name, bool global)
{
        int namei;
        bug_on(global && name == NULL);
        if (global) {
                namei = as_seek_rodata_tok(a, name);
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
                err_setstr(SyntaxError,
                        "'%s' not allowed as third part of 'for' statement",
                        what);
                as_err(a, AE_BADTOK);
        }

        as_lex(a);
        if (a->oc->t != OC_IDENTIFIER) {
                char *what = tok == OC_LET ? "let" : "global";
                err_setstr(SyntaxError,
                           "'%s' must be followed by an identifier", what);
                as_err(a, AE_EXPECT);
        }
        pos = as_savetok(a, &name);
        namei = assemble_declare(a, &name, tok == OC_GBL);

        /* if no assign, return early */
        if (as_peek(a, false) == OC_SEMI)
                return;

        /* for initializers, only '=', not '+=' or such */
        as_errlex(a, OC_EQ);

        /* XXX: is the extra LOAD/POP necessary? */
        ainstr_load_symbol(a, &name, pos);
        assemble_expr(a);
        add_instr(a, INSTR_ASSIGN,
                  tok == OC_LET ? IARG_PTR_AP : IARG_PTR_SEEK, namei);
        add_instr(a, INSTR_POP, 0, 0);
}

static void
assemble_return(struct assemble_t *a)
{
        if (as_peek(a, false) == OC_SEMI) {
                ainstr_return_null(a);
        } else {
                assemble_expr(a);
                add_instr(a, INSTR_RETURN_VALUE, 0, 0);
        }
}

static void
assemble_try(struct assemble_t *a)
{
        struct token_t exctok;
        int finally = as_next_label(a);
        int catch = as_next_label(a);

        ainstr_push_block(a, IARG_TRY, catch);

        /* block of the try { ... } statement */
        assemble_stmt(a, 0, 0);
        add_instr(a, INSTR_B, 0, finally);

        ainstr_pop_block(a);

        as_errlex(a, OC_CATCH);
        as_set_label(a, catch);

        /*
         * block of the catch(x) { ... } statement
         *
         * extra block push to prevent stack confusion about
         * declared stack exception below.
         * XXX Overkill? is it not safe to just add a POP below?
         */
        ainstr_push_block(a, IARG_BLOCK, 0);

        as_errlex(a, OC_LPAR);
        as_errlex(a, OC_IDENTIFIER);
        as_savetok(a, &exctok);
        as_errlex(a, OC_RPAR);
        /*
         * No instructions for pushing this on the stack.
         * The exception handler will do that for us in
         * execute loop.
         */
        fakestack_declare(a, exctok.s);

        assemble_stmt(a, 0, 0);

        ainstr_pop_block(a);

        as_lex(a);

        as_set_label(a, finally);

        if (a->oc->t == OC_FINALLY) {
                /* block of the finally { ... } statement */
                assemble_stmt(a, 0, 0);
        } else {
                as_unlex(a);
        }
}

static void
assemble_if(struct assemble_t *a)
{
        int true_jmpend = as_next_label(a);
        int jmpelse = as_next_label(a);
        /*
         * The 'if' of 'else if' is technically the start of its own
         * statement, so we could do this recursively and more simply,
         * but let's instead be friendlier to the stack.
         */
        while (a->oc->t == OC_IF) {
                int jmpend = as_next_label(a);
                assemble_expr(a);
                add_instr(a, INSTR_B_IF, IARG_COND_DELF, jmpelse);
                assemble_stmt(a, 0, 0);
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
        assemble_stmt(a, 0, 0);

done:
        as_set_label(a, true_jmpend);
}

static void
assemble_while(struct assemble_t *a)
{
        int start = as_next_label(a);
        int breakto = as_next_label(a);

        ainstr_push_block(a, IARG_LOOP, breakto);

        as_set_label(a, start);

        as_errlex(a, OC_LPAR);
        assemble_expr(a);
        as_errlex(a, OC_RPAR);

        add_instr(a, INSTR_B_IF, IARG_COND_DELF, breakto);
        assemble_stmt(a, FE_CONTINUE, start);
        add_instr(a, INSTR_B, 0, start);

        ainstr_pop_block(a);

        as_set_label(a, breakto);
}

static void
assemble_do(struct assemble_t *a)
{
        int start = as_next_label(a);
        int breakto = as_next_label(a);

        ainstr_push_block(a, IARG_LOOP, breakto);

        as_set_label(a, start);
        assemble_stmt(a, FE_CONTINUE, start);
        as_errlex(a, OC_WHILE);
        assemble_expr(a);
        add_instr(a, INSTR_B_IF, 1, start);

        ainstr_pop_block(a);

        as_set_label(a, breakto);
}

static void
assemble_foreach(struct assemble_t *a)
{
        struct token_t needletok;
        int breakto = as_next_label(a);
        int forelse = as_next_label(a);
        int iter    = as_next_label(a);

        ainstr_push_block(a, IARG_LOOP, breakto);

        /* save name of the 'needle' in 'for(needle, haystack)' */
        as_errlex(a, OC_IDENTIFIER);
        as_savetok(a, &needletok);

        as_errlex(a, OC_COMMA);

        /* declare 'needle', push placeholder onto the stack */
        assemble_declare(a, &needletok, false);

        /* push 'haystack' onto the stack */
        assemble_expr(a);
        as_errlex(a, OC_RPAR);
        fakestack_declare(a, NULL);

        /* maybe replace 'haystack' with its keys */
        add_instr(a, INSTR_FOREACH_SETUP, 0, 0);

        /* push 'i' iterator onto the stack beginning at zero */
        ainstr_load_const_int(a, 0LL);
        fakestack_declare(a, NULL);

        as_set_label(a, iter);
        add_instr(a, INSTR_FOREACH_ITER, 0, forelse);

        assemble_stmt(a, FE_CONTINUE, iter);

        add_instr(a, INSTR_B, 0, iter);

        as_set_label(a, forelse);

        as_lex(a);
        if (a->oc->t == OC_EOF) {
                as_badeof(a);
        } else if (a->oc->t == OC_ELSE) {
                assemble_stmt(a, 0, 0);
        } else {
                as_unlex(a);
        }

        ainstr_pop_block(a);

        as_set_label(a, breakto);
}

/*
 * breakto_else here is for the unusual case if break is encountered inside
 * in the `else' of a `for...else' block, otherwise it isn't used.
 */
static void
assemble_for_cstyle(struct assemble_t *a)
{
        int start   = as_next_label(a);
        int then    = as_next_label(a);
        int breakto = as_next_label(a);
        int iter    = as_next_label(a);
        int forelse = as_next_label(a);

        ainstr_push_block(a, IARG_LOOP, breakto);

        /* initializer */
        assemble_stmt(a, 0, 0);

        as_set_label(a, start);
        as_lex(a);
        if (a->oc->t == OC_EOF) {
                as_badeof(a);
        } else if (a->oc->t == OC_SEMI) {
                /* empty condition, always true */
                add_instr(a, INSTR_B, 0, then);
        } else {
                as_unlex(a);
                assemble_expr(a);
                as_errlex(a, OC_SEMI);
                add_instr(a, INSTR_B_IF, 0, forelse);
                add_instr(a, INSTR_B, 0, then);
        }
        as_set_label(a, iter);
        assemble_stmt(a, FE_FOR, 0);
        as_errlex(a, OC_RPAR);

        add_instr(a, INSTR_B, 0, start);
        as_set_label(a, then);
        assemble_stmt(a, FE_CONTINUE, iter);
        add_instr(a, INSTR_B, 0, iter);

        as_set_label(a, forelse);

        as_lex(a);
        if (a->oc->t == OC_EOF) {
                as_badeof(a);
        } else if (a->oc->t == OC_ELSE) {
                assemble_stmt(a, 0, 0);
        } else {
                as_unlex(a);
        }

        ainstr_pop_block(a);

        as_set_label(a, breakto);
}

static void
assemble_for(struct assemble_t *a)
{
        /* do some peeking to see which kind of 'for'
         * statement this is.
         */
        as_errlex(a, OC_LPAR);
        as_lex(a);
        if (a->oc->t == OC_IDENTIFIER) {
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
        assemble_for_cstyle(a);
}

static void
assemble_throw(struct assemble_t *a)
{
        assemble_expr(a);
        add_instr(a, INSTR_THROW, 0, 0);
}

/*
 * parse '{' stmt; stmt;... '}'
 * The first '{' has already been read.
 */
static void
assemble_block_stmt(struct assemble_t *a, unsigned int flags,
                    int continueto)
{
        int arg1, arg2;
        if (!!(flags & FE_CONTINUE)) {
                arg1 = IARG_CONTINUE;
                arg2 = continueto;
        } else {
                arg1 = IARG_BLOCK;
                arg2 = 0;
        }

        ainstr_push_block(a, arg1, arg2);

        /* don't pass this down */
        flags &= ~FE_CONTINUE;

        for (;;) {
                /* peek for end of compound statement */
                as_lex(a);
                if (a->oc->t == OC_RBRACE)
                        break;
                as_unlex(a);

                assemble_stmt(a, flags, -1);
        }
        ainstr_pop_block(a);
}

/* parse the stmt of 'stmt' + ';' */
static void
assemble_stmt_simple(struct assemble_t *a, unsigned int flags,
                     int continueto)
{
        int need_pop = 0;
        int pop_arg = !!(flags & FE_TOP) ? IARG_POP_PRINT : IARG_POP_NORMAL;

        flags &= ~FE_TOP;

        as_lex(a);
        /* cases return early if semicolon not expected at the end */
        switch (a->oc->t) {
        case OC_DELETE:
                assemble_delete(a);
                return;
        case OC_EOF:
                return;
        case OC_IDENTIFIER:
                need_pop = assemble_identifier(a, flags);
                break;
        case OC_THIS:
                /* not a saucy challenge */
                need_pop = assemble_this(a, flags);
                break;
        case OC_SEMI:
                /* empty statement */
                as_unlex(a);
                break;
        case OC_RPAR:
                /* in case for loop ends with empty ";)" */
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
                add_instr(a, INSTR_BREAK, 0, 0);
                break;
        case OC_CONTINUE:
                add_instr(a, INSTR_CONTINUE, 0, 0);
                break;
        case OC_THROW:
                assemble_throw(a);
                break;
        case OC_TRY:
                assemble_try(a);
                return;
        case OC_IF:
                assemble_if(a);
                return;
        case OC_WHILE:
                assemble_while(a);
                return;
        case OC_FOR:
                assemble_for(a);
                return;
        case OC_LBRACE:
                assemble_block_stmt(a, flags, continueto);
                return;
        case OC_DO:
                assemble_do(a);
                return;
        default:
                /* value expression */
                as_unlex(a);
                assemble_expr(a);
                need_pop = 1;
                break;
        }

        /* Throw result away */
        if (need_pop)
                add_instr(a, INSTR_POP, pop_arg, 0);

        if (!(flags & FE_FOR))
                as_errlex(a, OC_SEMI);
}

RECURSION_DECLARE(as_recursion);

/*
 * assemble_stmt - Parser for the top-level statement
 * @flags: If FE_FOR, we're in the iterator part of a for loop header.
 *
 * This covers block statement and single-line statements
 *
 *      single-line expr:       STMT ';'
 *      block:                  '{' STMT ';' STMT ';'... '}'
 *
 * See Tutorial.rst for the details.
 */
static void
assemble_stmt(struct assemble_t *a, unsigned int flags, int continueto)
{
        RECURSION_DEFAULT_START(as_recursion);

        assemble_stmt_simple(a, flags, continueto);

        RECURSION_END(as_recursion);
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
        assemble_frame_push(a, as_next_funcno(a));
        return a;
}

static void
as_delete_frame_list(struct list_t *parent_list)
{
        struct list_t *li, *tmp;
        list_foreach_safe(li, tmp, parent_list) {
                struct as_frame_t *fr = list2frame(li);
                list_remove(&fr->list);
                /*
                 * These buffers are arrays of pointers.  We only need to
                 * free the arrays; the actual pointers are maintained by
                 * token.c, which frees them in due time.
                 */
                buffer_free(&fr->af_locals);
                buffer_free(&fr->af_args);
                buffer_free(&fr->af_closures);

                /*
                 * These are safe to free, because if we aren't unwinding
                 * due to failure, buffer_trim reset these already.
                 */
                buffer_free(&fr->af_rodata);
                buffer_free(&fr->af_labels);
                buffer_free(&fr->af_instr);

                efree(fr);
        }
}

/**
 * free_assembler - Free an assembler state machine
 * @a:          Handle to the assembler state machine
 * @err:        Zero to keep the executable opcodes in memory,
 *              non-zero to delete those also.
 */
static void
free_assembler(struct assemble_t *a)
{
        as_delete_frame_list(&a->active_frames);
        as_delete_frame_list(&a->finished_frames);
        token_state_free(a->prog);
        efree(a);
}

/* Tell user where they screwed up */
static void
assemble_splash_error(struct assemble_t *a)
{
        int col;
        char *line = NULL;
        int lineno;

        bug_on(!err_occurred());

        err_print_last(stderr);
        lineno = a->oc ? a->oc->line : 1;
        fprintf(stderr, "in file '%s' near line '%d'\n",
                a->file_name, lineno);
        line = token_get_this_line(a->prog, &col);
        if (line) {
                fprintf(stderr, "Suspected error location:\n");
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

        if (a->oc && a->oc->t == OC_EOF) {
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
                        err_setstr(SyntaxError, "%s", msg);

                assemble_splash_error(a);

                /*
                 * TODO: probably more meticulous cleanup needed here,
                 * we don't know exactly where we failed.
                 */
                *status = RES_ERROR;
                ex = NULL;
        } else {
                do {
                        assemble_stmt(a, toeof ? 0 : FE_TOP, -1);
                } while (toeof && a->oc->t != OC_EOF);
                add_instr(a, INSTR_END, 0, 0);

                list_remove(&a->fr->list);
                list_add_front(&a->fr->list, &a->finished_frames);

                ex = assemble_post(a);

                *status = RES_OK;
        }

        RECURSION_RESET(as_recursion);

        return ex;
}

/* **********************************************************************
 *                      API functions
 ***********************************************************************/
/*
 * assemble() is the only intentional API function, the rest below are
 * forward-declared in assemble_priv.h and used by assemble_post.c and
 * reassemble.c
 */

void
assemble_label_here(struct assemble_t *a)
{
        as_set_label(a, as_next_label(a));
}

void
assemble_add_instr(struct assemble_t *a, int opcode, int arg1, int arg2)
{
        add_instr(a, opcode, arg1, arg2);
}

int
assemble_frame_next_label(struct as_frame_t *fr)
{
        /* XXX initialize to < 0 to sanity check later? */
        short v = 0;
        buffer_putd(&fr->af_labels, &v, sizeof(short));
        return as_frame_nlabel(fr) - 1;
}

void
assemble_frame_set_label(struct as_frame_t *fr, int jmp, unsigned long val)
{
        unsigned short *data;
        bug_on(jmp >= as_frame_nlabel(fr));
        data = (unsigned short *)fr->af_labels.s;
        /*
         * Limit to 15 bits instead of 16, because the final jump arg
         * will be directional.  We need an extended-arg method for
         * instructions before we can support Functions of Unusual Size.
         */
        bug_on(val > 32767);
        data[jmp] = (unsigned short)val;
}

/* will return -1 and set exc. if line is longer than @max */
ssize_t
assemble_get_line(struct assemble_t *a, struct token_t *toks,
                  size_t max, int *lineno)
{
        int line = 0;
        int count = 0;
        while (count < max) {
                as_lex(a);
                if (a->oc->t == OC_EOF)
                        break;
                if (count == 0) {
                        line = a->oc->line;
                } else if (a->oc->line != line) {
                        as_unlex(a);
                        break;
                }
                as_savetok(a, &toks[count]);
                count++;
        }
        if (count == max) {
                err_setstr(SyntaxError, "line %d too long", line);
                count = -1;
        }
        *lineno = line;
        return count;
}

/* extern linkage because assemble_post.c needs it */
int
assemble_seek_rodata(struct assemble_t *a, Object *v)
{
        Object **data = as_frame_rodata(a->fr);
        int i, n = as_frame_nconst(a->fr);

        bug_on(!v);
        for (i = 0; i < n; i++) {
                /* var_compare thinks 2 == 2.0, don't allow that */
                if (v->v_type != data[i]->v_type)
                        continue;
                if (var_compare(v, data[i]) == 0)
                        break;
        }

        /* No more than one reference to unique ID can exist in code */
        bug_on(v->v_type == &IdType && i != n);

        if (i == n) {
                VAR_INCR_REF(v);
                as_buffer_put_ptr(&a->fr->af_rodata, v);
        }

        return i;
}

void
assemble_frame_push(struct assemble_t *a, long long funcno)
{
        struct as_frame_t *fr;

        fr = emalloc(sizeof(*fr));
        memset(fr, 0, sizeof(*fr));

        /* memset did this, but just in case buffer.c internals change... */
        buffer_init(&fr->af_closures);
        buffer_init(&fr->af_locals);
        buffer_init(&fr->af_args);
        buffer_init(&fr->af_rodata);
        buffer_init(&fr->af_labels);
        buffer_init(&fr->af_instr);

        fr->funcno = funcno;
        fr->line = a->oc ? a->oc->line : 1;

        list_init(&fr->list);
        list_add_tail(&fr->list, &a->active_frames);

        a->fr = fr;
}

/*
 * Doesn't destroy it, it just removes it from active list.
 * We'll iterate through these when we're done.
 */
void
assemble_frame_pop(struct assemble_t *a)
{
        struct as_frame_t *fr = a->fr;
        bug_on(list_is_empty(&a->active_frames));

        /*
         * first to start will be last to finish, so prepending these
         * instead of appending them will make it easier to put the entry
         * point first.
         */
        list_remove(&fr->list);
        list_add_front(&fr->list, &a->finished_frames);

        /* list is empty in reassemble mode */
        if (!list_is_empty(&a->active_frames))
                a->fr = list2frame(a->active_frames.prev);
}

#if DBUG_PROFILE_LOAD_TIME
# define CLOCK_DECLARE() clock_t __FUNCTION__##clk
# define CLOCK_SAVE() \
  do { \
          __FUNCTION__##clk = clock(); \
  } while (0)
# define CLOCK_REPORT() \
  do { \
          DBUG("That took %lld clocks", \
               (long long)(clock() - __FUNCTION__##clk)); \
  } while (0)
#else
# define CLOCK_DECLARE()  do { (void)0; } while (0)
# define CLOCK_SAVE()     do { (void)0; } while (0)
# define CLOCK_REPORT()   do { (void)0; } while (0)
#endif

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
Object *
assemble(const char *filename, FILE *fp, bool toeof, int *status)
{
        int localstatus;
        struct xptrvar_t *ret;
        struct assemble_t *a;
        CLOCK_DECLARE();

        a = new_assembler(filename, fp);
        if (!a)
                return NULL;

        /*
         * If first token is a dot, it can't be EvilCandy source,
         * but it could be a disassembly.
         * XXX: toeof wasn't meant to be synonymous with !(interactive mode)
         */
        CLOCK_SAVE();
        if (toeof && as_peek(a, true) == OC_PER) {
                ret = reassemble(a);
                /* reassemble can only succeed or fail */
                if (!ret) {
                        err_print_last(stderr);
                        localstatus = RES_ERROR;
                } else {
                        localstatus = RES_OK;
                }
        } else {
                ret = assemble_next(a, toeof, &localstatus);
        }
        CLOCK_REPORT();

        /* status cannot be OK if ret is NULL and toeof is true */
        bug_on(toeof && ret == NULL && localstatus == RES_OK);
        bug_on(localstatus == RES_OK && err_occurred());

        if (status)
                *status = localstatus;

        /*
         * FIXME: Interactive mode issue.  This clears the token state
         * machine even if it still contains unread data on the same
         * line, eg. someone typed: "a = 1; b = 2;"... the second
         * statement will not be preserved to be executed in the next
         * pass to assemble().
         */
        free_assembler(a);
        return (Object *)ret;
}

