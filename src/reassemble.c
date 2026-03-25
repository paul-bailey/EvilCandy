/*
 * reassemble.c - Re-assemble disassembled file
 *
 * This does not parse the pretty, human-readable enumerated-and-commented
 * assembly files created with the -d option.  Instead it reads files where
 * an opcode line might be something like "12 1 1".  (Big comment in
 * reassemble() why we do this.)  At most it skips the pound '#' comment
 * character.
 *
 * The entry point is reassemble()
 */
#include <evilcandy.h>
#include <token.h>
#include "assemble_priv.h"
#include <errno.h>
#include <stdlib.h>

struct reassemble_t {
        struct assemble_t *a;   /* info passed from assemble() */
        FILE *fp;               /* input stream */
        char *line;             /* current line being processed */
        size_t line_len;        /* arg to egetline */
        int lineno;             /* current line number */
        char *s;                /* pointer into @line */
};

static ssize_t
ra_next_line(struct reassemble_t *ra)
{
        char *s;
        ssize_t nread;

        do {
                nread = egetline(&ra->line, &ra->line_len, ra->fp);
                if (nread <= 0)
                        return nread;
                ra->lineno++;

                s = ra->line;
                while (*s != '\0' && isspace((int)*s))
                        s++;
        } while (*s == '\0' || *s == '#');
        ra->s = s;
        return nread;
}

static char *
skip_ws(const char *s)
{
        while (*s != '\0' && isspace((int)*s))
                s++;
        if (*s == '#')
                return strchr(s, '\0');
        return (char *)s;
}

static void
ra_err(struct reassemble_t *ra, const char *msg)
{
        err_setstr(SyntaxError, "Line %d: %s", ra->lineno, msg);
}

static void
err_extratok(struct reassemble_t *ra)
{
        ra_err(ra, "Extra token");
}

/*
 * Parse a line either which contains a number and a colon (a jump label,
 * useful for making re-assembly human-readable), or which contains two
 * unsigned integers (the opcode and its first argument) and one signed
 * integer (the second argument).  Add parsed data to their proper
 * assembly-frame array.
 */
static int
parse_opcodes(struct reassemble_t *ra, const char *pc)
{
        unsigned long code, arg1;
        long arg2;
        char *endptr;

        errno = 0;
        code = strtoul(pc, &endptr, 0);
        if (errno || endptr == pc)
                goto err;
        pc = skip_ws(endptr);

        /* Check if this is a label instead */
        if (*pc == ':') {
                pc = skip_ws(pc + 1);
                if (*pc != '\0') {
                        err_extratok(ra);
                        return -1;
                }
                assemble_label_here(ra->a);
                return 0;
        }

        /* Not a label, carry on with opcodes */
        if (code >= N_INSTR)
                goto err;
        arg1 = strtoul(pc, &endptr, 0);
        if (errno || endptr == pc || arg1 > 255)
                goto err;
        pc = skip_ws(endptr);
        arg2 = strtol(pc, &endptr, 0);
        if (errno || endptr == pc || arg2 < -32768 || arg2 > 32767)
                goto err;
        pc = skip_ws(endptr);
        if (*pc != '\0') {
                err_extratok(ra);
                return -1;
        }

        if ((code == INSTR_ASSIGN_LOCAL || code == INSTR_LOAD_LOCAL) &&
            arg1 == IARG_PTR_AP) {
                int idx = arg2;
                int max = ra->a->fr->af_nlocals;
                if (idx < 0)
                        goto err;
                if (idx >= max)
                        ra->a->fr->af_nlocals = idx + 1;
        }

        assemble_add_instr(ra->a, code, arg1, arg2);
        return 0;

err:
        ra_err(ra, "Malformed opcode");
        return -1;
}

/*
 * Parse the first non-empty line of the input,
 * verify it's '.evilcandy "version"', where "version"
 * matches our own VERSION in config.h
 */
static int
check_version(struct reassemble_t *ra, const char *pc)
{
        const char *version;

        if (strncmp(pc, ".evilcandy", 10))
                goto err_firstline;
        pc = skip_ws(pc + 10);
        if (*pc != '"')
                goto err_firstline;

        pc++;
        version = pc;
        while (*pc != '\0' && *pc != '"')
                pc++;
        if (*pc == '\0')
                goto err_firstline;
        if (strncmp(version, VERSION, pc - version)) {
                ra_err(ra, "Refusing to reassemble: version mismatch");
                return -1;
        }
        pc = skip_ws(pc + 1);
        if (*pc != '\0') {
                err_extratok(ra);
                return -1;
        }
        return 0;

err_firstline:
        ra_err(ra, "Expected first line: .evilcandy VERSION");
        return -1;
}

/*
 * Parse a ".start <#####>" line. @pc is at the beginning of the line.
 * Get the id number between the angle brackets and, if @may_push is set,
 * get a new assembly frame using the new id number.
 */
static int
parse_funcid(struct reassemble_t *ra, const char *pc, bool may_push)
{
        char *endptr;
        unsigned long long id;

        if (strncmp(pc, ".start", 6))
                goto err_start;
        pc = skip_ws(pc + 6);
        if (*pc != '<')
                goto err_start;
        pc++;
        errno = 0;
        id = strtoull(pc, &endptr, 0);
        if (errno || endptr == pc)
                goto err_start;
        pc = endptr;
        if (*pc != '>')
                goto err_start;
        pc = skip_ws(pc + 1);
        if (*pc != '\0') {
                err_extratok(ra);
                return -1;
        }

        /*
         * Don't do this for entry point, new_assembler() already did
         * that.  We're losing the function number here, but that's OK
         * because no one needs to point to the entry point.
         */
        if (may_push)
                assemble_frame_push(ra->a, id);

        return 0;

err_start:
        ra_err(ra, "Expected: .start <ID>");
        return -1;
}

static Object *parse_rodata1(struct reassemble_t *ra,
                             struct token_state_t *tkstate);

/* parse rodata as a tuple or complex expression */
static Object *
parse_rodata2(struct reassemble_t *ra, struct token_state_t *tkstate)
{
        Object *o;
        struct token_t *tok;
        struct buffer_t b;
        bool istuple = false;
        size_t n;
        Object **stack;

        buffer_init(&b);
        n = 0;

        do {
                Object *child;

                /* if we have at least one comma, it's a tuple */
                if (n > 0)
                        istuple = true;

                /* first, check for zero- or one-sized tuple */
                if (get_tok(tkstate, &tok) < 0)
                        goto err_tupleclean;
                if (tok->t == OC_RPAR)
                        break;
                unget_tok(tkstate, &tok);

                child = parse_rodata1(ra, tkstate);
                if (child == ErrorVar)
                        goto err_tupleclean;
                n++;
                buffer_putd(&b, &child, sizeof(Object *));
                if (get_tok(tkstate, &tok) < 0)
                        goto err_tupleclean;
        } while (tok->t == OC_COMMA);
        if (tok->t != OC_RPAR) {
                ra_err(ra, "malformed tuple");
                goto err_tupleclean;
        }

        stack = (Object **)b.s;
        bug_on(n != buffer_size(&b) / sizeof(Object *));
        if (!istuple) {
                /* Can only be a complex number */
                bug_on(n != 1);
                o = stack[0];
                if (!isvar_complex(o)) {
                        ra_err(ra, "malformed tuple");
                        goto err_tupleclean;
                }
        } else {
                if (n) {
                        o = tuplevar_from_stack(stack, n, true);
                } else {
                        o = tuplevar_new(0);
                }
        }
        buffer_free(&b);
        return o;

err_tupleclean:
        if (n > 0) {
                size_t i;
                stack = (Object **)b.s;
                /*
                 * n could be different depending where the error
                 * was above.
                 */
                n = buffer_size(&b) / sizeof(Object *);
                for (i = 0; i < n; i++)
                        VAR_DECR_REF(stack[i]);
        }
        buffer_free(&b);
        return ErrorVar;
}

static Object *
parse_rodata1(struct reassemble_t *ra, struct token_state_t *tkstate)
{
        Object *o;
        struct token_t *tok;
        int sign;

        if (get_tok(tkstate, &tok) < 0)
                return ErrorVar;
        if (tok->t == OC_LPAR)
                return parse_rodata2(ra, tkstate);

        if (tok->t == OC_LT) {
                /* ID for more code */
                long long id;
                if (get_tok(tkstate, &tok) < 0)
                        return ErrorVar;
                if (tok->t != OC_INTEGER) {
                        ra_err(ra, "malformed function ID");
                        return ErrorVar;
                }
                id = intvar_toll(tok->v);
                if (get_tok(tkstate, &tok) < 0)
                        return ErrorVar;
                if (tok->t != OC_GT) {
                        ra_err(ra, "malformed function ID");
                        return ErrorVar;
                }
                return idvar_new(id);
        }

        sign = 0;
        if (tok->t == OC_MINUS) {
                sign = -1;
                if (get_tok(tkstate, &tok) < 0)
                        return ErrorVar;
        } else if (tok->t == OC_PLUS) {
                sign = 1;
                if (get_tok(tkstate, &tok) < 0)
                        return ErrorVar;
        }
        if (tok->v == NULL) {
                ra_err(ra, "Malformed rodata token");
                return ErrorVar;
        }
        o = VAR_NEW_REF(tok->v);
        if (isvar_number(o)) {
                if (!sign)
                        sign = 1;
                if (sign < 0) {
                        Object *tmp = qop_negate(o);
                        bug_on(!tmp || tmp == ErrorVar);
                        VAR_DECR_REF(o);
                        o = tmp;
                }
        } else if (sign) {
                ra_err(ra, "Malformed rodata token");
                return ErrorVar;
        }
        if (get_tok(tkstate, &tok) < 0)
                goto err_clean_o;

        if (tok->t != OC_MINUS && tok->t != OC_PLUS) {
                /* real number or some other one-token object */
                unget_tok(tkstate, &tok);
                return o;
        }

        /* By now, expression can only be +/- REAL +/- IMAGINARY */

        if (!isvar_real(o)) {
                ra_err(ra, "Malformed rodata token");
                goto err_clean_o;
        }
        sign = tok->t == OC_MINUS ? -1 : 1;
        if (get_tok(tkstate, &tok) < 0)
                goto err_clean_o;

        if (tok->t != OC_COMPLEX) {
                ra_err(ra, "Malformed rodata token");
                goto err_clean_o;
        }
        {
                binary_operator_t func = sign < 0 ? qop_sub : qop_add;
                Object *tmp = func(0, tok->v);
                bug_on(!tmp || tmp == ErrorVar);
                o = tmp;
                VAR_DECR_REF(tmp);
        }
        return o;

err_clean_o:
        VAR_DECR_REF(o);
        return ErrorVar;
}

static int
parse_rodata(struct reassemble_t *ra, const char *pc)
{
        struct token_state_t *tkstate = token_state_from_string(pc);
        struct token_t *tok;
        Object *o = parse_rodata1(ra, tkstate);
        if (o == ErrorVar)
                goto err_clean_tokstate;

        if (get_tok(tkstate, &tok) < 0)
                goto err_clean;
        if (tok->t != OC_EOF) {
                ra_err(ra, "excess tokens on line");
                goto err_clean;
        }

        token_state_free(tkstate);
        assemble_seek_rodata(ra->a, o);
        VAR_DECR_REF(o);
        return 0;

err_clean:
        VAR_DECR_REF(o);
err_clean_tokstate:
        token_state_free(tkstate);
        return -1;
}

/**
 * reassemble - Reassemble a disassembly file.
 *
 * Return: The entry-point compiled XptrType object.
 *
 * Called from assemble() when it detects that the file is a disassembly
 * instead of a regular source file.
 */
struct xptrvar_t *
reassemble(struct assemble_t *a)
{
        struct reassemble_t ra;
        char *pc;
        ssize_t nread;
        int havefunc;

        ra.a = a;
        ra.fp = a->fp;
        ra.lineno = 0;
        ra.line = ra.s = NULL;
        ra.line_len = 0;

        /*
         * It was tempting, because it's "cleaner", to use @a's token
         * state, since all of a disassembly file's tokens (verbose or
         * otherwise) are a subset of EvilCandy's valid tokens.
         * (A directive like ".rodata" is two tokens, OC_PER and
         * OC_IDENTIFIER).
         *
         * The problem is that even the most minimal disassembly files
         * have like a bazillion lines like
         *      12 0 3
         * creating three object per line, which we'd immediately toss
         * as soon as we finish adding the opcode.  Some load-time speed
         * tests confirm that this was taking up to four times longer
         * than just compiling the source code.  That totally defeats the
         * purpose of serialization in the first place!
         *
         * So instead we're going to forgo the ability to re-assemble
         * verbose, enumerated, human-readable disassembly, because we'll
         * manually parse all but the .rodata tokens.
         */
        rewind(a->fp);

        nread = ra_next_line(&ra);
        if (nread <= 0) {
                err_setstr(SystemError,
                           "(possible bug) end of disassembly before first instruction");
                return NULL;
        }

        if (check_version(&ra, ra.s) < 0)
                goto e_free_state;

        havefunc = 0;
        /* for each function... */
        for (;;) {
                nread = ra_next_line(&ra);
                if (nread <= 0)
                        break;

                /* get ID from .start directive */
                if (parse_funcid(&ra, ra.s, havefunc) < 0)
                        goto e_free_state;
                havefunc = 1;

                /* get opcodes */
                for (;;) {
                        nread = ra_next_line(&ra);
                        if (nread <= 0)
                                goto err_noend;
                        pc = ra.s;
                        if (*pc == '.')
                                break;

                        if (parse_opcodes(&ra, pc) < 0)
                                goto e_free_state;
                }

                /* get .rodata if any */
                for (;;) {
                        /* we already got line that starts w/ '.' */
                        if (strncmp(pc, ".rodata", 7))
                                break;
                        pc = skip_ws(pc + 7);
                        if (parse_rodata(&ra, pc) < 0)
                                goto e_free_state;

                        nread = ra_next_line(&ra);
                        if (nread <= 0)
                                goto err_noend;
                        pc = ra.s;
                }

                /* all functions must end with .end directive */
                if (strncmp(pc, ".end", 4)) {
                        ra_err(&ra, "Expected: .end or .rodata");
                        goto e_free_state;
                }

                assemble_frame_pop(a);
        }

        if (ra.line)
                efree(ra.line);
        /*
         * .prev instead of .next, because assemble_frame_pop() puts
         * finished assembly frames at the front of the list.
         * Normal-source assembly is recursive, so entry point is also
         * the last to be popped, putting it at the front of the list.
         * But in our case, there's no recursion.  So the entry point is
         * at the back of the list.
         */
        return assemble_frame_to_xptr(a,
                                list2frame(a->finished_frames.prev));

err_noend:
        err_setstr(SyntaxError, "End of input before expected .end");
        goto e_free_state;

e_free_state:
        if (ra.line)
                efree(ra.line);
        return NULL;
}

