/* token.c - Tokenizer code */
#include <evilcandy.h>
#include <evilcandy/token.h>
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
 * @pgm:        Buffer struct containing array of parsed tokens
 * @ntok:       Number of tokens in @pgm
 * @nexttok:    Next token in @pgm to get with get_tok()
 * @eof:        True if @fp has reached EOF
 * @inp:        Type of input, file, tty, or string
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
        Object *dedup;
        struct buffer_t fstring_tok;
        struct buffer_t tok;
        char *s;
        size_t _slen;
        char *line;
        FILE *fp;
        struct buffer_t pgm;
        int ntok;
        int nexttok;
        bool eof;
        enum {
                TKINP_FILE = 0,
                TKINP_TTY,
                TKINP_STRING,
        } inp;
        char fstring;
        size_t fstring_pos;
        jmp_buf env;
        const char *prompt;
        unsigned int start_col;
        unsigned int start_line;
};

#define TOKBUF_WIDTH_  sizeof(void *)
#define TOKBUF(state_) ((struct token_t **)(state_)->pgm.s)
#define TOKBUF_SIZE(state_) \
        (buffer_size(&(state_)->pgm) / TOKBUF_WIDTH_)
#define TOKBUF_PUT(state_, tokp_) \
do { \
        buffer_putd(&(state_)->pgm, &(tokp_), TOKBUF_WIDTH_); \
} while (0)

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

        if (!state->fp)
                return res;

        switch (state->inp) {
        case TKINP_TTY:
                bug_on(!state->fp);
                if (gbl.iatok.line) {
                        /* XXX: bug if state->line != NULL here? */
                        if (state->line)
                                efree(state->line);
                        state->line   = gbl.iatok.line;
                        state->s      = gbl.iatok.s;
                        state->lineno = gbl.iatok.lineno;
                        state->_slen  = gbl.iatok._slen;
                        memset(&gbl.iatok, 0, sizeof(gbl.iatok));
                        /*
                         * Don't fall through, we don't want to reset
                         * state->s
                         *
                         * XXX overkill result? everyone's only checking
                         * if <0, not exact size.
                         */
                        return strlen(state->line) - (state->s - state->line);
                }
                res = myreadline(&state->line, &state->_slen,
                                 state->fp, state->prompt);
                state->prompt = EVILCANDY_PS2;
                break;
        case TKINP_FILE:
                res = egetline(&state->line, &state->_slen, state->fp);
                break;
        case TKINP_STRING:
                res = -1;
                break;
        default:
                bug();
        }

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

        if (c == '\0') {
                if (tok_next_line(state) < 0)
                        token_errset(state, TE_UNTERM_QUOTE);
                pc = state->s;
                goto again;
        }

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
         * would be three tokens ['1', '-', '2'], while '1-2' would
         * be two tokens ['1', '-2'], which would cause a syntax error
         * in the assembler's eval parser.
         *
         * XXX: This permits scenarios I should probably not allow, e.g.
         * "- - -1" will be treated as a valid expression for negative one.
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
                        if (!state->line || !state->s)
                                return OC_EOF;
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
        return 0;
}

/*
 * Return: OC_xxx enum (always positive) or RES_ERROR if unparseable
 *         token.
 */
static int
tokenize_helper(struct token_state_t *state)
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

                state->start_line = state->lineno;
                state->start_col = (size_t)(state->s - state->line);

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
        int ret = tokenize_helper(state);
        if (ret == RES_ERROR) {
                return ret;
        } else if (ret == OC_EOF) {
                static const struct token_t eofoc = {
                        .t = OC_EOF,
                        .start_line = 0,
                        .stop_line = 0,
                        .start_col = 0,
                        .stop_col = 0,
                        .v = NULL,
                };
                struct token_t *oc = ememdup(&eofoc, sizeof(eofoc));
                TOKBUF_PUT(state, oc);
                state->eof = true;
        } else {
                struct token_t *oc = emalloc(sizeof(*oc));

                oc->t = ret;
                /* state->start_xxx was set in tokenize_helper() */
                oc->start_line = state->start_line;
                oc->stop_line  = state->lineno;
                oc->start_col  = state->start_col;
                oc->stop_col   = (size_t)(state->s - state->line);

                switch (ret) {
                case OC_NULL:
                        oc->v = VAR_NEW_REF(NullVar);
                        break;
                case OC_TRUE:
                        oc->v = VAR_NEW_REF(gbl.one);
                        break;
                case OC_FALSE:
                        oc->v = VAR_NEW_REF(gbl.zero);
                        break;
                case OC_BYTES:
                        oc->v = bytesvar_from_source(state->tok.s);
                        if (oc->v == ErrorVar) {
                                ret = bad_literal(state, "bytes");
                                oc->v = NULL;
                        }
                        break;
                case OC_INTEGER:
                    {
                        long long i;
                        if (evc_strtol(state->tok.s, NULL, 0, &i) == RES_ERROR) {
                                ret = bad_literal(state, "integer");
                        } else {
                                oc->v = intvar_new(i);
                        }
                        break;
                    }
                case OC_FLOAT:
                    {
                        double f;
                        if (evc_strtod(state->tok.s, NULL, &f) == RES_ERROR) {
                                ret = bad_literal(state, "double");
                        } else {
                                oc->v = floatvar_new(f);
                        }
                        break;
                    }
                case OC_COMPLEX:
                    {
                        double im;
                        if (evc_strtod(state->tok.s, NULL, &im) == RES_ERROR) {
                                ret = bad_literal(state, "double");
                        } else {
                                oc->v = complexvar_new(0.0, im);
                        }
                        break;
                    }
                case OC_IDENTIFIER:
                        /* FIXME: will need to de-dup this */
                        oc->v = stringvar_new(state->tok.s);
                        break;
                case OC_STRING:
                case OC_FSTRING_END:
                        oc->v = stringvar_from_source(state->tok.s, true);
                        if (oc->v == ErrorVar) {
                                ret = bad_literal(state, "string");
                                oc->v = NULL;
                        }
                        break;
                default:
                        oc->v = NULL;
                }

                if (state->dedup && oc->v && isvar_string(oc->v)) {
                        enum result_t res;
                        res = set_additem(state->dedup, oc->v, &oc->v);
                        bug_on(res != RES_OK);
                        (void)res;
                }

                TOKBUF_PUT(state, oc);
        }
        state->ntok++;
        return ret;
}

static void
token_init_state(struct token_state_t *state, FILE *fp)
{
        buffer_init(&state->tok);
        buffer_init(&state->fstring_tok);
        state->line     = NULL;
        state->_slen    = 0;
        state->s        = NULL;
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
        state->dedup = fp ? setvar_new(NULL) : NULL;
        if (fp) {
                if (isatty(fileno(fp))) {
                        state->inp = TKINP_TTY;
                        state->prompt = EVILCANDY_PS1;
                } else {
                        state->inp = TKINP_FILE;
                        state->prompt = NULL;
                }
        } else {
                state->inp = TKINP_STRING;
                state->prompt = NULL;
        }
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
        struct token_t **tokbuf = TOKBUF(state);

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
                        cur = tokbuf[state->ntok - 1];
                        bug_on(cur->t != OC_EOF);
                        goto done;
                }
                if (tokenize(state) == RES_ERROR)
                        return RES_ERROR;
                bug_on(state->nexttok >= state->ntok);
                /* need to refresh this */
                tokbuf = TOKBUF(state);
        }

        cur = tokbuf[state->nexttok++];

done:
        *tok = cur;
        bug_on(cur->t == 0 || (cur->t >= OC_NTOK));
        return cur->t;
}

/**
 * get_tok_at - similar to get_tok(), but do not update state position
 * @state: Token state machine
 * @pos:   Position to get token at.  This must be within the size
 *         of the token array.  It should be a former return value
 *         of token_get_pos.
 *
 * Return: Token at @pos
 */
struct token_t *
get_tok_at(struct token_state_t *state, token_pos_t pos)
{
        struct token_t **tokbuf = TOKBUF(state);

        bug_on(pos >= state->ntok);
        bug_on((int)pos < 0);
        return tokbuf[pos];
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
        struct token_t **tokbuf = TOKBUF(state);
        bug_on(state->nexttok <= 0);
        state->nexttok--;
        *tok = tokbuf[state->nexttok - 1];
}

token_pos_t
token_swap_pos(struct token_state_t *state, token_pos_t pos)
{
        bug_on(pos > TOKBUF_SIZE(state));
        token_pos_t ret = state->nexttok;
        state->nexttok = pos;
        return ret;
}

token_pos_t
token_get_pos(struct token_state_t *state)
{
        return (token_pos_t)state->nexttok;
}

static void
token_state_free_line(struct token_state_t *state)
{
        if (state->s) {
                char *s = state->s;
                int c;
                while ((c = *s) != '\0') {
                        if (c > 127 || !isspace(c))
                                break;
                        s++;
                }

                /*
                 * This checks if multiple top-level statements were
                 * typed on the same line, e.g. "let x = 1; let y = 2;"
                 * We had just assembled, etc., the first statement,
                 * but not the second statement.  In interactive mode,
                 * we do not want the second statement to disappear, so
                 * while destroying this token_state_t, save the line
                 * state to gbl.iatok, and retrieve it next
                 * token_next_line().
                 */
                if (state->inp == TKINP_TTY && *s != '\0') {
                        bug_on(gbl.iatok.line != NULL);
                        gbl.iatok.line   = state->line;
                        gbl.iatok.s = gbl.iatok.line + (s-state->line);
                        gbl.iatok.lineno = state->lineno;
                        gbl.iatok._slen  = state->_slen;
                        goto skip_free;
                }
        }

        /*
         * FIXME:  If TTY but we didn't copy to gbl.iatok above,
         * we are not getting line number.
         */
        efree(state->line);

skip_free:
        state->line = NULL;
        state->s = NULL;
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
        struct token_t **tokbuf;
        size_t i, n;

        buffer_free(&state->tok);
        buffer_free(&state->fstring_tok);
        if (state->line && state->inp != TKINP_STRING)
                token_state_free_line(state);
        n = TOKBUF_SIZE(state);
        bug_on(n != state->ntok);
        tokbuf = TOKBUF(state);
        for (i = 0; i < n; i++) {
                if (tokbuf[i]->v)
                        VAR_DECR_REF(tokbuf[i]->v);
                efree(tokbuf[i]);
        }
        buffer_free(&state->pgm);

        if (state->dedup)
                VAR_DECR_REF(state->dedup);
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
 *
 * Return: New token state machine.
 */
struct token_state_t *
token_state_new(FILE *fp)
{
        struct token_state_t *state = emalloc(sizeof(*state));

        token_init_state(state, fp);

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
 * token_state_from_string - Get a new token state machine using
 *                      a C-string as an input instead of a file.
 * @cstring: C-string to use as input.  This will not be copied, so it
 *           must remain in persistent memory until token_state_free().
 *
 * Return: New token state machine.
 */
struct token_state_t *
token_state_from_string(const char *cstring)
{
        struct token_state_t *state = emalloc(sizeof(*state));
        token_init_state(state, NULL);
        /*
         * TODO: More formal to have something like:
         *
         *      state->inp_cstring = cstring;
         *      state->inp_ptr     = cstring;
         *      if (tok_next_line(state) == -1) {
         *      ...
         *
         * where tok_next_line() will copy from state->inp_ptr to next
         * newline into state->line.  The problem with just assigning
         * state->line to cstring is that we can't keep track of line
         * number should any '\n' be embedded in cstring.
         */
        state->line = (char *)cstring;
        state->s = state->line;
        return state;
}

/**
 * token_get_this_line - get currently-being-parsed line
 *
 *      BEWARE!!  This pointer may be invalidated on next call
 *      to get_tok, so don't save the result.  Either copy it
 *      or whatever you're doing with it do it now.
 */
char *
token_get_this_line(struct token_state_t *state)
{
        return state->line;
}

/**
 * token_flush_tty - Flush remainder of line for interactive mode
 * @state:      Tokenizer state machine, or NULL to just flush remainder
 *              of saved TTY line "gbl.iatok"
 *
 * Used to clear the remainder of the line if an error is encountered by
 * the interpreter.  Otherwise, the remainder of this line will be
 * interpreted as the beginning of the next statement.  In the example
 *      'let x = 5 +; 5;'
 * a syntax error will be thrown at the first semicolon, but the ' 5;'
 * will then be parsed in the next pass of the interpreter loop.  Calling
 * this function upon detection of an error prevents that.
 */
void
token_flush_tty(struct token_state_t *state)
{
        if (gbl.iatok.line) {
                efree(gbl.iatok.line);
                memset(&gbl.iatok, 0, sizeof(gbl.iatok));
        }

        if (state && state->line) {
                /* Not an issue in script mode */
                if (state->inp != TKINP_TTY)
                        return;
                state->line[0] = 0;
                state->s = state->line;
        }
}
