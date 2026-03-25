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
 * @FE_CHECKTUPLE: Used by assemble_expr() to determine whether
 *          it should check if an expression is followed by a
 *          comma and, if so, build a tuple.  This supports lazy
 *          tuple expressions that don't use parentheses.
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
        FE_CHECKTUPLE   = 0x10,

        /*
         * bits 4-5, three mutually-exclusive arguments to
         * assemble_primary_elements.
         */
        FEE_EVAL        = 0x000, /* use with FEE_MASK */
        FEE_ASGN        = 0x100,
        FEE_DEL         = 0x200,
        FEE_MASK        = 0x300,
};

/* TODO: This should be in evcenums, it could be used in many places */
enum errhandler_t {
        ERRH_RETURN = 1,
        ERRH_EXCEPTION,
};

#define as_badeof(a) do { \
        DBUG("Bad EOF, trapped in assembly.c line %d", __LINE__); \
        err_setstr(SyntaxError, "Unexpected termination"); \
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

static int assemble_expr(struct assemble_t *a, unsigned int flags);
static int assemble_stmt(struct assemble_t *a, unsigned int flags,
                         int continueto);
static int assemble_expr5_atomic(struct assemble_t *a);
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
as_savetok(struct assemble_t *a, struct token_t **tok)
{
        if (tok)
                *tok = a->oc;
        return token_get_pos(a->prog);
}

static inline struct token_t *
as_pos2tok(struct assemble_t *a, token_pos_t pos)
{
        /*
         * minus one because @pos is a return value of as_savetok().
         * The token returned from token_get_pos() will have been
         * incremented.
         */
        return get_tok_at(a->prog, pos - 1);
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
        /* if err, get_tok should have set exception */
        return get_tok(a->prog, &a->oc);
}

static void
err_ae_expect(struct assemble_t *a, int exp)
{
        err_setstr(SyntaxError, "expected '%s' but got '%s'",
                   token_name(exp), token_name(a->oc->t));
}

static void
err_ae_brack(void)
{
        err_setstr(SyntaxError, "unbalanced bracket");
}

static void
err_ae_par(void)
{
        err_setstr(SyntaxError, "unbalanced parenthesis");
}

static void
err_ae_brace(void)
{
        err_setstr(SyntaxError, "unbalanced brace");
}

static int
as_errlex(struct assemble_t *a, int exp)
{
        if (as_lex(a) < 0)
                return -1;
        if (a->oc->t != exp) {
                err_ae_expect(a, exp);
                return -1;
        }
        return 0;
}

static inline token_pos_t
as_swap_pos(struct assemble_t *a, token_pos_t pos)
{
        int status;
        token_pos_t ret = token_swap_pos(a->prog, pos);
        as_unlex(a);
        status = as_lex(a);
        bug_on(status < 0);
        (void)status;
        return ret;
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

/*
 * DOC: localmap_xxx() Helper Functions and Scope Visibility Stuff:
 *
 * Scope at the sub-function level--that is, using braces or program-flow
 * loops to limit a local variable's scope--is sort of an illusion in
 * EvilCandy.  The assembler manages it by simply preventing compilation
 * of instructions that would access out-of-scope variables; however, the
 * variables will still remain on the stack during the entirety of a
 * function's execution, whether they remain in scope or not, and not get
 * cleared until the function completes.
 *
 * .af_locals is the assembler's stack of variable names, growing and
 * shrinking while assembling a function, so that we know whether a name
 * exists in scope or not.  It has a parallel array, .af_localmap, which
 * translates an index in .af_locals to the actual stack index where the
 * variable will exist on the stack during runtime.  .af_nlocals keeps
 * track of how the runtime stack will grow. .af_names is its parallel,
 * used for debug purposes; it is an array of the names as they will
 * appear on the stack; it could contain the same name twice, then, in
 * the case of code like this:
 *
 *      {
 *              let x;
 *      }
 *      {
 *              let x;
 *      }
 *
 * Here, .af_names would then contain at least two entries matching 'x',
 * while .af_locals will still always have either one or zero entries
 * matching 'x'.
 *
 * The algorithm is this:
 *
 * When declaring variable:
 *      o Ensure its name is not in .af_locals, then append it.
 *      o Append an index number in .af_localmap equal to .af_nlocals
 *      o Append its name to .af_names for disassembly, etc.
 *      o Increment .af_nlocals by one
 *
 * When leaving a block statement or loop:
 *      o Shrink .af_locals to the appropriate spot
 *      o Shrink .af_localmap by the same amount
 *      o keep .af_nlocals and .af_names as they are
 *
 * This way, any newly declared variable will be assigned a new index
 * without clobbering an old variable.
 *
 * XXX: Non-clobbering is easy, but is it necessary?  The only bad effect
 * of clobbering is we'd have to give up the debug-usefulness of
 * .af_names, not that it's really that useful. On the other hand,
 * clobbering improves the chance that some variables will be freed
 * earlier when they are no longer needed, and it reduces stack usage.
 */

static size_t
localmap_size(struct assemble_t *a)
{
        return buffer_size(&a->fr->af_localmap) / sizeof(int);
}

/*
 * Helper to as_symbol_seek.  Convert a current-namespace index into the
 * actual stack position of a named local variable.
 * @idx is an index in a->fr->af_locals
 */
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
        /* XXX: This is an error, not a bug */
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
static int
as_set_label(struct assemble_t *a, int jmp)
{
        unsigned long val = as_frame_ninstr(a->fr);
        if (val > 32767) {
                err_setstr(RangeError,
                           "Cannot compile: instruction set too large for jump labels");
                return -1;
        }
        assemble_frame_set_label(a->fr, jmp, val);
        return 0;
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

static int
ainstr_push_block(struct assemble_t *a, int arg1, int arg2)
{
        struct as_frame_t *fr = a->fr;
        if (fr->nest >= FRAME_NEST_MAX) {
                err_setstr(SyntaxError, "frame/scope nest overflow");
                return -1;
        }

        fr->scope[fr->nest++] = fr->fp;

        fr->fp = seqvar_size(fr->af_locals);
        if (arg1 != IARG_BLOCK)
                add_instr(a, INSTR_PUSH_BLOCK, arg1, arg2);
        return 0;
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
 *
 * return idx >= 0, or -1 if error;
 */
static int
fakestack_declare(struct assemble_t *a, Object *name)
{
        int idx;
        if (name && as_symbol_seek(a, name, NULL) >= 0) {
                err_setstr(SyntaxError, "Redefining variable ('%s')", name);
                return -1;
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
 * XXX: gather|cleanup_names's and gather|cleanup_args's routines
 * are sooo close to only having to use the same structs, we could
 * make this cleaner.
 */
struct names_t {
        struct list_t list;
        token_pos_t pos;
        int namei;
};

#define AS_LIST2NAMES(p)        (container_of(p, struct names_t, list))
#define AS_NAME2TOK(a_, n_)     as_pos2tok(a_, (n_)->pos)

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
gather_names(struct assemble_t *a, struct list_t *names,
             int *star_idx, enum errhandler_t errhandler)
{
        struct names_t *name;
        int needsize = 0;
        int star = -1;
        const char *emsg;

        list_init(names);
        bug_on(a->oc->t != OC_IDENTIFIER && a->oc->t != OC_MUL);
        as_unlex(a);

        do {
                if (as_lex(a) < 0)
                        goto err;

                if (a->oc->t == OC_MUL) {
                        if (star >= 0)
                                goto epermit;
                        star = needsize;
                        if (as_lex(a) < 0)
                                goto err;
                }

                if (a->oc->t != OC_IDENTIFIER)
                        goto eident;

                name = emalloc(sizeof(*name));
                name->pos = as_savetok(a, NULL);
                list_add_front(&name->list, names);

                needsize++;

                if (as_lex(a) < 0)
                        goto err;
        } while (a->oc->t == OC_COMMA);

        bug_on(needsize < 1);
        if (needsize == 1 && star >= 0) {
                bug_on(star != 0);
                goto esingle;
        }
        *star_idx = star;
        return needsize;

eident:
        emsg = "expected: identifier";
        goto err;
epermit:
        emsg = "starred expression not allowed here";
        goto err;
esingle:
        emsg = "starred target cannot be a single item";
err:
        cleanup_names(names);
        switch (errhandler) {
        case ERRH_EXCEPTION:
                err_setstr(SyntaxError, "%s", emsg);
                break;
        case ERRH_RETURN:
                break;
        default:
                bug();
        }
        return -1;
}

struct arglist_t {
        struct list_t names;
        int star;
        int starstar;
        int n;
};

static void
cleanup_arglist(struct arglist_t *alist)
{
        struct list_t *p, *tmp;
        list_foreach_safe(p, tmp, &alist->names) {
                list_remove(p);
                efree(AS_LIST2NAMES(p));
        }
}

/*
 * parse args (for a function *definition*, not for a function *call*)
 * a->oc->t points at the first token after the opening parenthesis.
 */
static enum result_t
gather_arglist(struct assemble_t *a,
               struct arglist_t *alist,
               enum errhandler_t errhandler)
{
        const char *emsg = NULL;

        list_init(&alist->names);
        alist->star = -1;
        alist->starstar = -1;
        alist->n = 0;
        do {
                struct names_t *name;
                if (as_lex(a) < 0)
                        goto err_parser;

                if (a->oc->t == OC_RPAR)
                        break;
                if (alist->starstar >= 0)
                        goto err_posarg_after_kw;
                if (a->oc->t == OC_MUL) {
                        if (alist->star >= 0)
                                goto err_multi_variadic;
                        alist->star = alist->n;
                        if (as_lex(a) < 0)
                                goto err_parser;
                } else if (a->oc->t == OC_POW) {
                        alist->starstar = alist->n;
                        if (as_lex(a) < 0)
                                goto err_parser;
                } else {
                        /* normal */
                        if (alist->star >= 0)
                                goto err_posarg_after_variadic;
                }
                if (a->oc->t != OC_IDENTIFIER)
                        goto err_not_identifier;

                name = emalloc(sizeof(*name));
                name->pos = as_savetok(a, NULL);
                list_add_tail(&name->list, &alist->names);
                alist->n++;
                if (as_lex(a) < 0)
                        goto err_parser;
        } while (a->oc->t == OC_COMMA);
        if (a->oc->t != OC_RPAR)
                goto err_missing_rpar;
        return RES_OK;

err_parser:
        errhandler = ERRH_RETURN;
        goto err;
err_missing_rpar:
        emsg = "Expected ')' or ','";
        goto err;
err_not_identifier:
        emsg = "Function argument is not an identifier";
        goto err;
err_posarg_after_variadic:
        emsg = "You may not declare positional argument after variadic argument";
        goto err;
err_multi_variadic:
        emsg = "You may only declare one variadic argument";
        goto err;
err_posarg_after_kw:
        emsg = "You may not declare arguments after a keyword argument";
        goto err;
err:
        cleanup_arglist(alist);
        switch (errhandler) {
        case ERRH_EXCEPTION:
                err_setstr(SyntaxError, "%s", emsg);
                break;
        case ERRH_RETURN:
                break;
        default:
                bug();
        }
        /* TODO: perform cleanup */
        return RES_ERROR;
}

/*
 * Parse either "i" of "x[i]" or "i:j:k" of "x[i:j:k]".
 * Return token state such that next as_lex() ought to point at
 * closing right bracket "]".
 */
static int
assemble_slice(struct assemble_t *a)
{
        int endmarker = OC_RBRACK;
        int i;

        for (i = 0; i < 3; i++) {
                if (as_lex(a) < 0)
                        return -1;
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
                                        return -1;
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
                        if (assemble_expr(a, 0) < 0)
                                return -1;
                        if (as_lex(a) < 0)
                                return -1;
                }

                if (a->oc->t == endmarker) {
                        as_unlex(a);
                        if (i == 0) /* not a slice, just a subscript */
                                return 0;
                } else if (i != 2 && a->oc->t != OC_COLON) {
                        err_setstr(SyntaxError,
                                   "Expected: either ':' or '%s'",
                                   token_name(endmarker));
                        return -1;
                }
        }
        add_instr(a, INSTR_DEFTUPLE, 0, 3);
        return 0;
}

/*
 * Helper to assemble_arrow_lambda() and assmeble_funcdef().
 * @alist is cleaned up in this function, don't use afterwards.
 *
 * return 0 if success, -1 if error
 */
static int
assemble_function_body(struct assemble_t *a,
                       bool lambda, struct arglist_t *alist)
{
        int funcno = as_next_funcno(a);

        ainstr_load_const_obj(a, idvar_new(funcno));
        add_instr(a, INSTR_DEFFUNC, 0, 0);
        add_instr(a, INSTR_FUNC_SETATTR, IARG_FUNC_MINARGS, alist->n);
        add_instr(a, INSTR_FUNC_SETATTR, IARG_FUNC_MAXARGS, alist->n);
        if (alist->star >= 0) {
                add_instr(a, INSTR_FUNC_SETATTR,
                          IARG_FUNC_OPTIND, alist->star);
        }
        if (alist->starstar >= 0) {
                add_instr(a, INSTR_FUNC_SETATTR,
                          IARG_FUNC_KWIND, alist->starstar);
        }

        assemble_frame_push(a, funcno);
        {
                struct list_t *p;
                bool have_brace;
                list_foreach(p, &alist->names) {
                        struct names_t *n = AS_LIST2NAMES(p);
                        struct token_t *tok = AS_NAME2TOK(a, n);
                        array_append(a->fr->af_args, tok->v);
                }

                if (as_lex(a) < 0)
                        return -1;
                have_brace = a->oc->t == OC_LBRACE;
                as_unlex(a);

                if (lambda && !have_brace) {
                        if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                                return -1;
                        add_instr(a, INSTR_RETURN_VALUE, 0, 0);
                        add_instr(a, INSTR_END, 0, 0);
                } else {
                        if (assemble_stmt(a, 0, 0) < 0)
                                return -1;
                        /*
                         * This is often unreachable to the VM, but in
                         * case statement reached end without hitting
                         * "return", we need to preven VM from over-
                         * running instruction set.
                         */
                        ainstr_return_null(a);
                }
        }
        assemble_frame_pop(a);
        return 0;
}

static int
assemble_funcdef(struct assemble_t *a)
{
        struct arglist_t alist;
        int ret;

        if (as_errlex(a, OC_LPAR) < 0)
                return -1;
        if (gather_arglist(a, &alist, ERRH_EXCEPTION) < 0)
                return -1;

        ret = assemble_function_body(a, false, &alist);
        cleanup_arglist(&alist);
        return ret;
}

static int
assemble_arraydef(struct assemble_t *a)
{
        int n_items = 0;
        bool have_star = false;

        if (as_lex(a) < 0)
                return -1;
        if (a->oc->t == OC_RBRACK) /* empty array */
                goto done;
        as_unlex(a);

        do {
                bool star_here = false;
                if (as_lex(a) < 0)
                        return -1;
                /*
                 * sloppy ",]", but allow it anyway,
                 * since everyone else does.
                 */
                if (a->oc->t == OC_RBRACK)
                        break;
                if (a->oc->t == OC_MUL) {
                        add_instr(a, INSTR_DEFLIST, 0, n_items);
                        have_star = true;
                        star_here = true;
                } else {
                        as_unlex(a);
                }
                if (assemble_expr(a, 0) < 0)
                        return -1;
                if (have_star) {
                        int instr;
                        if (star_here)
                                instr = INSTR_LIST_EXTEND;
                        else
                                instr = INSTR_LIST_APPEND;
                        add_instr(a, instr, 0, 0);
                }
                if (as_lex(a) < 0)
                        return -1;
                n_items++;
        } while (a->oc->t == OC_COMMA);
        if (a->oc->t != OC_RBRACK) {
                err_ae_brack();
                return -1;
        }

done:
        if (!have_star)
                add_instr(a, INSTR_DEFLIST, 0, n_items);
        return 0;
}

/* return 1 (true) if arrow lambda, 0 if not, -1 if error */
static int
assemble_arrow_lambda(struct assemble_t *a)
{
        int ret;
        struct arglist_t alist;
        token_pos_t pos = as_savetok(a, NULL);

        if (gather_arglist(a, &alist, ERRH_RETURN) == RES_ERROR) {
                if (err_occurred())
                        goto err_clean;
                goto not_arrow;
        }

        if (as_lex(a) < 0)
                goto err_clean;
        if (a->oc->t != OC_RARROW)
                goto not_arrow;

        /* must be lambda, so error hereafter is for-real error */
        ret = assemble_function_body(a, true, &alist);
        cleanup_arglist(&alist);
        return ret < 0 ? ret : 1;

not_arrow:
        cleanup_arglist(&alist);
        (void)as_swap_pos(a, pos);
        return 0;

err_clean:
        /* parser error, don't suppress this one */
        cleanup_arglist(&alist);
        return -1;
}

/*
 * or, assemble_presumably_a_tuple_tupldef()
 * Opening parenthesis could mean so many different things...
 * encapsulate a single expression, be the start of a tuple,
 * be the start of a lambda expression's argument list, and
 * if we add comprehensions, that too.
 */
static int
assemble_tupledef(struct assemble_t *a)
{
        int res, n_items = 0;
        bool has_comma;
        bool have_star = false;

        /* If "(..." is actually "(args) => expr", do that instead */
        if ((res = assemble_arrow_lambda(a)) != 0)
                return res < 0 ? res : 0;

        if (as_lex(a) < 0)
                return -1;
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
                if (as_lex(a) < 0)
                        return -1;
                if (a->oc->t == OC_RPAR) {
                        has_comma = true;
                        break;
                }
                if (a->oc->t == OC_MUL) {
                        add_instr(a, INSTR_DEFLIST, 0, n_items);
                        have_star = true;
                        star_here = true;
                } else {
                        as_unlex(a);
                }
                if (assemble_expr(a, 0) < 0)
                        return -1;
                if (have_star) {
                        int instr;
                        if (star_here)
                                instr = INSTR_LIST_EXTEND;
                        else
                                instr = INSTR_LIST_APPEND;
                        add_instr(a, instr, 0, 0);
                }
                if (as_lex(a) < 0)
                        return -1;
                n_items++;
        } while (a->oc->t == OC_COMMA);
        /* TODO: if comprehension, this would be 'for' */
        if (a->oc->t != OC_RPAR) {
                err_ae_par();
                return -1;
        }

        /* "(x,)" is a tuple.  "(x)" is whatever x is */
        if (!has_comma && n_items == 1) {
                if (have_star) {
                        err_setstr(SyntaxError,
                                "cannot use starred expression here");
                        return -1;
                }
                return 0;
        }
done:
        if (have_star) {
                add_instr(a, INSTR_CAST_TUPLE, 0, 0);
        } else {
                add_instr(a, INSTR_DEFTUPLE, 0, n_items);
        }
        return 0;
}

/*
 * called from assemble_setdef() as soon as it finds out it's
 * parsing a dict instead of a set
 */
static int
assemble_dictdef(struct assemble_t *a, int count, int where)
{
        /* XXX: deprecated, there used to be more 'where' choices */
        bug_on(where != 3);
        goto where_3;

        do {
                if (as_lex(a) < 0)
                        return -1;

                if (a->oc->t == OC_RBRACE) {
                        /* comma after last elem */
                        break;
                }
                as_unlex(a);
                if (assemble_expr(a, 0) < 0)
                        return -1;
                if (as_lex(a) < 0)
                        return -1;
                if (a->oc->t != OC_COLON) {
                        err_setstr(SyntaxError,
                                "Uncomputed dictionary key must a single-token expression");
                        return -1;
                }
where_3:
                if (assemble_expr(a, 0) < 0)
                        return -1;
                if (as_lex(a) < 0)
                        return -1;
                count++;
        } while (a->oc->t == OC_COMMA);
        if (a->oc->t != OC_RBRACE) {
                err_ae_brace();
                return -1;
        }

        add_instr(a, INSTR_DEFDICT, 0, count);
        return 0;
}

/*
 * Try parsing a set.  If it turns out this is a dict, call
 * assemble_dictdef() instead.
 */
static int
assemble_setdef(struct assemble_t *a)
{
        int count;

        if (as_lex(a) < 0)
                return -1;
        if (a->oc->t == OC_RBRACE) {
                /* actually an empty dict, not a set */
                add_instr(a, INSTR_DEFDICT, 0, 0);
                return 0;
        }

        count = 0;
        as_unlex(a);
        do {
                if (as_lex(a) < 0)
                        return -1;
                if (a->oc->t == OC_MUL) {
                        /* TODO: make necessary changes to allow this */
                        err_setstr(SyntaxError,
                                   "starred arguments not allowed here");
                        return -1;
                }

                /* comma after last elem */
                if (a->oc->t == OC_RBRACE)
                        break;

                as_unlex(a);
                if (assemble_expr(a, 0) < 0)
                        return -1;
                if (as_lex(a) < 0)
                        return -1;
                if (a->oc->t == OC_COLON) {
                        if (count > 0) {
                                err_ae_expect(a, OC_COMMA);
                                return -1;
                        }
                        return assemble_dictdef(a, count, 3);
                }
                count++;
        } while (a->oc->t == OC_COMMA);
        if (a->oc->t != OC_RBRACE) {
                err_ae_brace();
                return -1;
        }
        add_instr(a, INSTR_DEFLIST, 0, count);
        add_instr(a, INSTR_DEFSET, 0, 0);
        return 0;
}

static int
assemble_fstring(struct assemble_t *a)
{
        int count = 0;
        do {
                if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                        return -1;
                count++;
                if (as_lex(a) < 0)
                        return -1;
        } while (a->oc->t == OC_FSTRING_CONTINUE);

        if (a->oc->t != OC_FSTRING_END) {
                /*
                 * XXX bug? tokenizer should have trapped
                 * unterminated quote
                 */
                err_setstr(SyntaxError, "Expected: end of f-string");
                return -1;
        }

        add_instr(a, INSTR_DEFTUPLE, 0, count);
        ainstr_load_const(a, a->oc);
        add_instr(a, INSTR_FORMAT, 0, 0);
        return 0;
}

/*
 * helper to ainstr_load_symbol, @name is not in local namespace,
 * check enclosing function before resorting to LOAD_GLOBAL
 */
static enum result_t
maybe_closure(struct assemble_t *a, Object *name, token_pos_t pos, int *idx)
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
        if (!this_frame) {
                *idx = -1;
                return RES_OK;
        }

        if (as_symbol_seek(a, name, NULL) >= 0 ||
            as_is_ialocal(a, name)) {
                int status;
                /*
                 * 'atomic' instead of assemble_expr(), because if we see
                 * 'x.y', the closure should be x, not its descendant y.
                 * This way user-defined classes can use mutable closures
                 * like dictionaries to pass the same private data to
                 * multiple methods of the same instantiation.
                 */
                pos = as_swap_pos(a, pos);
                status = assemble_expr5_atomic(a);
                as_swap_pos(a, pos);

                if (status < 0)
                        return RES_ERROR;

                /* back to identifier */
                add_instr(a, INSTR_ADD_CLOSURE, 0, 0);
                success = true;
        }

        as_frame_restore(a, this_frame);

        if (success)
                array_append(a->fr->af_closures, name);

        /* try this again */
        *idx = as_closure_seek(a, name);
        return RES_OK;
}

/*
 * ainstr_load/assign_symbol
 *
 * @name:  name of symbol, token assumed to be saved from a->oc already.
 * @instr: either INSTR_LOAD_LOCAL, or INSTR_ASSIGN_LOCAL.  It will be
 *         changed to NAME or GLOBAL if necessary.
 * @pos:   Saved token position when saving name; needed to maybe pass to
 *         seek_or_add const
 */
static int
ainstr_load_or_assign(struct assemble_t *a, int instr, token_pos_t pos)
{
        int idx, arg, namei;
        struct token_t *name = as_pos2tok(a, pos);

        /* first check if in local namespace */
        if ((idx = as_symbol_seek(a, name->v, &arg)) >= 0) {
                add_instr(a, instr, arg, idx);
                return 0;
        }

        /* check if in ancestor scope, maybe add closure */
        if (maybe_closure(a, name->v, pos, &idx) == RES_ERROR)
                return -1;
        if (idx >= 0) {
                add_instr(a, instr, IARG_PTR_CP, idx);
                return 0;
        }

        /* Must be global or interactive-mode top-level local */
        namei = as_seek_rodata_tok(a, name);
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
        return 0;
}

static int
ainstr_load_symbol(struct assemble_t *a, token_pos_t pos)
{
        return ainstr_load_or_assign(a, INSTR_LOAD_LOCAL, pos);
}

static int
ainstr_assign_symbol(struct assemble_t *a, token_pos_t pos)
{
        return ainstr_load_or_assign(a, INSTR_ASSIGN_LOCAL, pos);
}

static int
assemble_call_func(struct assemble_t *a)
{
        int n_items = 0;
        int kwind = -1;
        bool have_star = false;

        if (as_errlex(a, OC_LPAR) < 0)
                return -1;

        do {
                bool star_here = false;
                if (as_lex(a) < 0)
                        return -1;
                if (a->oc->t == OC_RPAR)
                        break;
                if (a->oc->t == OC_MUL) {
                        add_instr(a, INSTR_DEFLIST, 0, n_items);
                        have_star = true;
                        star_here = true;
                        if (as_lex(a) < 0)
                                return -1;
                }

                if (a->oc->t == OC_IDENTIFIER) {
                        if (as_lex(a) < 0)
                                return -1;
                        if (a->oc->t == OC_EQ) {
                                if (star_here) {
                                        err_setstr(SyntaxError,
                                            "cannot use starred expression here");
                                        return -1;
                                }
                                kwind = n_items;
                                as_unlex(a);
                                as_unlex(a);
                                break;
                        }
                        as_unlex(a);
                }
                as_unlex(a);
                if (assemble_expr(a, 0) < 0)
                        return -1;
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
                if (as_lex(a) < 0)
                        return -1;
                n_items++;
        } while (a->oc->t == OC_COMMA);

        if (!have_star)
                add_instr(a, INSTR_DEFLIST, 0, n_items);

        if (kwind >= 0) {
                Object *tmp, *kw = arrayvar_new(0);
                int count = 0;
                do {
                        if (as_lex(a) < 0)
                                return -1;
                        if (a->oc->t == OC_RPAR)
                                break;
                        if (a->oc->t != OC_IDENTIFIER) {
                                err_setstr(SyntaxError,
                                           "Malformed keyword argument");
                                return -1;
                        }
                        array_append(kw, a->oc->v);
                        if (as_lex(a) < 0)
                                return -1;
                        if (a->oc->t != OC_EQ) {
                                err_setstr(SyntaxError,
                                           "Normal arguments may not follow keyword arguments");
                                return -1;
                        }
                        if (assemble_expr(a, 0) < 0)
                                return -1;
                        count++;
                        if (as_lex(a) < 0)
                                return -1;
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

        if (a->oc->t != OC_RPAR) {
                err_ae_par();
                return -1;
        }
        add_instr(a, INSTR_CALL_FUNC, 0, 0);
        return 0;
}

static int
assemble_expr5_atomic(struct assemble_t *a)
{
        switch (a->oc->t) {
        case OC_IDENTIFIER: {
                token_pos_t pos = as_savetok(a, NULL);
                if (ainstr_load_symbol(a, pos) < 0)
                        return -1;
                break;
        }

        case OC_FUNCARG: {
                long long offs;
                if (as_errlex(a, OC_LPAR) < 0)
                        return -1;
                if (as_errlex(a, OC_INTEGER) < 0)
                        return -1;
                offs = intvar_toll(a->oc->v);
                if (offs > 32767 || offs < 0) {
                        err_setstr(SyntaxError,
                                   "__funcargs__ must take arg in range of 0...32767");
                        return -1;
                }
                if (as_lex(a) < 0)
                        return -1;
                if (a->oc->t != OC_RPAR) {
                        err_setstr(SyntaxError, "Unbalanced parenthesis");
                        return -1;
                }
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
                if (assemble_fstring(a) < 0)
                        return -1;
                break;
        case OC_LPAR:
                if (assemble_tupledef(a) < 0)
                        return -1;
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
                if (assemble_funcdef(a) < 0)
                        return -1;
                break;
        case OC_LBRACK:
                if (assemble_arraydef(a) < 0)
                        return -1;
                break;
        case OC_LBRACE:
                if (assemble_setdef(a) < 0)
                        return -1;
                break;
        case OC_THIS:
                add_instr(a, INSTR_LOAD_LOCAL, IARG_PTR_THIS, 0);
                break;
        default:
                err_setstr(SyntaxError, "Unexpected token %s",
                           token_name(a->oc->t));
                return -1;
        }

        if (as_lex(a) < 0)
                return -1;
        return 0;
}

/* Check for indirection: things like a.b, a['b'], a[b], a(b)... */
static int
assemble_expr4_elems(struct assemble_t *a)
{
        if (assemble_expr5_atomic(a) < 0)
                return -1;
        assemble_primary_elements(a, FEE_EVAL);
        return 0;
}

static int
assemble_expr3_unarypre(struct assemble_t *a)
{
        if (istok_unarypre(a->oc->t)) {
                int op, t = a->oc->t;

                if (t == OC_TILDE)
                        op = INSTR_BITWISE_NOT;
                else if (t == OC_MINUS)
                        op = INSTR_NEGATE;
                else if (t == OC_EXCLAIM || t == OC_NOTSTR)
                        op = INSTR_LOGICAL_NOT;
                else /* +, do nothing*/
                        op = -1;

                if (as_lex(a) < 0)
                        return -1;
                /* recurse, we could have something like !! */
                if (assemble_expr3_unarypre(a) < 0)
                        return -1;

                if (op >= 0)
                        add_instr(a, op, 0, 0);
                return 0;
        } else {
                return assemble_expr4_elems(a);
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
static int
assemble_binary_operators_r(struct assemble_t *a,
                            const struct operator_state_t *tbl)
{
        if (tbl->toktbl == NULL) {
                /* carry on to unarypre and atom */
                return assemble_expr3_unarypre(a);
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

        if (assemble_binary_operators_r(a, tbl + 1) < 0)
                return -1;

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

                if (as_lex(a) < 0)
                        return -1;
                if (assemble_binary_operators_r(a, tbl + 1) < 0)
                        return -1;

                if (!logical) {
                        if (tbl->opcode < 0)
                                add_instr(a, t->opcode, 0, 0);
                        else
                                add_instr(a, tbl->opcode, t->opcode, 0);
                }

        } while (tbl->loop);

        if (logical && have_operator) {
                if (as_set_label(a, label) < 0)
                        return -1;
                bug_on(label < 0);
        }
        return 0;
}

/* Parse and compile operators with left- and right-side operands */
static int
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
                { .tok = OC_ANDSTR, .opcode = INSTR_LOGICAL_AND, },
                { .tok = -1 }
        };
        static const struct token_to_opcode_t LOG_OR_TOK2OP[] = {
                { .tok = OC_OROR,   .opcode = INSTR_LOGICAL_OR, },
                { .tok = OC_ORSTR,  .opcode = INSTR_LOGICAL_OR, },
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
        return assemble_binary_operators_r(a, BINARY_OPERATORS);
}

static int
assemble_expr1_ternary(struct assemble_t *a)
{
        if (assemble_expr2_binary(a) < 0)
                return -1;
        if (a->oc->t == OC_QUEST) {
                int b_end = as_next_label(a);
                int b_if_false = as_next_label(a);
                add_instr(a, INSTR_B_IF, 0, b_if_false);
                if (as_lex(a) < 0)
                        return -1;
                if (assemble_expr2_binary(a) < 0)
                        return -1;
                add_instr(a, INSTR_B, 0, b_end);
                if (as_set_label(a, b_if_false) < 0)
                        return -1;
                if (a->oc->t != OC_COLON) {
                        err_setstr(SyntaxError,
                                   "Expected: ':' in ternary expression");
                        return -1;
                }
                if (as_lex(a) < 0)
                        return -1;
                if (assemble_expr2_binary(a) < 0)
                        return -1;
                if (as_set_label(a, b_end) < 0)
                        return -1;
        }
        return 0;
}

/**
 * assemble_expr - sister function to assemble_stmt.
 * @flags: If FE_CHECKTUPLE is set and the expression is followed by a
 *         comma, continue parsing expressions and put into a tuple.
 *         If FE_CHECKTUPLE is cleared, a comma at this level is not
 *         part of the expression.
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
static int
assemble_expr(struct assemble_t *a, unsigned int flags)
{
        size_t n = 1;
        if (as_lex(a) < 0)
                return -1;

        if (assemble_expr1_ternary(a) < 0)
                return -1;

        if (!!(flags & FE_CHECKTUPLE) && a->oc->t == OC_COMMA) {
                do {
                        if (as_lex(a) < 0)
                                return -1;
                        if (assemble_expr1_ternary(a) < 0)
                                return -1;
                        n++;
                } while (a->oc->t == OC_COMMA);
        }
        if (n > 1)
                add_instr(a, INSTR_DEFTUPLE, 0, n);

        as_unlex(a);
        return 0;
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
static int
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
                if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                        return -1;
                add_instr(a, asgntok2instr(t), 0, 0);
        }
        return 0;
}

/* return -1 if error, 1 if attribute modified, 0 if not */
static int
maybe_modattr(struct assemble_t *a, unsigned int flags)
{
        if (flags == FEE_DEL) {
                int t = as_lex(a);
                if (t < 0)
                        return -1;
                if (istok_indirection(t)) {
                        as_unlex(a);
                        return 0;
                }
                add_instr(a, INSTR_DELATTR, 0, 0);
                return 1;
        } else if (flags == FEE_ASGN) {
                int t = as_lex(a);
                if (t < 0)
                        return -1;
                if (istok_assign(t)) {
                        if (t == OC_EQ) {
                                if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                                        return -1;
                        } else {
                                add_instr(a, INSTR_COPY, 0, 2);
                                add_instr(a, INSTR_COPY, 0, 2);
                                add_instr(a, INSTR_GETATTR, 0, 0);
                                if (assemble_preassign(a, t) < 0)
                                        return -1;
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
 * Return: 1 if an evaluated item is dangling on the stack
 *         0 if not
 *         -1 if error
 */
static int
assemble_primary_elements(struct assemble_t *a, unsigned int flags)
{
        flags &= FEE_MASK;
        while (istok_indirection(a->oc->t)) {
                int mres;
                switch (a->oc->t) {
                case OC_PER:
                        if (as_errlex(a, OC_IDENTIFIER) < 0)
                                return -1;
                        ainstr_load_const(a, a->oc);
                        mres = maybe_modattr(a, flags);
                        if (mres < 0)
                                return -1;
                        else if (mres)
                                return 0;
                        add_instr(a, INSTR_GETATTR, 0, 0);
                        break;

                case OC_LBRACK:
                        if (assemble_slice(a) < 0)
                                return -1;
                        if (as_lex(a) < 0)
                                return -1;
                        if (a->oc->t == OC_RBRACK) {
                                mres = maybe_modattr(a, flags);
                                if (mres < 0)
                                        return -1;
                                else if (mres)
                                        return 0;
                                as_unlex(a);
                        }
                        add_instr(a, INSTR_GETATTR, 0, 0);
                        if (as_errlex(a, OC_RBRACK) < 0)
                                return -1;
                        break;

                case OC_LPAR:
                        as_unlex(a);
                        if (assemble_call_func(a) < 0)
                                return -1;
                        /*
                         * XXX: I don't know a faster way to do this
                         * without spaghettifying the code.
                         */
                        if (flags == FEE_DEL) {
                                int t;
                                if (as_lex(a) < 0)
                                        return -1;
                                t = a->oc->t;
                                as_unlex(a);
                                if (!istok_indirection(t)) {
                                        err_setstr(SyntaxError,
                                                   "Trying to delete function call");
                                        return -1;
                                }
                        }
                        break;

                default:
                        err_setstr(SyntaxError, "Invalid token");
                        return -1;
                }

                if (as_lex(a) < 0)
                        return -1;
        }

        if (!!flags && a->oc->t == OC_SEMI)
                as_unlex(a);

        return 1;
}

static int
assemble_primary_elements__(struct assemble_t *a)
{
        if (as_lex(a) < 0)
                return -1;
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

/* return 1 if item left on the stack, 0 if not, -1 if error */
static int
assemble_identifier(struct assemble_t *a, unsigned int flags)
{
        struct list_t names;
        int needsize, star;
        struct names_t *n;
        int ret;

        needsize = gather_names(a, &names, &star, ERRH_EXCEPTION);
        if (needsize < 0)
                return -1;
        if (needsize > 1) {
                /* eg. "a, b" or "a, b = (some, iterable)" */
                struct list_t *p;

                if (a->oc->t == OC_SEMI) {
                        /* empty statement, e.g. "a, b" */
                        token_pos_t pos = AS_LIST2NAMES(names.prev)->pos;
                        (void)as_swap_pos(a, pos);
                        as_unlex(a);
                        if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                                goto err_cleanup;
                        goto one;
                }

                if (a->oc->t != OC_EQ) {
                        err_ae_expect(a, OC_EQ);
                        goto err_cleanup;
                }
                if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                        goto err_cleanup;
                if (star < 0) {
                        add_instr(a, INSTR_UNPACK, 0, needsize);
                } else {
                        add_instr(a, INSTR_UNPACK_SPECIAL,
                                  0, star << 8 | needsize);
                }
                list_foreach(p, &names) {
                        n = AS_LIST2NAMES(p);
                        if (ainstr_assign_symbol(a, n->pos) < 0)
                                goto err_cleanup;
                }
                goto zero;
        }

        n = AS_LIST2NAMES(names.next);
        if (a->oc->t == OC_EQ) {
                /*
                 * x = value;
                 * Don't load, INSTR_ASSIGN_LOCAL knows where from frame
                 * pointer to store 'value'
                 */
                if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                        goto err_cleanup;
                ainstr_assign_symbol(a, n->pos);
                goto zero;
        } else if (istok_assign(a->oc->t)) {
                /*
                 * x++;
                 * x += value;
                 * ...
                 */
                if (ainstr_load_symbol(a, n->pos) < 0)
                        goto err_cleanup;
                if (assemble_preassign(a, a->oc->t) < 0)
                        goto err_cleanup;
                ainstr_assign_symbol(a, n->pos);
                goto zero;
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
                if (ainstr_load_symbol(a, n->pos) < 0)
                        goto err_cleanup;
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
                if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                        goto err_cleanup;
                goto one;
        }

err_cleanup:
        ret = -1;
        goto out;
one:
        ret = 1;
        goto out;
zero:
        ret = 0;
        goto out;
out:
        cleanup_names(&names);
        return ret;
}

static int
assemble_delete(struct assemble_t *a, unsigned int flags)
{
        token_pos_t pos;
        struct token_t *name;

        if (as_lex(a) < 0)
                return -1;
        pos = as_savetok(a, &name);

        if (as_lex(a) < 0)
                return -1;
        if (istok_indirection(a->oc->t)) {
                if (name->t != OC_THIS && name->t != OC_IDENTIFIER)
                        goto baddelete;
                if (ainstr_load_symbol(a, pos) < 0)
                        return -1;
                if (assemble_primary_elements(a, FEE_DEL) < 0)
                        return -1;
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
                if (name->t != OC_IDENTIFIER)
                        goto baddelete;

                as_unlex(a);
                ainstr_load_null(a);
                ainstr_assign_symbol(a, pos);
        }
        return 0;

baddelete:
        /* back up for more accurate error reporting */
        as_unlex(a);
        err_setstr(SyntaxError, "Invalid expression for delete");
        return -1;
}

/*
 * helper to assemble_declarator_stmt
 *
 * return index >= 0, or -1 if error
 */
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
                        return -1;
                }
        } else {
                bug_on(!name);
                namei = fakestack_declare(a, name->v);
                if (namei < 0)
                        return -1;
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

static int
assemble_declarator_stmt(struct assemble_t *a, int tok, unsigned int flags)
{
        struct list_t names, *p;
        bool global;
        int needsize, star;
        unsigned int extraflags = 0;

        if (as_lex(a) < 0)
                return -1;
        if (a->oc->t != OC_MUL && a->oc->t != OC_IDENTIFIER) {
                err_ae_expect(a, OC_IDENTIFIER);
                return -1;
        }

        needsize = gather_names(a, &names, &star, ERRH_EXCEPTION);
        if (needsize < 0)
                return -1;
        (void)star;

        global = tok == OC_GBL;

        if (a->oc->t == OC_EQ)
                extraflags = FE_SKIPNULLASSIGN;

        list_foreach(p, &names) {
                struct names_t *n = AS_LIST2NAMES(p);
                struct token_t *tok = AS_NAME2TOK(a, n);
                n->namei = assemble_declare(a, tok,
                                            global, flags | extraflags);
                if (n->namei < 0)
                        goto err_clean;
        }

        if (a->oc->t == OC_SEMI) {
                /*
                 * XXX: star >= 0 means someone typed eg. 'let a, *b, c;'
                 * We are allowing it and ignoring the star. Should we?
                 */
                as_unlex(a);
                goto done;
        }

        if (a->oc->t != OC_EQ) {
                /* for initializers, only '=', not '+=' or such */
                err_ae_expect(a, OC_EQ);
                goto err_clean;
        }

        if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                goto err_clean;

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
done:
        cleanup_names(&names);
        return 0;
err_clean:
        cleanup_names(&names);
        return -1;
}

static int
assemble_return(struct assemble_t *a)
{
        if (as_lex(a) < 0)
                return -1;
        if (a->oc->t == OC_SEMI) {
                as_unlex(a);
                ainstr_return_null(a);
        } else {
                as_unlex(a);
                if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                        return -1;
                add_instr(a, INSTR_RETURN_VALUE, 0, 0);
        }
        return 0;
}

static int
assemble_try(struct assemble_t *a)
{
        struct token_t *exctok;
        int finally = as_next_label(a);
        int catch = as_next_label(a);

        if (ainstr_push_block(a, IARG_TRY, catch) < 0)
                return -1;

        /* block of the try { ... } statement */
        if (assemble_stmt(a, 0, 0) < 0)
                return -1;
        add_instr(a, INSTR_B, 0, finally);

        ainstr_pop_block(a, IARG_TRY);

        if (as_errlex(a, OC_CATCH) < 0)
                return -1;
        if (as_set_label(a, catch) < 0)
                return -1;

        /* block of the catch(x) { ... } statement */
        if (as_errlex(a, OC_LPAR) < 0)
                return -1;
        if (as_errlex(a, OC_IDENTIFIER) < 0)
                return -1;
        as_savetok(a, &exctok);
        if (as_errlex(a, OC_RPAR) < 0)
                return -1;
        /*
         * No instructions for pushing this on the stack.
         * The exception handler will do that for us in
         * execute loop.
         */
        if (fakestack_declare(a, exctok->v) < 0)
                return -1;
        if (assemble_stmt(a, 0, 0) < 0)
                return -1;
        if (as_lex(a) < 0)
                return -1;

        if (as_set_label(a, finally) < 0)
                return -1;

        if (a->oc->t == OC_FINALLY) {
                /* block of the finally { ... } statement */
                if (assemble_stmt(a, 0, 0) < 0)
                        return -1;
        } else {
                as_unlex(a);
        }
        return 0;
}

static int
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
                if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                        return -1;
                add_instr(a, INSTR_B_IF, 0, jmpelse);
                if (assemble_stmt(a, 0, 0) < 0)
                        return -1;
                add_instr(a, INSTR_B, 0, true_jmpend);
                if (as_set_label(a, jmpelse) < 0)
                        return -1;

                if (as_lex(a) < 0)
                        return -1;
                if (a->oc->t == OC_ELSE) {
                        jmpelse = jmpend;
                        if (as_lex(a) < 0)
                                return -1;
                } else {
                        as_unlex(a);
                        if (as_set_label(a, jmpend) < 0)
                                return -1;
                        goto done;
                }
        }

        /* final else */
        as_unlex(a);
        if (as_set_label(a, jmpelse) < 0)
                return -1;
        if (assemble_stmt(a, 0, 0) < 0)
                return -1;

done:
        if (as_set_label(a, true_jmpend) < 0)
                return -1;
        return 0;
}

static int
assemble_while(struct assemble_t *a)
{
        int start = as_next_label(a);
        int breakto = as_next_label(a);

        if (ainstr_push_block(a, IARG_LOOP, breakto) < 0)
                return -1;

        if (as_set_label(a, start) < 0)
                return -1;

        if (as_errlex(a, OC_LPAR) < 0)
                return -1;
        if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                return -1;
        if (as_errlex(a, OC_RPAR) < 0)
                return -1;

        add_instr(a, INSTR_B_IF, 0, breakto);
        if (assemble_stmt(a, FE_CONTINUE, start) < 0)
                return -1;
        add_instr(a, INSTR_B, 0, start);

        ainstr_pop_block(a, IARG_LOOP);

        if (as_set_label(a, breakto) < 0)
                return -1;
        return 0;
}

static int
assemble_do(struct assemble_t *a)
{
        int start = as_next_label(a);
        int breakto = as_next_label(a);

        if (ainstr_push_block(a, IARG_LOOP, breakto) < 0)
                return -1;

        if (as_set_label(a, start) < 0)
                return -1;
        if (assemble_stmt(a, FE_CONTINUE, start) < 0)
                return -1;
        if (as_errlex(a, OC_WHILE) < 0)
                return -1;
        if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                return -1;
        add_instr(a, INSTR_B_IF, 1, start);

        ainstr_pop_block(a, IARG_LOOP);

        if (as_set_label(a, breakto) < 0)
                return -1;
        return 0;
}

static int
assemble_foreach2(struct assemble_t *a, struct list_t *names,
                  int star, int needsize)
{
        int iternext = as_next_label(a);
        if (needsize == 1) {
                /* needle is the 'a' of 'for (a in b)' */
                struct names_t *n = AS_LIST2NAMES(names->next);
                struct token_t *tok = AS_NAME2TOK(a, n);
                bug_on(!tok->v);
                n->namei = fakestack_declare(a, tok->v);
                if (n->namei < 0)
                        return -1;
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
                        struct token_t *tok = AS_NAME2TOK(a, n);
                        bug_on(!tok->v);
                        n->namei = fakestack_declare(a, tok->v);
                        if (n->namei < 0)
                                return -1;
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
        if (assemble_stmt(a, FE_CONTINUE, iternext) < 0)
                return -1;
        if (as_set_label(a, iternext) < 0)
                return -1;
        return 0;
}

static int
assemble_foreach1(struct assemble_t *a, int breakto)
{
        struct list_t names;
        int star;
        int forelse = as_next_label(a);
        int iter = as_next_label(a);
        int needsize;
        bool have_par = false;

        if (as_lex(a) < 0)
                return -1;
        if (a->oc->t == OC_LPAR) {
                have_par = true;
                if (as_lex(a) < 0)
                        return -1;
        }

        /* save names of the 'needles' in 'for (needles in haystack)' */
        if (a->oc->t != OC_MUL && a->oc->t != OC_IDENTIFIER) {
                err_ae_expect(a, OC_IDENTIFIER);
                return -1;
        }
        needsize = gather_names(a, &names, &star, ERRH_EXCEPTION);
        if (needsize < 0)
                return -1;

        if (a->oc->t != OC_IN) {
                err_ae_expect(a, OC_IN);
                goto err_cleanup;
        }

        /* push 'haystack onto the stack */
        if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                goto err_cleanup;

        if (have_par) {
                if (as_lex(a) < 0)
                        goto err_cleanup;
                if (a->oc->t != OC_RPAR) {
                        err_ae_expect(a, OC_RPAR);
                        goto err_cleanup;
                }
        }

        /* maybe replace 'haystack' with its keys */
        add_instr(a, INSTR_FOREACH_SETUP, 0, 0);
        if (as_set_label(a, iter) < 0)
                goto err_cleanup;
        add_instr(a, INSTR_FOREACH_ITER, 0, forelse);

        if (ainstr_push_block(a, IARG_BLOCK, 0) < 0)
                goto err_cleanup;
        if (assemble_foreach2(a, &names, star, needsize) < 0)
                goto err_cleanup;
        ainstr_pop_block(a, IARG_BLOCK);

        add_instr(a, INSTR_B, 0, iter);

        if (as_set_label(a, forelse) < 0)
                goto err_cleanup;
        if (as_lex(a) < 0)
                goto err_cleanup;
        if (a->oc->t == OC_EOF) {
                as_badeof(a);
                goto err_cleanup;
        } else if (a->oc->t == OC_ELSE) {
                if (assemble_stmt(a, 0, 0) < 0)
                        goto err_cleanup;
        } else {
                as_unlex(a);
        }
        cleanup_names(&names);
        return 0;

err_cleanup:
        cleanup_names(&names);
        return -1;
}

static int
assemble_foreach(struct assemble_t *a)
{
        int breakto = as_next_label(a);
        if (ainstr_push_block(a, IARG_LOOP, breakto) < 0)
                return -1;

        if (assemble_foreach1(a, breakto) < 0)
                return -1;

        ainstr_pop_block(a, IARG_LOOP);
        if (as_set_label(a, breakto) < 0)
                return -1;
        return 0;
}

static int
assemble_throw(struct assemble_t *a)
{
        if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                return -1;
        add_instr(a, INSTR_THROW, 0, 0);
        return 0;
}

/*
 * parse '{' stmt; stmt;... '}'
 * The first '{' has already been read.
 */
static int
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

        if (ainstr_push_block(a, arg1, arg2) < 0)
                return -1;

        /* don't pass this down */
        flags &= ~FE_CONTINUE;

        for (;;) {
                /* peek for end of compound statement */
                if (as_lex(a) < 0)
                        return -1;
                if (a->oc->t == OC_RBRACE)
                        break;
                as_unlex(a);

                if (assemble_stmt(a, flags, -1) < 0)
                        return -1;
        }
        ainstr_pop_block(a, arg1);
        return 0;
}

/* parse the stmt of 'stmt' + ';' */
static int
assemble_stmt_simple(struct assemble_t *a, unsigned int flags,
                     int continueto)
{
        int pop_arg, need_pop = 0;

        /*
         * XXX: We need a better way to know if we are...
         *      in TTY mode at the top
         *      in script mode
         *      in string mode at the top
         *      in string mode not at the top
         */
        if (!!(flags & FE_TOP) && a->fp)
                pop_arg = IARG_POP_PRINT;
        else
                pop_arg = IARG_POP_NORMAL;

        if (as_lex(a) < 0)
                return -1;
        /* cases return early if semicolon not expected at the end */
        switch (a->oc->t) {
        case OC_DELETE:
                return assemble_delete(a, flags);
        case OC_EOF:
                return 0;
        case OC_MUL:
        case OC_IDENTIFIER:
                need_pop = assemble_identifier(a, flags);
                if (need_pop < 0)
                        return -1;
                break;
        case OC_THIS:
                /* not a saucy challenge */
                need_pop = assemble_this(a, flags);
                if (need_pop < 0)
                        return -1;
                break;
        case OC_SEMI:
                /* empty statement */
                as_unlex(a);
                break;
        case OC_RPAR:
                err_setstr(SyntaxError, "Unbalanced parenthesis");
                return -1;
        case OC_LET:
        case OC_GBL:
                if (assemble_declarator_stmt(a, a->oc->t, flags) < 0)
                        return -1;
                break;
        case OC_RETURN:
                if (!!(flags & FE_TOP)) {
                        err_setstr(SyntaxError,
                                "Cannot return in interactive mode while outside of a function");
                        return -1;
                }
                if (assemble_return(a) < 0)
                        return -1;
                break;
        case OC_BREAK:
                add_instr(a, INSTR_BREAK, 0, 0);
                break;
        case OC_CONTINUE:
                add_instr(a, INSTR_CONTINUE, 0, 0);
                break;
        case OC_THROW:
                if (assemble_throw(a) < 0)
                        return -1;
                break;
        case OC_TRY:
                return assemble_try(a);
        case OC_IF:
                return assemble_if(a);
        case OC_WHILE:
                return assemble_while(a);
        case OC_FOR:
                return assemble_foreach(a);
        case OC_LBRACE:
                return assemble_block_stmt(a, flags & ~FE_TOP, continueto);
        case OC_DO:
                return assemble_do(a);
        default:
                /* value expression */
                as_unlex(a);
                if (assemble_expr(a, FE_CHECKTUPLE) < 0)
                        return -1;
                need_pop = 1;
                break;
        }

        /* Throw result away */
        if (need_pop) {
                if (!a->fp) {
                        add_instr(a, INSTR_RETURN_VALUE, 0, 0);
                } else {
                        add_instr(a, INSTR_POP, pop_arg, 0);
                }
        }

        if (as_lex(a) < 0)
                return -1;
        if (a->oc->t != OC_SEMI) {
                err_ae_expect(a, OC_SEMI);
                return -1;
        }
        return 0;
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
 * See Documentation for the details.
 */
static int
assemble_stmt(struct assemble_t *a, unsigned int flags, int continueto)
{
        int ret;

        RECURSION_DEFAULT_START(as_recursion);
        ret = assemble_stmt_simple(a, flags, continueto);
        RECURSION_END(as_recursion);

        return ret;
}

static struct assemble_t *
new_assembler(const char *source_file_name, FILE *fp,
              Object *localdict, const char *src_str)
{
        struct assemble_t *a;
        struct token_state_t *prog;

        if (fp) {
                prog = token_state_new(fp, notdir(source_file_name));
                if (!prog) /* no tokens, just eof */
                        return NULL;
        } else {
                bug_on(!src_str);
                prog = token_state_from_string(src_str);
        }

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

static struct assemble_t *
new_string_assembler(const char *str)
{
        return new_assembler("<string>", NULL, NULL, str);
}

static struct assemble_t *
new_file_assembler(const char *source_file_name,
                   FILE *fp, Object *localdict)
{
        return new_assembler(source_file_name, fp, localdict, NULL);
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
static Object *
assemble_next(struct assemble_t *a, bool toeof, unsigned int flags)
{
        struct xptrvar_t *ex;

        if (a->oc && a->oc->t == OC_EOF)
                return NULL;

        do {
                if (assemble_stmt(a, flags, -1) < 0) {
                        bug_on(!err_occurred());
                        /*
                         * If in string mode, we were called from UAPI
                         * eval(); pass the exception upstream.
                         * otherwise, print the error.
                         *
                         * FIXME: What if we're here from import()?
                         * We would also want to pass the exception
                         * upstream, but here we don't know whether
                         * we should do that.
                         */
                        if (a->fp)
                                assemble_splash_error(a);
                        return ErrorVar;
                }
        } while (toeof && a->oc->t != OC_EOF);
        add_instr(a, INSTR_END, 0, 0);

        list_remove(&a->fr->list);
        list_add_front(&a->fr->list, &a->finished_frames);

        ex = assemble_post(a);
        return (Object *)ex;
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
        int res = as_set_label(a, as_next_label(a));
        bug_on(res < 0);
        (void)res;
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
 *
 * Return: One of the following...
 *            - XptrType object ready for the VM to use
 *            - ErrroVar if error
 *            - NULL if EOF encountered before there was anything to
 *              interpret.
 */
Object *
assemble(const char *filename, FILE *fp, Object *localdict)
{
        int firsttok;
        Object *ret;
        struct assemble_t *a;
        bool toeof;
        CLOCK_DECLARE();

        a = new_file_assembler(filename, fp, localdict);
        if (!a)
                return NULL;

        toeof = !localdict;

        /*
         * If first token is a dot, it can't be EvilCandy source,
         * but it could be a disassembly.
         */
        CLOCK_SAVE();
        if (as_lex(a) < 0) {
                /* oops, never mind, something's unparseable */
                free_assembler(a);
                return ErrorVar;
        }
        firsttok = a->oc->t;
        as_unlex(a);
        if (toeof && firsttok == OC_PER) {
                ret = (Object *)reassemble(a);
                if (!ret) {
                        /* reassemble can only succeed or fail */
                        err_print_last(stderr);
                        ret = ErrorVar;
                }
        } else {
                ret = assemble_next(a, toeof, localdict ? FE_TOP : 0);
        }
        CLOCK_REPORT();

        bug_on(ret != ErrorVar && err_occurred());
        free_assembler(a);

        return ret;
}

/**
 * assemble_string - Like assemble, but using a C string for input
 * @str: string containing EvilCandy-syntax code.
 *
 * Return: Same type of result as assemble().
 */
Object *
assemble_string(const char *str)
{
        struct assemble_t *a;
        Object *ret;

        a = new_string_assembler(str);
        if (!a)
                return NULL;

        ret = assemble_next(a, true, FE_TOP);
        free_assembler(a);
        return ret;
}

