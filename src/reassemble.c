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

/*
 * Parse one rodata line.
 * @pc points at first nonwhitespace character beyond ".rodata"
 */
static int
parse_rodata(struct reassemble_t *ra, const char *pc)
{
        char *endptr;
        Object *o;
        struct token_t tok;
        int status, negative;

        /*
         * .rodata is either an ID to more code, or an atomic--but not
         * necessarily single-token--object.  Numbers are the only
         * objects that might have multiple tokens, but we can parse
         * the plus, minus, negative signs easily enough without
         * egregiously usurping the tokenizer's job.  For everything
         * else, it's best to leave it to the tokenizer.
         */

        if (*pc == '<') {
                /* ID for more code */
                long long id;

                pc++;
                errno = 0;
                id = strtoull(pc, &endptr, 0);
                if (errno || endptr == pc) {
                        ra_err(ra, "Malformed function ID");
                        return -1;
                }
                pc = endptr;
                if (*pc != '>') {
                        ra_err(ra, "Missing '>'");
                        return -1;
                }
                pc = skip_ws(pc + 1);
                if (*pc != '\0') {
                        err_extratok(ra);
                        return -1;
                }
                o = idvar_new(id);
                goto done;
        }

        negative = 0;
        if (*pc == '-') {
                pc++;
                negative = 1;
        }
        tok.v = NULL;
        status = get_tok_from_cstring(pc, &endptr, &tok);
        if (status < 0 || tok.v == NULL) {
                ra_err(ra, "Malformed rodata token");
                return -1;
        }
        o = tok.v;
        pc = skip_ws(endptr);
        if (negative) {
                /* negative number */
                if (tok.t != OC_INTEGER && tok.t != OC_FLOAT) {
                        ra_err(ra, "Unary minus before a non-number");
                        goto err_clear_left;
                }
                Object *tmp = qop_negate(o);
                bug_on(!tmp || tmp == ErrorVar);
                VAR_DECR_REF(o);
                o = tmp;
        } else if (tok.t != OC_INTEGER && tok.t != OC_FLOAT) {
                /* Not a number */
                if (*pc != '\0') {
                        err_extratok(ra);
                        goto err_clear_left;
                }
                goto done;
        }

        /* real number: "[-]X", or... */
        if (*pc == '\0')
                goto done;

        /* ...complex number. pc at "+/- Imag" */
        negative = 0;
        if (*pc == '-' || *pc == '+') {
                if (*pc == '-')
                        negative = 1;
                pc = skip_ws(pc + 1);
        } else {
                /* ...or just bad input */
                err_extratok(ra);
                goto err_clear_left;
        }

        tok.v = NULL;
        status = get_tok_from_cstring(pc, &endptr, &tok);
        if (status < 0 || tok.t != OC_COMPLEX) {
                ra_err(ra, "Expected: complex number");
                goto err_clear_left;
        }

        pc = skip_ws(endptr);
        if (*pc != '\0') {
                err_extratok(ra);
                goto err_clear_left;
        }

        {
                binary_operator_t func = negative ? qop_sub : qop_add;
                Object *tmp = func(o, tok.v);
                bug_on(!tmp || tmp == ErrorVar);
                VAR_DECR_REF(o);
                VAR_DECR_REF(tok.v);
                o = tmp;
        }

done:
        bug_on(!o);
        assemble_seek_rodata(ra->a, o);
        VAR_DECR_REF(o);
        return 0;

err_clear_left:
        if (o)
                VAR_DECR_REF(o);
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

