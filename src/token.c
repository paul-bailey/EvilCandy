/* token.c - Tokenizer code */
#include <evilcandy.h>
#include "token.h"
#include <setjmp.h>
#include <stdlib.h>

enum {
        QDELIM = 0x01,
        QIDENT = 0x02,
        QIDENT1 = 0x04,
        QDDELIM = 0x08,
};

/* Token errors, args to longjmp(state->env) */
enum {
        TE_UNTERM_QUOTE = 1, /* may not be zero */
        TE_UNTERM_COMMENT,
        TE_INVALID_CHARS,
        TE_TOO_BIG,
        TE_MALFORMED,
        TE_UNRECOGNIZED
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
 * @_slen:      Length of line buffer, for getline calls
 * @line:       line buffer, for getline calls
 * @fp:         File we're getting input from
 * @filename:   name of @fp
 * @pgm:        Buffer struct containing array of parsed tokens
 * @ntok:       Number of tokens in @pgm
 * @nexttok:    Next token in @pgm to get with get_tok()
 * @eof:        True if @fp has reached EOF
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
        jmp_buf env;
};

static void token_state_free_(struct token_state_t *state, bool free_self);

/* a sort of "ctype" for tokens */
static unsigned char tok_charmap[128];

static inline bool tokc_isascii(int c) { return c && c == (c & 0x7fu); }
static inline bool
tokc_isflags(int c, unsigned char flags)
{
        return tokc_isascii(c) && (tok_charmap[c] & flags) == flags;
}

static inline bool tokc_isdelim(int c) { return tokc_isflags(c, QDELIM); }
/* may be in identifier */
static inline bool tokc_isident(int c) { return tokc_isflags(c, QIDENT); }
/* may be 1st char of identifier */
static inline bool tokc_isident1(int c) { return tokc_isflags(c, QIDENT1); }

static int
tok_next_line(struct token_state_t *state)
{
        int res = -1;

        if (state->fp)
                res = getline(&state->line, &state->_slen, state->fp);

        if (res != -1) {
                state->s = state->line;
                state->lineno++;
        } else {
                state->s = NULL;
        }
        return res;
}

static bool
str_or_bytes_finish(struct token_state_t *state, char *pc, int q)
{
        struct buffer_t *tok = &state->tok;
        int c;
        while ((c = *pc++) != q) {
                if (c == '\0')
                        token_errset(state, TE_UNTERM_QUOTE);

                buffer_putc(tok, c);

                /* make sure we don't misinterpret q */
                if (c == '\\' && *pc == q) {
                        buffer_putc(tok, q);
                        pc++;
                }
        }
        buffer_putc(tok, q);
        state->s = pc;
        return true;
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
        if (!tokc_isdelim(*pc))
                token_errset(state, TE_INVALID_CHARS);
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

        default:
                /* decimal int or float */
                return false;
        }


        if (!tokc_isdelim(*pc))
                goto e_malformed;

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
 * Return: 'i' if integer, 'f' if float, 0 if token is not a number.
 */
static int
get_tok_number(struct token_state_t *state)
{
        char *pc, *start;
        int ret;

        if (get_tok_int_hdr(state))
                return OC_INTEGER;

        pc = start = state->s;

        while (isdigit((int)*pc))
                ++pc;

        if (pc == start)
                return 0;

        ret = OC_INTEGER;
        if (*pc == '.' || *pc == 'e' || *pc == 'E') {
                ret = OC_FLOAT;
                if (*pc == '.')
                        ++pc;
                while (isdigit(*pc))
                        ++pc;
                if (*pc == 'e' || *pc == 'E') {
                        char *e = pc;
                        ++pc;
                        if (*pc == '-' || *pc == '+') {
                                ++e;
                                ++pc;
                        }
                        while (isdigit(*pc))
                                ++pc;
                        if (pc == e)
                                goto malformed;
                }
        }

        if (*pc == 'j' || *pc == 'J') {
                ret = OC_COMPLEX;
                pc++;
        }

        /* We don't allow suffixes like f, u, ul, etc. */
        if (!tokc_isdelim(*pc))
                goto malformed;

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
 * returns:
 * 'd' OR'd with ((delim<<8)|flags) if token was a delimiter
 * 'k' OR'd with code<<8 for keyword if token was a keyword
 * 'q' if quoted string.
 * 'b' if bytes
 * 'i' if integer
 * 'f' if float
 * 'u' if identifier
 * EOF if end of file
 *
 * RES_ERROR if bad or unparseable token.  Do not de-reference
 * token data if this happens.
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

                /* get number before delim, '.' could not be delim */
                if ((ret = get_tok_number(state)) != 0) {
                        return ret;
                } else if (get_tok_delim(&ret, state)) {
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
                        VAR_INCR_REF(NullVar);
                        oc.v = NullVar;
                        intern = true;
                        break;
                case OC_TRUE:
                        oc.v = intvar_new(1);
                        intern = true;
                        break;
                case OC_FALSE:
                        oc.v = intvar_new(0);
                        intern = true;
                        break;
                case OC_BYTES:
                        oc.line = line;
                        oc.v = bytesvar_from_source(state->tok.s);
                        if (oc.v == ErrorVar) {
                                err_setstr(SyntaxError,
                                        "Error in bytes literal %s",
                                        state->tok.s);
                                oc.v = NULL;
                                ret = RES_ERROR;
                        }
                        break;
                case OC_INTEGER:
                    {
                        long long i = strtoul(state->tok.s, NULL, 0);
                        oc.v = intvar_new(i);
                        break;
                    }
                case OC_FLOAT:
                    {
                        double f = strtod(state->tok.s, NULL);
                        oc.v = floatvar_new(f);
                        break;
                    }
                case OC_COMPLEX:
                    {
                        double im = strtod(state->tok.s, NULL);
                        oc.v = complexvar_new(0.0, im);
                        break;
                    }
                case OC_IDENTIFIER:
                        /* FIXME: will need to de-dup this */
                        oc.v = stringvar_new(state->tok.s);
                        break;
                case OC_STRING:
                        oc.line = line;
                        oc.v = stringvar_from_source(state->tok.s, true);
                        if (oc.v == ErrorVar) {
                                err_setstr(SyntaxError,
                                        "Error in string literal %s",
                                        state->tok.s);
                                ret = RES_ERROR;
                        }
                        break;
                default:
                        oc.v = NULL;
                }

                if ((oc.v == NULL || intern) && state->dedup) {
                        oc.s = dict_unique(state->dedup, state->tok.s);
                } else {
                        oc.s = estrdup(state->tok.s);
                }

                if (ret == RES_ERROR) {
                        if (oc.v)
                                VAR_DECR_REF(oc.v);
                        return ret;
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

        /*
         * if not file, we're just parsing a line, so interning
         * is more overhead than it's worth
         */
        state->dedup = fp ? dictvar_new() : NULL;
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

/*
 * Use when you still need what's been parsed from @state,
 * but you don't need the current-token bits.
 */
void
token_state_trim(struct token_state_t *state)
{
        buffer_free(&state->tok);
        if (state->line)
                efree(state->line);
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
        struct token_t *tok = (struct token_t *)(state->pgm.s);
        int i, n = buffer_size(&state->pgm);
        if (n > state->ntok)
                n = state->ntok;
        token_state_trim(state);
        for (i = 0; i < n; i++) {
                bool intern = false;
                if (tok[i].v) {
                        int t = tok[i].t;
                        if ((t == OC_NULL ||
                             t == OC_TRUE ||
                             t == OC_FALSE) &&
                            state->dedup) {
                                intern = true;
                        }
                        VAR_DECR_REF(tok[i].v);
                } else {
                        intern = true;
                }

                if (!intern) {
                       bug_on(!tok[i].s);
                       efree(tok[i].s);
                }
        }
        if (state->dedup)
                VAR_DECR_REF(state->dedup);
        buffer_free(&state->pgm);
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

void
moduleinit_token(void)
{
        /*
         * IMPORTANT!! These two strings must be in same order as
         *             their OC_* enums in token.h
         */
        static const char *const DELIMS = "+-<>=&|.!;,/*%^()[]{}:~ \t\n";
        static const char *const DELIMDBL = "+-<>=&|";
        const char *s;
        int i;

        /*
         * Set up tok_charmap
         * XXX: more optimal to put this in a code generator
         * so it's all done at compile time instead.
         */

        /* delimiter */
        for (s = DELIMS; *s != '\0'; s++)
                tok_charmap[(int)*s] |= QDELIM;
        /* double-delimeters */
        for (s = DELIMDBL; *s != '\0'; s++)
                tok_charmap[(int)*s] |= QDDELIM;

        /* special cases */
        tok_charmap[(int)'*'] |= (QDELIM | QDDELIM);
        tok_charmap[(int)'`'] |= (QDELIM | QDDELIM);
        tok_charmap[0] |= QDELIM;

        /* permitted identifier chars */
        for (i = 'a'; i <= 'z'; i++)
                tok_charmap[i] |= QIDENT | QIDENT1;
        for (i = 'A'; i <= 'Z'; i++)
                tok_charmap[i] |= QIDENT | QIDENT1;
        for (i = '0'; i <= '9'; i++)
                tok_charmap[i] |= QIDENT;
        tok_charmap['_'] |= QIDENT | QIDENT1;
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

