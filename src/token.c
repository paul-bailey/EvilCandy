/* token.c - Tokenizer code */
#include <evilcandy.h>
#include "token.h"
#include <setjmp.h>
#include <unistd.h> /* isatty */

/* Token errors, args to longjmp(state->env) */
enum {
        TE_UNTERM_QUOTE = 1, /* may not be zero */
        TE_UNTERM_COMMENT,
        TE_INVALID_CHARS,
        TE_TOO_BIG,
        TE_MALFORMED,
        TE_UNRECOGNIZED,
        TE_FMT_1BRACE,
        TE_FSTR_EMPTY,
};

#define token_errset(state_, err_) longjmp((state_)->env, err_)

/**
 * struct token_state_t - Keep track of all the tokenize calls
 *                        for a given input stream.
 * @lineno:     Current line number in file.
 * @dedup:      Dictionary designed to eliminate duplicate strings,
 *              which are everywhere in a source file.
 * @tok:        Last parsed token, not literal()-ized yet.  Library use
 *              only.
 * @s:          Current pointer into @line, where to look for next token
 * @_slen:      Length of line buffer, for egetline calls
 * @line:       line buffer, for egetline calls
 * @fp:         File we're getting input from
 * @filename:   name of @fp
 * @pgm:        Buffer struct containing array of parsed tokens
 * @ntok:       Number of tokens in @pgm
 * @nexttok:    Next token in @pgm to get with get_tok()
 * @eof:        True if @fp has reached EOF
 * @fstring:    single- or double-quote char if tokenizer is in the middle
 *              of an F-string, nullchar otherwise.
 * @env:        Jump buffer, USE ONLY IN THE tokenize_helper() CONTEXT!
 *
 * Although using pointers for @ntok and @nexttok would make [un]get_tok
 * slightly faster, @pgm's buffer could get realloc'd, so for safety's
 * sake they're indices instead.
 */
struct token_state_t {
        int lineno;
        int col;
        Object *dedup;
        struct buffer_t fstring_tok;
        struct buffer_t tok;
        char *s;
        size_t _slen;
        char *line;
        FILE *fp;
        char *filename;
        struct buffer_t pgm;
        int ntok;
        int nexttok;
        bool eof;
        char fstring;
        size_t fstring_pos;
        jmp_buf env;
        const char *prompt;
};

static void token_state_free_(struct token_state_t *state, bool free_self);

static const char *EVILCANDY_PS1 = "evc> ";
static const char *EVILCANDY_PS2 = " ... ";

/* may be in identifier */
static inline bool
tokc_isident(int c)
{
        return (unsigned)c < 128 && (c == '_' || isalnum(c));
}

/* may be 1st char of identifier */
static inline bool
tokc_isident1(int c)
{
        return (unsigned)c < 128 && (c == '_' || isalpha(c));
}

static int
tok_next_line(struct token_state_t *state)
{
        int res = -1;

        if (state->prompt) {
                fprintf(stderr, "%s", state->prompt);
                fflush(stderr);
                state->prompt = EVILCANDY_PS2;
        }

        if (state->fp)
                res = egetline(&state->line, &state->_slen, state->fp);

        if (res != -1) {
                state->s = state->line;
                state->lineno++;
        } else {
                state->s = NULL;
        }
        return res;
}

static int
str_slide(struct token_state_t *state, struct buffer_t *tok,
          char *pc, const char *charset)
{
        int c, clast = 0;
        bool fstring = tok == &state->fstring_tok;

again:
        while (!strchr(charset, c = *pc++)) {
                /* XXX: portable behavior of strchr? */
                bug_on(c == '\0');
                buffer_putc(tok, c);

                /* make sure we don't misinterpret special chars */
                if (clast != '\\' && c == '\\' &&
                    strchr_nonnull(charset, *pc)) {
                        buffer_putc(tok, *pc);
                        c = *pc++;
                }
                clast = c;
        }

        if (c == '\0')
                token_errset(state, TE_UNTERM_QUOTE);

        if (c == '{') {
                bug_on(!fstring);
                if (*pc == '{') {
                        buffer_putc(tok, c);
                        buffer_putc(tok, c);
                        pc++;
                        goto again;
                }
                if (*pc == '}')
                        token_errset(state, TE_FSTR_EMPTY);
        }

        buffer_putc(tok, c);
        state->s = pc;
        return c;
}

static bool
str_or_bytes_finish(struct token_state_t *state, char *pc, int q)
{
        char charset[2];
        charset[0] = q;
        charset[1] = '\0';

        str_slide(state, &state->tok, pc, charset);
        return true;
}

static int
fstring_continue(struct token_state_t *state)
{
        char charset[3];
        charset[0] = state->fstring;
        charset[1] = '{';
        charset[2] = '\0';
        bug_on(!charset[0]);

        if (*state->s == ':') {
                while (*state->s != '}') {
                        buffer_putc(&state->fstring_tok, *state->s);
                        state->s++;
                }
        }

        return str_slide(state, &state->fstring_tok, state->s, charset);
}

/*
 * Get string literal, or return false if token is something different.
 * state->s points at first quote
 */
static bool
get_tok_string(struct token_state_t *state)
{
        struct buffer_t *tok = &state->tok;
        int q = *state->s;
        if (!isquote(q))
                return false;

        buffer_putc(tok, q);
        return str_or_bytes_finish(state, state->s + 1, q);
}

/*
 * Bytes version of string literal. Processed the same but check for
 * b before the quote.  Type handler will error-check the values in
 * the string.
 */
static bool
get_tok_bytes(struct token_state_t *state)
{
        struct buffer_t *tok = &state->tok;
        char *pc = state->s;
        int q, b;

        b = *pc++;
        if (b != 'b' && b != 'B')
                return false;

        q = *pc++;
        if (!isquote(q))
                return false;

        buffer_putc(tok, b);
        buffer_putc(tok, q);
        return str_or_bytes_finish(state, pc, q);
}

static int
get_tok_fstring(struct token_state_t *state)
{
        struct buffer_t *tok = &state->fstring_tok;
        char *pc = state->s;
        int q, f;

        /* Don't even try to nest f-strings */
        if (state->fstring)
                return 0;

        f = *pc++;
        if (f != 'f' && f != 'F')
                return 0;
        q = *pc++;
        if (!isquote(q))
                return 0;

        state->s = pc;

        buffer_reset(tok);
        buffer_putc(tok, q);
        state->fstring = q;
        if (fstring_continue(state) == q) {
                /*
                 * f-string without any conversion specifiers.
                 * Do the {{ -> { stuff here, since FORMAT instruction
                 * will not be used.
                 */
                const char *src = tok->s;
                int c;
                while ((c = *src++) != '\0') {
                        if (c == '{' || c == '}') {
                                int clast = c;
                                c = *src++;
                                if (c != clast)
                                        token_errset(state, TE_FMT_1BRACE);
                        }
                        buffer_putc(&state->tok, c);
                }
                state->fstring = '\0';
                return OC_STRING;
        }

        return OC_FSTRING_START;
}

static bool
skip_comment(struct token_state_t *state)
{
        char *pc = state->s;
        if (*pc == '#')
                goto oneline;

        if (*pc++ != '/')
                return false;

        if (*pc == '/')
                goto oneline;

        if (*pc == '*') {
                /* block comment */
                do {
                        ++pc;
                        if (*pc == '\0') {
                                if (tok_next_line(state) == -1)
                                        token_errset(state, TE_UNTERM_COMMENT);
                                pc = state->s;
                        }
                } while (!(pc[0] == '*' && pc[1] == '/'));
                state->s = pc + 2;
                return true;
        }
        return false;

oneline:
        /* single-line comment */
        while (*pc != '\n' && *pc != '\0')
                ++pc;
        state->s = pc;
        return true;
}

/*
 * Get identifier token, or return false if token is something different
 */
static bool
get_tok_identifier(struct token_state_t *state)
{
        struct buffer_t *tok = &state->tok;
        char *pc = state->s;
        if (!tokc_isident1(*pc))
                return false;
        while (tokc_isident(*pc))
                buffer_putc(tok, *pc++);
        state->s = pc;
        return true;
}

/* parse hex/binary int if token begins with '0x' or '0b' */
static bool
get_tok_int_hdr(struct token_state_t *state)
{
        struct buffer_t *tok = &state->tok;
        int count = 0;
        char *pc = state->s;

        if (pc[0] != '0')
                return false;

        switch (pc[1]) {
        case 'x':
        case 'X':
                buffer_putc(tok, *pc++);
                buffer_putc(tok, *pc++);
                if (!isxdigit(*pc))
                        goto e_malformed;
                while (isxdigit((int)(*pc))) {
                        if (count++ >= 16)
                                goto e_toobig;
                        buffer_putc(tok, *pc++);
                }
                break;

        case 'b':
        case 'B':
                buffer_putc(tok, *pc++);
                buffer_putc(tok, *pc++);
                if (*pc != '0' && *pc != '1')
                        goto e_malformed;
                while (*pc == '0' || *pc == '1') {
                        if (count++ >= 64)
                                goto e_toobig;
                        buffer_putc(tok, *pc++);
                }
                break;

        case 'o':
        case 'O':
                buffer_putc(tok, *pc++);
                buffer_putc(tok, *pc++);
                if (!isodigit(*pc))
                        goto e_malformed;
                while (isodigit(*pc)) {
                        /*
                         * XXX still could overflow.
                         * should be 21 & last digit <= '1'
                         */
                        if (count++ >= 22)
                                goto e_toobig;
                        buffer_putc(tok, *pc++);
                }
                break;

        default:
                /* decimal int or float */
                return false;
        }


        /* TODO: Support things like 0x1j */

        state->s = pc;
        return true;

e_toobig:
        token_errset(state, TE_TOO_BIG);
e_malformed:
        token_errset(state, TE_MALFORMED);
        return false;
}

/*
 * Get and interpret number literal.
 *
 * Return: OC_INTEGER, OC_FLOAT, OC_COMPLEX, or 0 if not a numerical
 * token.
 */
static int
get_tok_number(struct token_state_t *state)
{
        char *pc, *start;
        int ret, may_be_int;

        /*
         * Do not include sign with number, instead let assembler
         * perform unary-prefix operation on it.  Rationale: If we
         * include the sign here just to save load time, '1 - 2'
         * would be three tokens ['1', '-', '2'], while '1-2' whould
         * be two tokens ['1', '-2'], which would cause a syntax error
         * in the assembler's eval parser.
         */
        if (state->s[0] == '-' || state->s[0] == '+')
                return 0;

        if (get_tok_int_hdr(state))
                return OC_INTEGER;

        start = state->s;

        pc = strtod_scanonly(start, &may_be_int);
        if (!pc)
                return 0;

        if (*pc == 'j' || *pc == 'J') {
                ret = OC_COMPLEX;
                pc++;
                /*
                 * Can't tell if 'j' is end of number or start of
                 * next token?
                 */
                if (tokc_isident(*pc))
                        goto malformed;
        } else if (may_be_int) {
                ret = OC_INTEGER;
        } else {
                ret = OC_FLOAT;
                if (tokc_isident(*pc))
                        goto malformed;
        }

        bug_on(pc == start);
        while (start < pc)
                buffer_putc(&state->tok, *start++);
        state->s = pc;
        return ret;

malformed:
        token_errset(state, TE_MALFORMED);
        return 0;
}

/*
 * Get delimiter token, or return false if token is not a delimiter
 */
static bool
get_tok_delim(int *ret, struct token_state_t *state)
{
        int count = token_delim_seek__(state->s, ret);
        if (count) {
                char *s = state->s;
                state->s += count;
                while (s < state->s) {
                        buffer_putc(&state->tok, *s);
                        s++;
                }
                return true;
        }
        return false;
}

/* return EOF if only whitespace to end of file, 0 otherwise */
static int
skip_whitespace(struct token_state_t *state)
{
        do {
                char *s;
                do {
                        /*
                         * Careful!  This looks like a spinlock bug,
                         * but it isn't.  tok_next_line() below updates
                         * state->s, both the pointer _and_ the buffer's
                         * contents.  So we aren't repeating the same
                         * thing when the do loop reiterates.
                         */
                        s = state->s;
                        while (*s != '\0' && isspace((int)(*s)))
                                ++s;
                } while (*s == '\0' && tok_next_line(state) != -1);
                state->s = s;
                if (*s == '\0')
                        return OC_EOF;
        } while (skip_comment(state));
        state->col = state->s - state->line;
        return 0;
}

/*
 * Return: OC_xxx enum (always positive) or RES_ERROR if unparseable
 *         token.
 */
static int
tokenize_helper(struct token_state_t *state, int *line)
{
        /*
         * XXX: setjmp for every token?
         * Too much overhead?
         * Better to be more fussy with return values?
         */
        int ret;

        if ((ret = setjmp(state->env)) != 0) {
                const char *msg;
                switch (ret) {
                case TE_UNTERM_QUOTE:
                        msg = "Unterminated quote";
                        break;
                case TE_UNTERM_COMMENT:
                        msg = "Unterminated comment";
                        break;
                case TE_INVALID_CHARS:
                        msg = "Invalid chars in identifier or keyword";
                        break;
                case TE_TOO_BIG:
                        msg = "Integer too large";
                        break;
                case TE_MALFORMED:
                        msg = "Malformed numerical expression";
                        break;
                case TE_UNRECOGNIZED:
                        msg = "Unrecognized token";
                        break;
                case TE_FMT_1BRACE:
                        msg = "Single '{' or '}' not allowed";
                        break;
                case TE_FSTR_EMPTY:
                        msg = "F-strings may not take empty expressions";
                        break;
                default:
                        msg = "Token parsing error";
                        break;
                }

                err_setstr(SyntaxError, msg);
        } else {
                struct buffer_t *tok = &state->tok;

                buffer_reset(tok);

                /* repurpose ret to be a token-type result */
                if ((ret = skip_whitespace(state)) == OC_EOF)
                        return ret;

                if (state->fstring) {
                        if (state->s[0] == '\\') {
                                /*
                                 * We're in an expression in an f-string,
                                 * but some characters need to be backslash-
                                 * escaped.  Escape them here and fall
                                 * through to regular-token processing
                                 * below.
                                 */
                                int c = state->s[1];
                                if (!!strchr_nonnull(":{}", c)) {
                                        state->s++;
                                } else {
                                        token_errset(state, TE_UNRECOGNIZED);
                                }
                        } else if (state->s[0] == '}' || state->s[0] == ':') {
                                if (fstring_continue(state) == state->fstring) {
                                        buffer_puts(tok, state->fstring_tok.s);
                                        state->fstring = '\0';
                                        return OC_FSTRING_END;
                                }
                                return OC_FSTRING_CONTINUE;
                        }
                }

                /* get number before delim, '.' could not be delim */
                if ((ret = get_tok_number(state)) != 0) {
                        return ret;
                } else if (get_tok_delim(&ret, state)) {
                        return ret;
                } else if ((ret = get_tok_fstring(state)) != 0) {
                        /*
                         * FIXME: support multi-line concatenation of
                         * adjacent f-string tokens.
                         */
                        return ret;
                } else if (get_tok_string(state)) {
                        *line = state->lineno;
                        /*
                         * this allows for strings expressed like
                         *      "..." "..."
                         * to be parsed as single concatenated literals.
                         */
                        do {
                                ret = skip_whitespace(state);
                        } while (ret != OC_EOF && get_tok_string(state));
                        return OC_STRING;
                } else if (get_tok_bytes(state)) {
                        *line = state->lineno;
                        do {
                                ret = skip_whitespace(state);
                        } while (ret != OC_EOF && get_tok_bytes(state));
                        return OC_BYTES;
                } else if (get_tok_identifier(state)) {
                        if ((ret = token_kw_seek__(tok->s)) >= 0)
                                return ret;
                        return OC_IDENTIFIER;
                }
                token_errset(state, TE_UNRECOGNIZED);
        }

        /* If we're here, some error happened */
        return RES_ERROR;
}

static int
bad_literal(struct token_state_t *state, const char *what)
{
        err_setstr(SyntaxError, "Error in %s literal '%s'",
                   what, state->tok.s);
        return RES_ERROR;
}

/* Get the next token from the current input file. */
static int
tokenize(struct token_state_t *state)
{
        int ret;
        /* only set if bytes or string */
        int line = -1;

        ret = tokenize_helper(state, &line);
        if (ret == RES_ERROR) {
                return ret;
        } else if (ret == OC_EOF) {
                static const struct token_t eofoc = {
                        .t = OC_EOF,
                        .line = 0,
                        .s = NULL,
                        .v = NULL,
                };
                state->eof = true;
                buffer_putd(&state->pgm, &eofoc, sizeof(eofoc));
        } else {
                struct token_t oc;
                bool intern = false;

                oc.t = ret;
                oc.line = state->lineno;

                switch (ret) {
                case OC_NULL:
                        oc.v = VAR_NEW_REF(NullVar);
                        intern = true;
                        break;
                case OC_TRUE:
                        oc.v = VAR_NEW_REF(gbl.one);
                        intern = true;
                        break;
                case OC_FALSE:
                        oc.v = VAR_NEW_REF(gbl.zero);
                        intern = true;
                        break;
                case OC_BYTES:
                        oc.line = line;
                        oc.v = bytesvar_from_source(state->tok.s);
                        if (oc.v == ErrorVar) {
                                ret = bad_literal(state, "bytes");
                                oc.v = NULL;
                        }
                        break;
                case OC_INTEGER:
                    {
                        long long i;
                        if (evc_strtol(state->tok.s, NULL, 0, &i) == RES_ERROR) {
                                ret = bad_literal(state, "integer");
                        } else {
                                oc.v = intvar_new(i);
                        }
                        break;
                    }
                case OC_FLOAT:
                    {
                        double f;
                        if (evc_strtod(state->tok.s, NULL, &f) == RES_ERROR) {
                                ret = bad_literal(state, "double");
                        } else {
                                oc.v = floatvar_new(f);
                        }
                        break;
                    }
                case OC_COMPLEX:
                    {
                        double im;
                        if (evc_strtod(state->tok.s, NULL, &im) == RES_ERROR) {
                                ret = bad_literal(state, "double");
                        } else {
                                oc.v = complexvar_new(0.0, im);
                        }
                        break;
                    }
                case OC_IDENTIFIER:
                        /* FIXME: will need to de-dup this */
                        oc.v = stringvar_new(state->tok.s);
                        break;
                case OC_STRING:
                case OC_FSTRING_END:
                        oc.line = line;
                        oc.v = stringvar_from_source(state->tok.s, true);
                        if (oc.v == ErrorVar) {
                                ret = bad_literal(state, "string");
                                oc.v = NULL;
                        }
                        break;
                default:
                        intern = true;
                        oc.v = NULL;
                }

                intern = intern && !!state->dedup;

                if (intern) {
                        oc.s = dict_unique(state->dedup, state->tok.s);
                } else {
                        oc.s = estrdup(state->tok.s);
                }

                buffer_putd(&state->pgm, &oc, sizeof(oc));
        }
        state->ntok++;
        return ret;
}

static void
token_init_state(struct token_state_t *state, FILE *fp, const char *filename)
{
        buffer_init(&state->tok);
        buffer_init(&state->fstring_tok);
        state->line     = NULL;
        state->_slen    = 0;
        state->s        = NULL;
        state->filename = filename ? estrdup(filename) : NULL;
        state->fp       = fp;
        state->lineno   = 0;

        buffer_init(&state->pgm);
        state->ntok     = 0;
        state->nexttok  = 0;
        state->eof      = false;
        state->fstring  = false;
        state->fstring_pos = 0;

        /*
         * if not file, we're just parsing a line, so interning
         * is more overhead than it's worth
         */
        state->dedup = fp ? dictvar_new() : NULL;
        if (fp && isatty(fileno(fp)))
                state->prompt = EVILCANDY_PS1;
        else
                state->prompt = NULL;
}

#define TOKBUF(state_) ((struct token_t *)(state_)->pgm.s)

int
get_tok_from_cstring(const char *s, char **endptr, struct token_t *dst)
{
        struct token_state_t state;
        int ret;

        token_init_state(&state, NULL, NULL);
        state.line = (char *)s;
        state.s = state.line;

        ret = tokenize(&state);
        if (ret >= 0) {
                memcpy(dst, TOKBUF(&state), sizeof(*dst));
                /* cleaning state below would consume this */
                if (dst->v)
                        VAR_INCR_REF(dst->v);
                if (dst->s)
                        dst->s = NULL;
                if (endptr)
                        *endptr = state.s;
        }

        /* don't let token_state_free touch this */
        state.line = state.s = NULL;
        token_state_free_(&state, false);
        return ret;
}

/**
 * get_tok - Get the next token
 * @state:      Token state machine
 * @tok:        (output) pointer to the next token
 *
 * Return: Type of token stored in @tok.  If RES_ERROR, then @tok was
 * not updated and an error message was printed to stderr.
 *
 * WARNING!! @tok will point into an array of struct token_t's, which
 * could be moved if a later get_tok() call triggers a realloc.  Do not
 * dereference the old pointer after the next call to get_tok().  If
 * the earlier @tok's contents are still needed, first declare another
 * struct token_t (these aren't big) and memcpy @tok's contents into it
 * before calling get_tok() again.  This should be safe; even if a later
 * get_tok() moves the array again, the CONTENTS of the old @tok,
 * including its .s pointer, have already been finalized by the time
 * calling code ever sees it,
 *
 * XXX REVISIT: Should @tok just be a single pointer for us to memcpy
 * into it?  Saves a lot of policy.  They're only 24 measly bytes.
 */
int
get_tok(struct token_state_t *state, struct token_t **tok)
{
        struct token_t *cur;

        bug_on(tok == NULL);

        /*
         * If next token has not yet been parsed, parse it.
         * Otherwise return the next parsed token.
         */
        if (state->nexttok >= state->ntok) {
                if (state->eof) {
                        /*
                         * At EOF and "need" new token.
                         * Keep returning EOF token
                         */
                        bug_on(state->ntok <= 0);
                        cur = TOKBUF(state) + state->ntok - 1;
                        bug_on(cur->t != OC_EOF);
                        goto done;
                }
                if (tokenize(state) == RES_ERROR)
                        return RES_ERROR;
                bug_on(state->nexttok >= state->ntok);
        }

        /* tokenize() may have changed state->nexttok, so refresh this */
        cur = TOKBUF(state) + state->nexttok;
        state->nexttok++;

done:
        *tok = cur;
        bug_on(cur->t == 0 || (cur->t >= OC_NTOK));
        return cur->t;
}

/**
 * unget_tok - Back up the token state to previous token
 * @state:      Token state machine
 * @tok:        Variable storing pointer to new "current" token.
 *
 *      WARNING!! @tok may be an invalid pointer if number of unget_tok()
 *      calls match or outnumber get_tok() calls.  Calling code must keep
 *      track of this, and be sure not to de-reference @tok when the
 *      imbalance was intentional, eg. when un-peeking at the first token
 *      of the input stream.
 */
void
unget_tok(struct token_state_t *state, struct token_t **tok)
{
        bug_on(state->nexttok <= 0);
        state->nexttok--;
        *tok = TOKBUF(state) + state->nexttok - 1;
}

token_pos_t
token_swap_pos(struct token_state_t *state, token_pos_t pos)
{
        token_pos_t ret = state->nexttok;
        state->nexttok = pos;
        return ret;
}

token_pos_t
token_get_pos(struct token_state_t *state)
{
        return (token_pos_t)state->nexttok;
}

static bool
isintern(struct token_state_t *state, struct token_t *tok)
{
        if (!state->dedup)
                return false;
        if (!tok->v)
                return true;
        return tok->t == OC_NULL ||
               tok->t == OC_TRUE ||
               tok->t == OC_FALSE;
}

/**
 * token_state_free - Destructor for a token state machine
 * @state: A return value of token_state_new()
 *
 * Called when finished parsing a full file.
 */
static void
token_state_free_(struct token_state_t *state, bool free_self)
{
        struct token_t *tok, *endtok;
        int n;

        buffer_free(&state->tok);
        buffer_free(&state->fstring_tok);
        if (state->line)
                efree(state->line);
        n = buffer_size(&state->pgm) / sizeof(struct token_t);
        bug_on(n != state->ntok);
        tok = TOKBUF(state);
        endtok = &tok[n];
        while (tok < endtok) {
                if (tok->v)
                        VAR_DECR_REF(tok->v);
                if (!isintern(state, tok)) {
                        bug_on(!tok->s);
                        efree(tok->s);
                }
                tok++;
        }
        buffer_free(&state->pgm);

        if (state->dedup)
                VAR_DECR_REF(state->dedup);
        if (state->filename)
                efree(state->filename);
        if (free_self)
                efree(state);
}

void
token_state_free(struct token_state_t *state)
{
        token_state_free_(state, true);
}

/**
 * token_state_new - Get a new token state machine
 * @fp: Open file to parse
 * @filename: Name of @fp, used for printing syntax errors
 *      This may not be NULL.  Name it something like
 *      "(stdin)" or "(pipe)" if not a regular script file.
 *
 * Return: New token state machine.
 */
struct token_state_t *
token_state_new(FILE *fp, const char *filename)
{
        struct token_state_t *state = emalloc(sizeof(*state));

        token_init_state(state, fp, filename);

        /*
         * Get first line, so that the
         * above functions don't all have to start
         * with "if (state->s == NULL)"
         */
        if (tok_next_line(state) == -1) {
                token_state_free(state);
                return NULL;
        }

        return state;
}

/**
 * token_get_this_line - get currently-being-parsed line
 * @col: Variable to hold column of last token parsed or partially parsed.
 *       This may not be NULL.
 *
 *      BEWARE!!  This pointer may be invalidated on next call
 *      to get_tok, so don't save the result.  Either copy it
 *      or whatever you're doing with it do it now.
 */
char *
token_get_this_line(struct token_state_t *state, int *col)
{
        *col = state->col;
        return state->line;
}

