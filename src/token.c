/* token.c - Tokenizer code */
#include <evilcandy.h>
#include "token.h"
#include <stdlib.h>
#include <setjmp.h>
#include <unistd.h> /* TODO: for isatty, get rid of this */

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
        TE_HALFLAMBDA,
        TE_UNRECOGNIZED
};

#define token_errset(state_, err_) longjmp((state_)->env, err_)

/**
 * struct token_state_t - Keep track of all the tokenize calls
 *                        for a given input stream.
 * @lineno:     Current line number in file.
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
        bool tty;
        jmp_buf env;
};

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
        int res = getline(&state->line, &state->_slen, state->fp);
        if (res != -1) {
                state->s = state->line;
                state->lineno++;
                /*
                 * TODO: if state->tty, here's where to print
                 * continuation prompt
                 */
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
                return 'i';

        pc = start = state->s;

        while (isdigit((int)*pc))
                ++pc;

        if (pc == start)
                return 0;

        ret = 'i';
        if (*pc == '.' || *pc == 'e' || *pc == 'E') {
                ret = 'f';
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
 * Return size of delimiter token, or 0 if token is not a
 * known delimiter
 */
static int
get_tok_delim_helper(int *ret, const char *s)
{
        switch (*s++) {
        case '+':
                switch (*s) {
                case '+':
                        *ret = OC_PLUSPLUS;
                        return 2;
                case '=':
                        *ret = OC_PLUSEQ;
                        return 2;
                }
                *ret = OC_PLUS;
                return 1;
        case '-':
                switch (*s) {
                case '-':
                        *ret = OC_MINUSMINUS;
                        return 2;
                case '=':
                        *ret = OC_MINUSEQ;
                        return 2;
                }
                *ret = OC_MINUS;
                return 1;
        case '<':
                switch (*s) {
                case '<': {
                        if (s[1] == '=') {
                                *ret = OC_LSEQ;
                                return 3;
                        }
                        *ret = OC_LSHIFT;
                        return 2;
                }
                case '=':
                        *ret = OC_LEQ;
                        return 2;
                }
                *ret = OC_LT;
                return 1;
        case '>':
                switch (*s) {
                case '>':
                        if (s[1] == '=') {
                                *ret = OC_RSEQ;
                                return 3;
                        }
                        *ret = OC_RSHIFT;
                        return 2;
                case '=':
                        *ret = OC_GEQ;
                        return 2;
                }
                *ret = OC_GT;
                return 1;
        case '=':
                switch (*s) {
                case '=':
                        *ret=  OC_EQEQ;
                        return 2;
                }
                *ret = OC_EQ;
                return 1;
        case '&':
                switch (*s) {
                case '&':
                        *ret = OC_ANDAND;
                        return 2;
                case '=':
                        *ret = OC_ANDEQ;
                        return 2;
                }
                *ret = OC_AND;
                return 1;
        case '|':
                switch (*s) {
                case '|':
                        *ret = OC_OROR;
                        return 2;
                case '=':
                        *ret = OC_OREQ;
                        return 2;
                }
                *ret = OC_OR;
                return 1;
        case '.':
                if (!isdigit((int)*s)) {
                        *ret = OC_PER;
                        return 1;
                }
                break;
        case '!':
                if (*s == '=') {
                        *ret = OC_NEQ;
                        return 2;
                }
                *ret = OC_EXCLAIM;
                return 1;
        case ';':
                *ret = OC_SEMI;
                return 1;
        case ',':
                *ret = OC_COMMA;
                return 1;
        case '/':
                *ret = OC_DIV;
                return 1;
        case '*':
                switch (*s) {
                case '=':
                        *ret = OC_MULEQ;
                        return 2;
                /* TODO: support **, pow() */
                }
                *ret = OC_MUL;
                return 1;
        case '%':
                if (*s == '=') {
                        *ret = OC_MODEQ;
                        return 2;
                }
                *ret = OC_MOD;
                return 1;
        case '^':
                if (*s == '=') {
                        *ret = OC_XOREQ;
                        return 2;
                }
                *ret = OC_XOR;
                return 1;
        case '(':
                *ret = OC_LPAR;
                return 1;
        case ')':
                *ret = OC_RPAR;
                return 1;
        case '[':
                *ret = OC_LBRACK;
                return 1;
        case ']':
                *ret = OC_RBRACK;
                return 1;
        case '{':
                *ret = OC_LBRACE;
                return 1;
        case '}':
                *ret = OC_RBRACE;
                return 1;
        case ':':
                *ret = OC_COLON;
                return 1;
        case '~':
                *ret = OC_TILDE;
                return 1;
        case '`':
                if (*s != '`')
                        return 0;
                        // token_errset(state, TE_HALFLAMBDA);
                *ret = OC_LAMBDA;
                return 2;
        }
        return 0;
}

/*
 * Get delimiter token, or return false if token is not a delimiter
 */
static bool
get_tok_delim(int *ret, struct token_state_t *state)
{
        int count = get_tok_delim_helper(ret, state->s);
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
                        return EOF;
        } while (skip_comment(state));
        return 0;
}

static int
tok_kw_seek(const char *key)
{
        static const struct kw_tbl_t {
                const char *name;
                int v;
        } KEYWORDS[] = {
                { "break",      OC_BREAK },
                { "const",      OC_CONST },
                { "do",         OC_DO },
                { "else",       OC_ELSE },
                { "false",      OC_FALSE },
                { "for",        OC_FOR },
                { "global",     OC_GBL },
                { "if",         OC_IF },
                { "let",        OC_LET },
                { "function",   OC_FUNC },
                { "null",       OC_NULL },
                { "private",    OC_PRIV },
                { "return",     OC_RETURN },
                { "this",       OC_THIS },
                { "true",       OC_TRUE },
                { "while",      OC_WHILE },
                { NULL, 0 }
        };
        const struct kw_tbl_t *tkw;
        for (tkw = KEYWORDS; tkw->name != NULL; tkw++) {
                if (!strcmp(tkw->name, key))
                        return tkw->v;
        }
        return -1;
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
 * TOKEN_ERROR if bad or unparseable token.  Do not de-reference
 * token data if this happens.
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
                case TE_HALFLAMBDA:
                        msg = "Unrecognized token '`'";
                        break;
                case TE_UNRECOGNIZED:
                        msg = "Unrecognized token";
                        break;
                default:
                        msg = "Token parsing error";
                        break;
                }

                err_setstr(ParserError, msg);

                /* XXX: Hacky, but do for now, remove to main.c wrapper */
                char *emsg;
                struct var_t *exc;
                err_get(&exc, &emsg);
                bug_on(exc == NULL || emsg == NULL);
                err_print(stderr, exc, emsg);
                free(emsg);
                if (state->line != NULL) {
                        fprintf(stderr, "In file %s line %d:\n",
                                state->filename, state->lineno);
                        /* the newline is included in the "%s" here */
                        fprintf(stderr, "\t%s\t", state->line);
                        if (state->s != NULL) {
                                ssize_t col = state->s - state->line;
                                while (col-- > 0)
                                        fputc(' ', stderr);
                                fprintf(stderr, "^\n");
                        }

                        /* Flush this whole line */
                        state->s = state->line;
                        state->s[0] = '\0';
                }

        } else {
                struct buffer_t *tok = &state->tok;

                buffer_reset(tok);

                /* repurpose ret to be a token-type result */
                if ((ret = skip_whitespace(state)) == EOF)
                        return ret;

                if (get_tok_delim(&ret, state)) {
                        return ret;
                } else if (get_tok_string(state)) {
                        /*
                         * this allows for strings expressed like
                         *      "..." "..."
                         * to be parsed as single concatenated literals.
                         */
                        do {
                                ret = skip_whitespace(state);
                        } while (ret != EOF && get_tok_string(state));
                        return 'q';
                } else if (get_tok_bytes(state)) {
                        do {
                        } while (ret != EOF && get_tok_bytes(state));
                        return 'b';
                } else if (get_tok_identifier(state)) {
                        if ((ret = tok_kw_seek(tok->s)) >= 0)
                                return ret;
                        return 'u';
                } else if ((ret = get_tok_number(state)) != 0) {
                        return ret;
                }
                token_errset(state, TE_UNRECOGNIZED);
        }

        /* If we're here, some error happened */
        return TOKEN_ERROR;
}

/* Get the next token from the current input file. */
static int
tokenize(struct token_state_t *state)
{
        int ret;

        ret = tokenize_helper(state);
        if (ret == TOKEN_ERROR) {
                return ret;
        } else if (ret == EOF) {
                static const struct token_t eofoc = {
                        .t = EOF,
                        .line = 0,
                        .s = NULL,
                        .i = 0LL,
                };
                state->eof = true;
                buffer_putd(&state->pgm, &eofoc, sizeof(eofoc));
        } else {
                struct token_t oc;

                oc.t = ret;
                oc.line = state->lineno;
                oc.s = literal_put(state->tok.s);
                bug_on(oc.s == NULL);
                if (ret == 'f') {
                        double f = strtod(oc.s, NULL);
                        oc.f = f;
                } else if (ret == 'i') {
                        long long i = strtoul(oc.s, NULL, 0);
                        oc.i = i;
                } else {
                        oc.i = 0LL;
                }
                buffer_putd(&state->pgm, &oc, sizeof(oc));
        }
        state->ntok++;
        return ret;
}

#define TOKBUF(state_) ((struct token_t *)(state_)->pgm.s)

/**
 * get_tok - Get the next token
 * @state:      Token state machine
 * @tok:        (output) pointer to the next token
 *
 * Return: Type of token stored in @tok.  If TOKEN_ERROR, then @tok was
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
                        bug_on(cur->t != EOF);
                        goto done;
                }
                if (tokenize(state) == TOKEN_ERROR)
                        return TOKEN_ERROR;
                bug_on(state->nexttok >= state->ntok);
        }

        /* tokenize() may have changed state->nexttok, so refresh this */
        cur = TOKBUF(state) + state->nexttok;
        state->nexttok++;

done:
        *tok = cur;
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
                free(state->line);
}

/**
 * token_state_free - Destructor for a token state machine
 * @state: A return value of token_state_new()
 *
 * Called when finished parsing a full file.
 */
void
token_state_free(struct token_state_t *state)
{
        token_state_trim(state);
        buffer_free(&state->pgm);
        free(state);
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

        buffer_init(&state->tok);
        state->line     = NULL;
        state->_slen    = 0;
        state->s        = NULL;
        state->filename = literal_put(filename);
        state->fp       = fp;
        state->lineno   = 0;

        buffer_init(&state->pgm);
        state->ntok     = 0;
        state->nexttok  = 0;
        state->eof      = false;
        state->tty      = !!isatty(fileno(fp));

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

        /* special case */
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

