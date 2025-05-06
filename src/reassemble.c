#include <evilcandy.h>
#include <token.h>
#include "assemble_priv.h"

enum {
        MAX_TOKS_PER_LINE = 10,
};

/* toks must have at least MAX_TOKS_PER_LINE */
static int
lex_line(struct assemble_t *a, struct token_t *toks, int *pline)
{
        return assemble_get_line(a, toks, MAX_TOKS_PER_LINE, pline);
}

static int
opcode_check(int code, int arg1, int arg2)
{
        if ((unsigned)code >= N_INSTR
            || (unsigned)arg1 > 255 || arg2 < -32768 || arg2 > 32767)
                return -1;
        return 0;
}

static int
tok2arg(struct token_t *tok, Object *dict)
{
        enum { GUARANTEED_BAD_ARG = -32769 };
        int ret;
        if (tok->t == OC_IDENTIFIER) {
                if (!dict)
                        return GUARANTEED_BAD_ARG;
                Object *v = dict_getitem(dict, tok->v);
                if (!v) {
                        ret = GUARANTEED_BAD_ARG;
                } else {
                        ret = intvar_toi(v);
                        VAR_DECR_REF(v);
                }
        } else {
                ret = intvar_toi(tok->v);
        }
        if (err_occurred()) {
                err_clear();
                ret = GUARANTEED_BAD_ARG;
        }
        return ret;
}

static int
parse_opcode(struct assemble_t *a, struct token_t *toks, int ntok, Object *dict)
{
        /* Going in, we know ntok >= 4, tok[0]==ID, tok[2]=="," */
        Object *oarg;
        int opcode, arg1, arg2, negative;
        struct token_t *start, *end = toks + ntok;

        opcode = instruction_from_name(toks[0].s);
        arg1 = tok2arg(&toks[1], dict);
        start = &toks[3];

        if (start->t == OC_MINUS) {
                negative = 1;
                start++;
        } else {
                negative = 0;
        }
        if (start == end || start->t != OC_INTEGER)
                return -1;

        oarg = start->v;
        start++;
        if (start != end)
                return -1;

        if (negative) {
                Object *res = qop_negate(oarg);
                if (!res || res == ErrorVar)
                        return -1;
                oarg = res;
        } else {
                VAR_INCR_REF(oarg);
        }

        arg2 = intvar_toi(oarg);
        VAR_DECR_REF(oarg);

        if (opcode_check(opcode, arg1, arg2) < 0)
                return -1;

        assemble_add_instr(a, opcode, arg1, arg2);
        return 0;
}

static int
parse_rodata(struct assemble_t *a, struct token_t *toks, int ntok)
{
        struct token_t *start, *stop;
        int negative;
        int ret;
        if (ntok < 3)
                return -1;

        /*
         * take care of the most common cases first:
         * function or single-token expression
         */
        if (ntok == 5 && toks[2].t == OC_LT &&
            toks[3].t == OC_INTEGER && toks[4].t == OC_GT) {
                /* rodata points at another function */
                long long id = intvar_toll(toks[3].v);
                Object *idobj = idvar_new(id);
                assemble_seek_rodata(a, idobj);
                VAR_DECR_REF(idobj);
                return 0;
        } else if (ntok == 3 && toks[2].v != NULL
                   && toks[2].t != OC_IDENTIFIER) {
                /* most common case */
                assemble_seek_rodata(a, toks[2].v);
                return 0;
        }

        /* -number, +number, [+/-]real[+/-]imag */
        stop = &toks[ntok];
        start = &toks[2];
        negative = 0;
        if (start->t == OC_PLUS || start->t == OC_MINUS) {
                if (start->t == OC_MINUS)
                        negative = 1;
                start++;
                if (start >= stop)
                        return -1;
        }
        if (start->t != OC_INTEGER && start->t != OC_FLOAT)
                return -1;

        Object *left, *right, *result;

        left = start->v;

        if (negative) {
                left = qop_negate(left);
                bug_on(!left || left == ErrorVar);
        } else {
                VAR_INCR_REF(left);
        }

        /* from here, need to unwind before returning */
        ret = -1;

        start++;
        if (start >= stop) {
                /* '+' or '-' followed by a real number */
                assemble_seek_rodata(a, left);
                ret = 0;
                goto out_decr_left;
        }

        /* we must have a complex number */
        negative = 0;
        if (start->t == OC_PLUS || start->t == OC_MINUS) {
                if (start->t == OC_MINUS)
                        negative = 1;
                start++;
                if (start == stop)
                        goto out_decr_left;
        } else {
                goto out_decr_left;
        }

        if (start->t != OC_COMPLEX)
                goto out_decr_left;
        right = start->v;
        if (negative) {
                right = qop_negate(right);
                bug_on(!right || right == ErrorVar);
        } else {
                VAR_INCR_REF(right);
        }

        result = qop_add(left, right);
        bug_on(!result || result == ErrorVar);

        assemble_seek_rodata(a, result);

        VAR_DECR_REF(result);
        VAR_DECR_REF(right);
out_decr_left:
        VAR_DECR_REF(left);
        return ret;
}



struct xptrvar_t *
reassemble(struct assemble_t *a)
{
        struct token_t toks[MAX_TOKS_PER_LINE];
        Object *dict = NULL;
        int line, ntok, status;
        int havefunc = 0;

        /* XXX policy dirt. assemble() uses this but it hasn't set it yet */
        if ((status = setjmp(a->env)) != 0) {
                if (!err_occurred()) {
                        err_setstr(SyntaxError,
                                   "Assembler returned error %d",
                                   status);
                }
                return NULL;
        }

        ntok = lex_line(a, toks, &line);

        /* this is the reason we were called! */
        bug_on(ntok == 0);
        bug_on(toks[0].t != OC_PER);

        if (ntok < 0)
                return NULL;

        /* first line, expect three tokens: '.' + 'evilcandy' + "version" */
        if (ntok != 3) {
                err_setstr(SyntaxError,
                           "Malformed first line %d of disassembly",
                           line);
                return NULL;
        }
        if (toks[1].t != OC_IDENTIFIER || strcmp(toks[1].s, "evilcandy"))
                goto efirstline;

        if (toks[2].t != OC_STRING)
                goto efirstline;

        char *version = string_get_cstring(toks[2].v);
        if (strcmp(version, VERSION) != 0) {
                err_setstr(SyntaxError,
                           "Refusing to compile: version '%s' does not match our version '%s'",
                           version, VERSION);
                return NULL;
        }
        /* end of code that can return without any cleanup */

        for (;;) {
                ntok = lex_line(a, toks, &line);
                if (ntok <= 0) {
                        if (ntok == 0) {
                                err_setstr(SyntaxError,
                                           "Empty assembly with no code to execute");
                        }
                        /* else, message sent already */
                        goto bad_out;
                }

                if (toks[0].t == OC_PER) {
                        char *dir;
                        if (ntok < 2 || toks[1].t != OC_IDENTIFIER)
                                goto ebaddirective;
                        dir = toks[1].s;
                        if (!strcmp(dir, "start"))
                                break;

                        if (!strcmp(dir, "define")) {
                                Object *key;

                                if (ntok != 4 || toks[2].t != OC_IDENTIFIER
                                    || toks[3].t != OC_INTEGER) {
                                        goto ebaddirective;
                                }
                                if (!dict)
                                        dict = dictvar_new();
                                key = stringvar_new(toks[2].s);
                                dict_setitem(dict, key, toks[3].v);
                                VAR_DECR_REF(key);
                        } else {
                                err_setstr(SyntaxError,
                                        "Expected: only .define directives before .start (line %d)",
                                        line);
                                goto bad_out;
                        }
                } else {
                        err_setstr(SyntaxError,
                                   "Non-directive before .start near line %d",
                                   line);
                        goto bad_out;
                }
        }

get_function:
        /* if here, we already know line begins with '.' and 'start' */
        if (ntok != 5 || toks[2].t != OC_LT ||
            toks[3].t != OC_INTEGER || toks[4].t != OC_GT) {
                err_setstr(SyntaxError,
                           "Malformed .start directive near line %d",
                           line);
                goto bad_out;
        }

        /*
         * Don't do this for entry point, new_assembler() already did
         * that.  We're losing the function number here, but that's all
         * right because no one references the entry point.
         */
        if (havefunc)
                assemble_frame_push(a, intvar_toll(toks[3].v));
        havefunc = 1;

        for (;;) {
                ntok = lex_line(a, toks, &line);
                if (ntok <= 0) {
                        err_setstr(SyntaxError,
                                "End of input before .end directive");
                        goto bad_out;
                }
                if (toks[0].t == OC_PER) {
                        if (ntok < 2 || toks[1].t != OC_IDENTIFIER)
                                goto ebaddirective;
                        if (!strcmp(toks[1].s, "rodata")) {
                                if (parse_rodata(a, toks, ntok) < 0) {
                                        err_setstr(SyntaxError,
                                                "Malformed rodata line %d",
                                                line);
                                        goto bad_out;
                                }
                        } else if (!strcmp(toks[1].s, "end")) {
                                if (ntok != 2)
                                        goto ebaddirective;
                                break;
                        }
                } else if (ntok == 2 && toks[0].t == OC_INTEGER
                           && toks[1].t == OC_COLON) {
                        assemble_label_here(a);
                } else if (ntok >= 4 && toks[0].t == OC_IDENTIFIER &&
                           toks[2].t == OC_COMMA) {
                        if (parse_opcode(a, toks, ntok, dict) < 0)
                                goto ebadopcodes;
                } else {
                        err_setstr(SyntaxError, "Malformed line %d", line);
                        goto bad_out;
                }
        }

        assemble_frame_pop(a);

        ntok = lex_line(a, toks, &line);
        if (ntok > 0) {
                if (ntok > 1 && toks[0].t == OC_PER &&
                    toks[1].t == OC_IDENTIFIER &&
                    !strcmp(toks[1].s, "start")) {
                        goto get_function;
                }
                /* anything else, malformed */
                err_setstr(SyntaxError,
                           "Invalid line %d between functions", line);
                goto bad_out;
        }

        if (dict)
                VAR_DECR_REF(dict);

        /*
         * Unlike assembler.c, we did not recurse, so entry level
         * is *last* on the list, not first.
         */
        return assemble_frame_to_xptr(a,
                        list2frame(a->finished_frames.prev), false);


ebadopcodes:
        err_setstr(SyntaxError, "Malformed opcode near line %d", line);
        goto bad_out;

ebaddirective:
        err_setstr(SyntaxError, "Invalid directive at line %d", line);
        goto bad_out;

efirstline:
        err_setstr(SyntaxError, "Expected: .evilcandy \"VERSION\"");

bad_out:
        if (dict)
                VAR_DECR_REF(dict);
        return NULL;
}

