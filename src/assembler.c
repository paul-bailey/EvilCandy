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
 * @FE_CONTINUE: We're the start of a loop where 'continue' may
 *          break us out.
 * @FE_TOP: Two things must be true: 1. We're in interactive mode,
 *          AND 2. we're at the top-level statement
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
        FE_CONTINUE     = 0x02,
        FE_TOP          = 0x04,
        FE_SKIPNULLASSIGN=0x08,

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

static void
err_ae_expect(struct assemble_t *a, int exp)
{
        err_setstr(SyntaxError, "expected '%s' but got '%s'",
                   token_name(exp), token_name(a->oc->t));
}

static int
as_errlex(struct assemble_t *a, int exp)
{
        as_lex(a);
        if (a->oc->t != exp) {
                err_ae_expect(a, exp);
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

static bool
as_is_ialocal(struct assemble_t *a, Object *name)
{
        if (!a->localdict)
                return false;
        bug_on(!isvar_string(name));
        /*
         * Only true if we're in the top-level function;
         * We'll need to convert it into a closure otherwise.
         */
        if (a->fr->list.prev != &a->active_frames)
                return false;
        return var_hasattr(a->localdict, name);
}

static int
as_closure_seek(struct assemble_t *a, Object *name)
{
        return array_indexof(a->fr->af_closures, name);
}

static size_t
localmap_size(struct assemble_t *a)
{
        return buffer_size(&a->fr->af_localmap) / sizeof(int);
}

static int
localmap_idx(struct assemble_t *a, int idx)
{
        int *arr = (int *)(a->fr->af_localmap.s);
        bug_on(!arr);
        bug_on(idx > localmap_size(a));

        /*
         * this should always returns the same number or greater,
         * never lesser.
         */
        bug_on(idx > arr[idx]);

        idx = arr[idx];
        bug_on(idx >= a->fr->af_nlocals);
        return idx;
}

static void
localmap_pop(struct assemble_t *a)
{
        struct buffer_t *b = &a->fr->af_localmap;
        bug_on(b->p < sizeof(int));
        b->p -= sizeof(int);
}

static void
localmap_push(struct assemble_t *a, int idx)
{
        buffer_putd(&a->fr->af_localmap, &idx, sizeof(int));
}

/* arg may be NULL, it's the arg1 for LOAD/ASSIGN commands & such */
static int
as_symbol_seek(struct assemble_t *a, Object *name, int *arg)
{
        ssize_t i;
        int targ;
        struct as_frame_t *fr = a->fr;

        bug_on(!name || !isvar_string(name));
        /*
         * In the case of .af_locals, we search backwards, since locals
         * inside the current program flow block "{...}" take precedence
         * over higher-up locals.
         */
        if ((i = array_rindexof(fr->af_locals, name)) >= 0) {
                i = localmap_idx(a, i);
                targ = IARG_PTR_AP;
                goto found;
        }
        if ((i = array_indexof(fr->af_args, name)) >= 0) {
                targ = IARG_PTR_FP;
                goto found;
        }
        if ((i = array_indexof(fr->af_closures, name)) >= 0) {
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

static void
ainstr_load_const_obj(struct assemble_t *a, Object *obj)
{
        int idx = assemble_seek_rodata(a, obj);
        VAR_DECR_REF(obj);
        add_instr(a, INSTR_LOAD_CONST, 0, idx);
}

static void
ainstr_load_null(struct assemble_t *a)
{
        ainstr_load_const_obj(a, VAR_NEW_REF(NullVar));
}

/*
 * like ainstr_load_const but from an integer, not token, since
 * loading zero is common enough.
 */
static void
ainstr_load_const_int(struct assemble_t *a, long long ival)
{
        Object *iobj;

        if (ival == 1)
                iobj = VAR_NEW_REF(gbl.one);
        else if (ival == 0)
                iobj = VAR_NEW_REF(gbl.zero);
        else
                iobj = intvar_new(ival);
        ainstr_load_const_obj(a, iobj);
}

static void
ainstr_push_block(struct assemble_t *a, int arg1, int arg2)
{
        struct as_frame_t *fr = a->fr;
        if (fr->nest >= FRAME_NEST_MAX)
                as_err(a, AE_OVERFLOW);

        fr->scope[fr->nest++] = fr->fp;

        fr->fp = seqvar_size(fr->af_locals);
        if (arg1 != IARG_BLOCK)
                add_instr(a, INSTR_PUSH_BLOCK, arg1, arg2);
}

/* helper to ainstr_pop_block */
static inline void
array_pop_to(Object *arr, size_t newsize)
{
        while (seqvar_size(arr) > newsize)
                array_setitem(arr, seqvar_size(arr)-1, NULL);
}

static void
ainstr_pop_block(struct assemble_t *a, int kind)
{
        struct as_frame_t *fr = a->fr;
        size_t reduce = seqvar_size(fr->af_locals) - fr->fp;
        bug_on(fr->nest <= 0);
        bug_on((int)reduce < 0);

        array_pop_to(fr->af_locals, fr->fp);
        while (reduce--)
                localmap_pop(a);
        fr->nest--;

        fr->fp = fr->scope[fr->nest];
        if (kind != IARG_BLOCK)
                add_instr(a, INSTR_POP_BLOCK, 0, 0);
}

static void
ainstr_return_null(struct assemble_t *a)
{
        /*
         * Identical to LOAD_CONST (null) and RETURN_VALUE, but this is
         * one instruction fewer.
         */
        add_instr(a, INSTR_END, 0, 0);
}

/*
 * Declare a local variable.
 * @name is name of variable being declared.
 *
 * XXX REVISIT: This has an old, now misleading, function name
 */
static int
fakestack_declare(struct assemble_t *a, Object *name)
{
        int idx;
        if (name && as_symbol_seek(a, name, NULL) >= 0) {
                err_setstr(SyntaxError, "Redefining variable ('%s')", name);
                as_err(a, AE_GEN);
                return 0;
        }

        bug_on(!name);
        idx = a->fr->af_nlocals;
        array_append(a->fr->af_locals, name);
        array_append(a->fr->af_names, name);
        localmap_push(a, idx);
        bug_on(localmap_size(a) != seqvar_size(a->fr->af_locals));
        a->fr->af_nlocals++;
        return idx;
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
                                ainstr_load_null(a);
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

        /* placeholder for XptrType, resolved in assemble_frame_to_xptr() */
        ainstr_load_const_obj(a, idvar_new(funcno));
        add_instr(a, INSTR_DEFFUNC, 0, 0);
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
                        optarg = seqvar_size(a->fr->af_args);
                        as_lex(a);
                } else if (a->oc->t == OC_POW) {
                        kind = KWIND;
                        kwarg = seqvar_size(a->fr->af_args);
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

                Object *name = a->oc->v;
                as_lex(a);
                array_append(a->fr->af_args, name);
                minargs = seqvar_size(a->fr->af_args);
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RPAR, AE_PAR);

        bug_on(kwarg == optarg && kwarg >= 0);

        assemble_function(a, lambda, funcno);

        /* for user functions, minargs == maxargs */
        bug_on(minargs != seqvar_size(a->fr->af_args));

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
        bool have_star = false;

        as_lex(a);
        if (a->oc->t == OC_RBRACK) /* empty array */
                goto done;
        as_unlex(a);

        do {
                /*
                 * sloppy ",]", but allow it anyway,
                 * since everyone else does.
                 */
                bool star_here = false;
                if (as_peek(a, false) == OC_RBRACK) {
                        as_lex(a);
                        break;
                }
                if (as_peek(a, false) == OC_MUL) {
                        as_lex(a);
                        add_instr(a, INSTR_DEFLIST, 0, n_items);
                        have_star = true;
                        star_here = true;
                }
                assemble_expr(a);
                if (have_star) {
                        int instr;
                        if (star_here)
                                instr = INSTR_LIST_EXTEND;
                        else
                                instr = INSTR_LIST_APPEND;
                        add_instr(a, instr, 0, 0);
                }
                as_lex(a);
                n_items++;
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RBRACK, AE_BRACK);

done:
        if (!have_star)
                add_instr(a, INSTR_DEFLIST, 0, n_items);
}

static void
assemble_tupledef(struct assemble_t *a)
{
        int n_items = 0;
        bool has_comma;
        bool have_star = false;
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
                bool star_here = false;
                if (as_peek(a, false) == OC_RPAR) {
                        as_lex(a);
                        has_comma = true;
                        break;
                }
                if (as_peek(a, false) == OC_MUL) {
                        as_lex(a);
                        add_instr(a, INSTR_DEFLIST, 0, n_items);
                        have_star = true;
                        star_here = true;
                }
                assemble_expr(a);
                if (have_star) {
                        int instr;
                        if (star_here)
                                instr = INSTR_LIST_EXTEND;
                        else
                                instr = INSTR_LIST_APPEND;
                        add_instr(a, instr, 0, 0);
                }
                as_lex(a);
                n_items++;
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RPAR, AE_PAR);

        /* "(x,)" is a tuple.  "(x)" is whatever x is */
        if (!has_comma && n_items == 1) {
                if (have_star) {
                        err_setstr(SyntaxError,
                                "cannot use starred expression here");
                        as_err(a, AE_EXPECT);
                }
                return;
        }
done:
        if (have_star) {
                add_instr(a, INSTR_CAST_TUPLE, 0, 0);
        } else {
                add_instr(a, INSTR_DEFTUPLE, 0, n_items);
        }
}

/*
 * called from assemble_setdef() as soon as it finds out it's
 * parsing a dict instead of a set
 */
static void
assemble_dictdef(struct assemble_t *a, int count, int where)
{
        switch (where) {
        case 1:
                goto where_1;
        case 2:
                goto where_2;
        case 3:
                goto where_3;
        default:
                bug();
        }

        do {
                as_lex(a);

                if (a->oc->t == OC_LBRACK) {
where_1:
                        /* computed key */
                        assemble_expr(a);
                        as_errlex(a, OC_RBRACK);
                } else if (istok_atomic_key(a->oc->t)) {
                        /* key is literal text */
where_2:
                        ainstr_load_const(a, a->oc);
                } else if (a->oc->t == OC_RBRACE) {
                        /* comma after last elem */
                        break;
                } else {
                        err_setstr(SyntaxError,
                                "Invalid token for uncomputed dictionary key");
                        as_err(a, AE_EXPECT);
                }
                as_lex(a);
                if (a->oc->t != OC_COLON) {
                        err_setstr(SyntaxError,
                                "Uncomputed dictionary key must a single-token expression");
                        as_err(a, AE_EXPECT);
                }
where_3:
                assemble_expr(a);
                count++;
                as_lex(a);
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RBRACE, AE_BRACE);

        add_instr(a, INSTR_DEFDICT, 0, count);
}

/*
 * Try parsing a set.  If it turns out this is a dict, call
 * assemble_dictdef() instead.
 */
static void
assemble_setdef(struct assemble_t *a)
{
        int count;

        as_lex(a);
        if (a->oc->t == OC_RBRACE) {
                /* actually an empty dict, not a set */
                add_instr(a, INSTR_DEFDICT, 0, 0);
                return;
        }

        count = 0;
        as_unlex(a);
        do {
                bool could_be_dict = !count;
                as_lex(a);
                if (a->oc->t == OC_LBRACK) {
                        /*
                         * Lists are unhashable, so this MUST be for a
                         * computed dictionary key.
                         */
                        if (!could_be_dict)
                                as_err(a, AE_EXPECT);
                        assemble_dictdef(a, count, 1);
                        return;
                }

                if (a->oc->t == OC_MUL) {
                        err_setstr(SyntaxError,
                                   "starred arguments not allowed here");
                        as_err(a, AE_EXPECT);
                }

                /*
                 * FIXME: Requiring computed keys to exist only within
                 * square brackets is an artifact of when EvilCandy was
                 * more strictly trying to look like JavaScript.  But
                 * the requirement is purely artificial; it's actually
                 * **easier** to be more versatile and not require it.
                 */
                if (istok_atomic_key(a->oc->t)) {
                        if (as_peek(a, false) == OC_COLON) {
                                if (!could_be_dict)
                                        as_err(a, AE_EXPECT);
                                assemble_dictdef(a, count, 2);
                                return;
                        }
                } else {
                        could_be_dict = false;
                }

                /* comma after last elem */
                if (a->oc->t == OC_RBRACE)
                        break;

                as_unlex(a);
                assemble_expr(a);
                as_lex(a);
                if (a->oc->t == OC_COLON) {
                        if (!could_be_dict)
                                as_err(a, AE_EXPECT);
                        assemble_dictdef(a, count, 3);
                        return;
                }
                count++;
        } while (a->oc->t == OC_COMMA);
        as_err_if(a, a->oc->t != OC_RBRACE, AE_BRACE);
        add_instr(a, INSTR_DEFLIST, 0, count);
        add_instr(a, INSTR_DEFSET, 0, 0);
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
 * check enclosing function before resorting to LOAD_GLOBAL
 */
static int
maybe_closure(struct assemble_t *a, Object *name, token_pos_t pos)
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

        if (as_symbol_seek(a, name, NULL) >= 0 ||
            as_is_ialocal(a, name)) {
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
                array_append(a->fr->af_closures, name);

        /* try this again */
        return as_closure_seek(a, name);
}

/*
 * ainstr_load/assign_symbol
 *
 * @name:  name of symbol, token assumed to be saved from a->oc already.
 * @instr: either INSTR_LOAD_LOCAL, or INSTR_ASSIGN_LOCAL
 * @pos:   Saved token position when saving name; needed to maybe pass to
 *         seek_or_add const
 */
static void
ainstr_load_or_assign(struct assemble_t *a, struct token_t *name,
                      int instr, token_pos_t pos, unsigned int flags)
{
        /*
         * XXX let namei be an arg, -1 means "calculate namei",
         * this removes DRY violation with assemble_declarator_stmt()
         */
        int idx, arg;
        if ((idx = as_symbol_seek(a, name->v, &arg)) >= 0) {
                add_instr(a, instr, arg, idx);
        } else if ((idx = maybe_closure(a, name->v, pos)) >= 0) {
                add_instr(a, instr, IARG_PTR_CP, idx);
        } else {
                int namei = as_seek_rodata_tok(a, name);
                if (instr == INSTR_ASSIGN_LOCAL) {
                        if (as_is_ialocal(a, name->v))
                                instr = INSTR_ASSIGN_NAME;
                        else
                                instr = INSTR_ASSIGN_GLOBAL;
                } else {
                        bug_on(instr != INSTR_LOAD_LOCAL);
                        if (as_is_ialocal(a, name->v))
                                instr = INSTR_LOAD_NAME;
                        else
                                instr = INSTR_LOAD_GLOBAL;
                }
                add_instr(a, instr, 0, namei);
        }
}

static void
ainstr_load_symbol(struct assemble_t *a, struct token_t *name, token_pos_t pos)
{
        ainstr_load_or_assign(a, name, INSTR_LOAD_LOCAL, pos, 0);
}

static void
ainstr_assign_symbol(struct assemble_t *a, struct token_t *name,
                      token_pos_t pos, unsigned int flags)
{
        ainstr_load_or_assign(a, name, INSTR_ASSIGN_LOCAL, pos, flags);
}

static void
assemble_call_func(struct assemble_t *a)
{
        int n_items = 0;
        int kwind = -1;
        bool have_star = false;

        as_errlex(a, OC_LPAR);

        do {
                bool star_here = false;
                if (as_peek(a, false) == OC_RPAR) {
                        as_lex(a);
                        break;
                }
                if (as_peek(a, false) == OC_MUL) {
                        as_lex(a);
                        add_instr(a, INSTR_DEFLIST, 0, n_items);
                        have_star = true;
                        star_here = true;
                }
                as_lex(a);
                if (a->oc->t == OC_IDENTIFIER) {
                        as_lex(a);
                        if (a->oc->t == OC_EQ) {
                                if (star_here) {
                                        err_setstr(SyntaxError,
                                            "cannot use starred expression here");
                                        as_err(a, AE_EXPECT);
                                }
                                kwind = n_items;
                                as_unlex(a);
                                as_unlex(a);
                                break;
                        }
                        as_unlex(a);
                }
                as_unlex(a);
                assemble_expr(a);
                /*
                 * FIXME: This is naive.  Consider something where a
                 * generator would be preferred:
                 *         print(*range(0, 20*BAJILLION));
                 * Do we really want to try to allocate a list that big?
                 */
                if (have_star) {
                        int instr;
                        if (star_here)
                                instr = INSTR_LIST_EXTEND;
                        else
                                instr = INSTR_LIST_APPEND;
                        add_instr(a, instr, 0, 0);
                }
                as_lex(a);
                n_items++;
        } while (a->oc->t == OC_COMMA);

        if (!have_star)
                add_instr(a, INSTR_DEFLIST, 0, n_items);

        if (kwind >= 0) {
                Object *tmp, *kw = arrayvar_new(0);
                int count = 0;
                do {
                        as_lex(a);
                        if (a->oc->t == OC_RPAR)
                                break;
                        if (a->oc->t != OC_IDENTIFIER) {
                                err_setstr(SyntaxError,
                                           "Malformed keyword argument");
                                as_err(a, AE_GEN);
                        }
                        array_append(kw, a->oc->v);
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

                /* transform kw from a list into a blossoming tuple */
                tmp = kw;
                kw = tuplevar_from_stack(array_get_data(tmp),
                                         seqvar_size(tmp), false);
                VAR_DECR_REF(tmp);

                ainstr_load_const_obj(a, kw);
                add_instr(a, INSTR_DEFDICT_K, 0, count);
        } else {
                ainstr_load_null(a);
        }

        as_err_if(a, a->oc->t != OC_RPAR, AE_PAR);
        add_instr(a, INSTR_CALL_FUNC, 0, 0);
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

        case OC_FUNCARG: {
                long long offs;
                as_errlex(a, OC_LPAR);
                as_errlex(a, OC_INTEGER);
                offs = intvar_toll(a->oc->v);
                if (offs > 32767 || offs < 0) {
                        err_setstr(SyntaxError,
                                   "__funcargs__ must take arg in range of 0...32767");
                        as_err(a, AE_OVERFLOW);
                }
                as_errlex(a, OC_RPAR);
                add_instr(a, INSTR_LOAD_ARG, 0, offs);
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
                ainstr_load_null(a);
                break;

        case OC_FUNC:
                assemble_funcdef(a, false);
                break;
        case OC_LBRACK:
                assemble_arraydef(a);
                break;
        case OC_LBRACE:
                assemble_setdef(a);
                break;
        case OC_LAMBDA:
                assemble_funcdef(a, true);
                break;
        case OC_THIS:
                add_instr(a, INSTR_LOAD_LOCAL, IARG_PTR_THIS, 0);
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
                /* recurse, we could have something like !! */
                assemble_expr3_unarypre(a);

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
                        int cond = IARG_COND_SAVEF;
                        if (t->opcode != INSTR_LOGICAL_AND)
                                cond |= IARG_COND_COND;
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
        /*
         * FIXME: Lots of tables means lots of recursion, without the
         * relief of tail-call optimization.
         */
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
                { .tok = OC_IN,     .opcode = IARG_IN },
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
                { .tok = OC_XOR,    .opcode = INSTR_BINARY_XOR },
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
                add_instr(a, INSTR_B_IF, 0, b_if_false);
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
                                add_instr(a, INSTR_COPY, 0, 2);
                                add_instr(a, INSTR_COPY, 0, 2);
                                add_instr(a, INSTR_GETATTR, 0, 0);
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
        add_instr(a, INSTR_LOAD_LOCAL, IARG_PTR_THIS, 0);
        return assemble_primary_elements__(a);
}

struct names_t {
        struct list_t list;
        struct token_t tok;
        token_pos_t pos;
        int namei;
};
#define AS_LIST2NAMES(p) (container_of(p, struct names_t, list))

static void
cleanup_names(struct list_t *names)
{
        struct list_t *p, *q;
        list_foreach_safe(p, q, names) {
                list_remove(p);
                /* list is first entry */
                efree(AS_LIST2NAMES(p));
        }
}

/*
 * common to assemble_unpacker and assemble_declarator stmt
 * This should end with token state pointing at '=' or (if
 * let/global) ';'
 */
static int
gather_names(struct assemble_t *a, struct list_t *names, int *star_idx)
{
        struct names_t *name;
        int needsize = 0; /*< start at 1 to account for firstname */
        int star = -1;

        list_init(names);
        bug_on(a->oc->t != OC_IDENTIFIER && a->oc->t != OC_MUL);
        as_unlex(a);

        do {
                as_lex(a);

                if (a->oc->t == OC_MUL) {
                        if (star >= 0) {
                                err_setstr(SyntaxError,
                                        "starred expression not allowed here");
                                as_err(a, AE_EXPECT);
                        }
                        star = needsize;
                        as_lex(a);
                }

                if (a->oc->t != OC_IDENTIFIER) {
                        err_ae_expect(a, OC_IDENTIFIER);
                        as_err(a, AE_EXPECT);
                }

                name = emalloc(sizeof(*name));
                name->pos = as_savetok(a, &name->tok);
                list_add_front(&name->list, names);

                needsize++;

                as_lex(a);
        } while (a->oc->t == OC_COMMA);

        bug_on(needsize < 1);
        if (needsize == 1 && star >= 0) {
                bug_on(star != 0);
                err_setstr(SyntaxError,
                           "starred target cannot be a single item");
                as_err(a, AE_EXPECT);
        }
        *star_idx = star;
        return needsize;
}

/* return 1 if item left on the stack, 0 if not */
static int
assemble_identifier(struct assemble_t *a, unsigned int flags)
{
        struct list_t names;
        int needsize, star;
        struct names_t *n;

        needsize = gather_names(a, &names, &star);
        if (needsize > 1) {
                /* eg. "a, b = (some, iterable)" */
                struct list_t *p;

                /* XXX What about empty statement like "a, b;"? */
                if (a->oc->t != OC_EQ) {
                        cleanup_names(&names);
                        err_ae_expect(a, OC_EQ);
                        as_err(a, AE_EXPECT);
                }
                assemble_expr(a);
                if (star < 0) {
                        add_instr(a, INSTR_UNPACK, 0, needsize);
                } else {
                        add_instr(a, INSTR_UNPACK_SPECIAL,
                                  0, star << 8 | needsize);
                }
                list_foreach(p, &names) {
                        n = AS_LIST2NAMES(p);
                        ainstr_assign_symbol(a, &n->tok, n->pos, flags);
                }
                cleanup_names(&names);
                return 0;
        }

        n = AS_LIST2NAMES(names.next);
        if (a->oc->t == OC_EQ) {
                /*
                 * x = value;
                 * Don't load, INSTR_ASSIGN_LOCAL knows where from frame
                 * pointer to store 'value'
                 */
                assemble_expr(a);
                ainstr_assign_symbol(a, &n->tok, n->pos, flags);
                cleanup_names(&names);
                return 0;
        } else if (istok_assign(a->oc->t)) {
                /*
                 * x++;
                 * x += value;
                 * ...
                 */
                ainstr_load_symbol(a, &n->tok, n->pos);
                assemble_preassign(a, a->oc->t);
                ainstr_assign_symbol(a, &n->tok, n->pos, flags);
                cleanup_names(&names);
                return 0;
        } else if (istok_indirection(a->oc->t)) {
                /*
                 * x(args);
                 * x[i] [= value];
                 * x.big(damn)[mess].of.stuff...
                 * ...
                 * Here we are not modifying x directly.  We are either
                 * calling a function or modifying one of x's descendants.
                 */
                as_unlex(a);
                ainstr_load_symbol(a, &n->tok, n->pos);
                cleanup_names(&names);
                return assemble_primary_elements__(a);
        } else {
                /*
                 * either an empty statement beginning with an identifier
                 * (eg. "a == b") or a bad statement.  Try evaluation
                 * instead.
                 */
                as_unlex(a);
                as_unlex(a);
                assemble_expr(a);
                return 1;
        }
}

static void
assemble_delete(struct assemble_t *a, unsigned int flags)
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
                ainstr_load_null(a);
                ainstr_assign_symbol(a, &name, pos, flags);
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
assemble_declare(struct assemble_t *a, struct token_t *name,
                 bool global, unsigned int flags)
{
        int namei;
        bug_on(global && name == NULL);
        if (global) {
                namei = as_seek_rodata_tok(a, name);
                add_instr(a, INSTR_NEW_GLOBAL, 0, namei);
        } else if (!!(flags & FE_TOP)) {
                /* Interactive, top-level scope */
                namei = as_seek_rodata_tok(a, name);
                add_instr(a, INSTR_NEW_NAME, 0, namei);
                bug_on(!a->localdict);
                /* We really just care about the key, hence NullVar */
                if (dict_setitem_exclusive(a->localdict, name->v, NullVar)
                    == RES_ERROR) {
                        err_setstr(SyntaxError,
                                   "Redefining variable ('%s')", name->v);
                        as_err(a, AE_GEN);
                }
        } else {
                bug_on(!name);
                namei = fakestack_declare(a, name->v);
                if (!(flags & FE_SKIPNULLASSIGN)) {
                        /*
                         * XXX: This opcode is needed for example when re-
                         * starting a {...} namespace in a for loop, but in most
                         * cases this is a redundant step.
                         */
                        ainstr_load_null(a);
                        add_instr(a, INSTR_ASSIGN_LOCAL, IARG_PTR_AP, namei);
                }
        }
        return namei;
}

static void
assemble_declarator_stmt(struct assemble_t *a, int tok, unsigned int flags)
{
        struct list_t names, *p;
        bool global;
        int needsize, star;
        unsigned int extraflags = 0;

        as_lex(a);
        if (a->oc->t != OC_MUL && a->oc->t != OC_IDENTIFIER) {
                err_ae_expect(a, OC_IDENTIFIER);
                as_err(a, AE_EXPECT);
        }

        needsize = gather_names(a, &names, &star);
        (void)star;

        global = tok == OC_GBL;

        if (a->oc->t == OC_EQ)
                extraflags = FE_SKIPNULLASSIGN;

        list_foreach(p, &names) {
                struct names_t *n = AS_LIST2NAMES(p);
                n->namei = assemble_declare(a, &n->tok,
                                            global, flags | extraflags);
        }

        if (a->oc->t == OC_SEMI) {
                /*
                 * XXX: star >= 0 means someone typed eg. 'let a, *b, c;'
                 * We are allowing it and ignoring the star. Should we?
                 */
                as_unlex(a);
                cleanup_names(&names);
                return;
        }

        if (a->oc->t != OC_EQ) {
                /* for initializers, only '=', not '+=' or such */
                err_ae_expect(a, OC_EQ);
                cleanup_names(&names);
                as_err(a, AE_EXPECT);
        }

        assemble_expr(a);

        if (needsize != 1) {
                if (star < 0) {
                        add_instr(a, INSTR_UNPACK, 0, needsize);
                } else {
                        add_instr(a, INSTR_UNPACK_SPECIAL,
                                0, star << 8 | needsize);
                }

        }

        list_foreach(p, &names) {
                struct names_t *n = AS_LIST2NAMES(p);
                int instr, arg1 = 0;

                /* XXX: DRY violation with ainstr_assign_symbol */
                if (global) {
                        instr = INSTR_ASSIGN_GLOBAL;
                } else if (!!(flags & FE_TOP)) {
                        instr = INSTR_ASSIGN_NAME;
                } else {
                        instr = INSTR_ASSIGN_LOCAL;
                        arg1 = IARG_PTR_AP;
                }
                add_instr(a, instr, arg1, n->namei);
        }
        cleanup_names(&names);
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

        ainstr_pop_block(a, IARG_TRY);

        as_errlex(a, OC_CATCH);
        as_set_label(a, catch);

        /* block of the catch(x) { ... } statement */
        as_errlex(a, OC_LPAR);
        as_errlex(a, OC_IDENTIFIER);
        as_savetok(a, &exctok);
        as_errlex(a, OC_RPAR);
        /*
         * No instructions for pushing this on the stack.
         * The exception handler will do that for us in
         * execute loop.
         */
        fakestack_declare(a, exctok.v);
        assemble_stmt(a, 0, 0);
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
                add_instr(a, INSTR_B_IF, 0, jmpelse);
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

        add_instr(a, INSTR_B_IF, 0, breakto);
        assemble_stmt(a, FE_CONTINUE, start);
        add_instr(a, INSTR_B, 0, start);

        ainstr_pop_block(a, IARG_LOOP);

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

        ainstr_pop_block(a, IARG_LOOP);

        as_set_label(a, breakto);
}

static void
assemble_foreach2(struct assemble_t *a, struct list_t *names,
                  int star, int needsize)
{
        int iternext = as_next_label(a);
        if (needsize == 1) {
                /* needle is the 'a' of 'for (a in b)' */
                struct names_t *n = AS_LIST2NAMES(names->next);
                bug_on(!n->tok.v);
                n->namei = fakestack_declare(a, n->tok.v);
                add_instr(a, INSTR_ASSIGN_LOCAL, IARG_PTR_AP, n->namei);
        } else {
                /*
                 * 'needle is itself a sequence, the unnamed container
                 * of 'a' and 'b' and child of 'c' in 'for (a, b in c)'.
                 */
                struct list_t *p;
                int i = 0;

                list_foreach(p, names) {
                        struct names_t *n = AS_LIST2NAMES(p);
                        bug_on(!n->tok.v);
                        n->namei = fakestack_declare(a, n->tok.v);
                        i++;
                }
                bug_on(i != needsize);
                if (star < 0) {
                        add_instr(a, INSTR_UNPACK, 0, needsize);
                } else {
                        add_instr(a, INSTR_UNPACK_SPECIAL,
                                0, star << 8 | needsize);
                }
                list_foreach(p, names) {
                        struct names_t *n = AS_LIST2NAMES(p);
                        add_instr(a, INSTR_ASSIGN_LOCAL, IARG_PTR_AP, n->namei);
                }
        }
        assemble_stmt(a, FE_CONTINUE, iternext);
        as_set_label(a, iternext);
}

static void
assemble_foreach1(struct assemble_t *a, int breakto)
{
        struct list_t names;
        int star;
        int forelse = as_next_label(a);
        int iter = as_next_label(a);
        int needsize;
        bool have_par = false;

        as_lex(a);
        if (a->oc->t == OC_LPAR) {
                have_par = true;
                as_lex(a);
        }

        /* save names of the 'needles' in 'for (needles in haystack)' */
        if (a->oc->t != OC_MUL && a->oc->t != OC_IDENTIFIER) {
                err_ae_expect(a, OC_IDENTIFIER);
                as_err(a, AE_EXPECT);
        }
        needsize = gather_names(a, &names, &star);

        if (a->oc->t != OC_IN) {
                err_ae_expect(a, OC_IN);
                cleanup_names(&names);
                as_err(a, AE_EXPECT);
        }

        /* push 'haystack onto the stack */
        assemble_expr(a);

        if (have_par) {
                as_lex(a);
                if (a->oc->t != OC_RPAR) {
                        err_ae_expect(a, OC_RPAR);
                        cleanup_names(&names);
                        as_err(a, AE_EXPECT);
                }
        }

        /* maybe replace 'haystack' with its keys */
        add_instr(a, INSTR_FOREACH_SETUP, 0, 0);
        as_set_label(a, iter);
        add_instr(a, INSTR_FOREACH_ITER, 0, forelse);

        ainstr_push_block(a, IARG_BLOCK, 0);
        assemble_foreach2(a, &names, star, needsize);
        ainstr_pop_block(a, IARG_BLOCK);

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
        cleanup_names(&names);
}

static void
assemble_foreach(struct assemble_t *a)
{
        int breakto = as_next_label(a);
        ainstr_push_block(a, IARG_LOOP, breakto);

        assemble_foreach1(a, breakto);

        ainstr_pop_block(a, IARG_LOOP);
        as_set_label(a, breakto);
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
        ainstr_pop_block(a, arg1);
}

/* parse the stmt of 'stmt' + ';' */
static void
assemble_stmt_simple(struct assemble_t *a, unsigned int flags,
                     int continueto)
{
        int need_pop = 0;
        int pop_arg = !!(flags & FE_TOP) ? IARG_POP_PRINT : IARG_POP_NORMAL;

        as_lex(a);
        /* cases return early if semicolon not expected at the end */
        switch (a->oc->t) {
        case OC_DELETE:
                assemble_delete(a, flags);
                return;
        case OC_EOF:
                return;
        case OC_MUL:
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
                as_err(a, AE_PAR);
                as_unlex(a);
                break;
        case OC_LET:
        case OC_GBL:
                assemble_declarator_stmt(a, a->oc->t, flags);
                break;
        case OC_RETURN:
                if (!!(flags & FE_TOP)) {
                        err_setstr(SyntaxError,
                                "Cannot return in interactive mode while outside of a function");
                        as_err(a, AE_EXPECT);
                }
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
                assemble_foreach(a);
                return;
        case OC_LBRACE:
                assemble_block_stmt(a, flags & ~FE_TOP, continueto);
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

        as_errlex(a, OC_SEMI);
}

RECURSION_DECLARE(as_recursion);

/*
 * assemble_stmt - Parser for the top-level statement
 * @flags: FE_xxxx flags
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
new_assembler(const char *source_file_name, FILE *fp, Object *localdict)
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
        if (localdict) {
                a->localdict = dictvar_new();
                dict_copyto(a->localdict, localdict);
        } else {
                a->localdict = NULL;
        }
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
                VAR_DECR_REF(fr->af_locals);
                VAR_DECR_REF(fr->af_args);
                VAR_DECR_REF(fr->af_closures);
                VAR_DECR_REF(fr->af_rodata);
                VAR_DECR_REF(fr->af_names);

                /*
                 * These are safe to free, because if we aren't unwinding
                 * due to failure, buffer_trim reset these already.
                 */
                buffer_free(&fr->af_localmap);
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
        if (a->localdict)
                VAR_DECR_REF(a->localdict);
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
        if (a->oc) {
                lineno = a->oc->start_line;
                col = a->oc->start_col;
        } else {
                lineno = 1;
                col = 0;
        }
        fprintf(stderr, "in file '%s' near line '%d'\n",
                a->file_name, lineno);
        line = token_get_this_line(a->prog);
        if (line) {
                fprintf(stderr, "Suspected error location:\n");
                fprintf(stderr, "\t%s\n\t", line);
                while (col-- > 0)
                        fputc(' ', stderr);
                fprintf(stderr, "^\n");
        }
        token_flush_tty(a->prog);
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

/* extern linkage because assemble_post.c needs it */
int
assemble_seek_rodata(struct assemble_t *a, Object *v)
{
        Object *rodata = a->fr->af_rodata;
        ssize_t i = array_indexof_strict(rodata, v);
        if (i < 0) {
                i = seqvar_size(rodata);
                array_append(rodata, v);
        }
        return i;
}

void
assemble_frame_push(struct assemble_t *a, long long funcno)
{
        struct as_frame_t *fr;

        fr = emalloc(sizeof(*fr));
        memset(fr, 0, sizeof(*fr));

        fr->af_locals   = arrayvar_new(0);
        fr->af_args     = arrayvar_new(0);
        fr->af_closures = arrayvar_new(0);
        fr->af_rodata   = arrayvar_new(0);
        fr->af_names    = arrayvar_new(0);
        /* memset did this, but just in case buffer.c internals change... */
        buffer_init(&fr->af_localmap);
        buffer_init(&fr->af_labels);
        buffer_init(&fr->af_instr);

        fr->funcno = funcno;
        fr->line = a->oc ? a->oc->start_line : 1;

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
 * @localdict:  NULL if in script mode.  If in interactive mode, the
 *              dictionary used by the VM for top-level local variables.
 * @status:     stores RES_OK if all is well or RES_ERROR if an assembler
 *              error occurred.
 *
 * Return: Either...
 *      a) A struct xptrvar_t which is ready for passing to the VM.
 *      b) NULL if the input is already at EOF or if there was an error
 *         (check status).
 */
Object *
assemble(const char *filename, FILE *fp, Object *localdict, int *status)
{
        int localstatus;
        struct xptrvar_t *ret;
        struct assemble_t *a;
        bool toeof;
        CLOCK_DECLARE();

        a = new_assembler(filename, fp, localdict);
        if (!a) {
                /* no program, just eof */
                *status = RES_OK;
                return NULL;
        }

        toeof = !localdict;

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

        free_assembler(a);
        return (Object *)ret;
}

