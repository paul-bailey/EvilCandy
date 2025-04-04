/*
 * token.c - Tokenizer code
 *
 * TODO: Replace all the syntax() calls with proper stack unwinding
 * so it can be done in one spot in the code. This shouldn't require
 * setjmp or anything.  It would make it easier to change things so
 * that syntax() doesn't just close the program when in interactive
 * mode.
 */
#include <evilcandy.h>
#include "token.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

enum {
        QDELIM = 0x01,
        QIDENT = 0x02,
        QIDENT1 = 0x04,
        QDDELIM = 0x08,
};

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
        } else {
                state->s = NULL;
        }
        return res;
}

/* parse the usual backslash suspects */
static bool
bksl_char(char **src, int *c, int q)
{
        char *p = *src;
        switch (*p) {
        case '\'':
                *c = '\'';
                break;
        case '"':
                *c = '"';
                break;
        case 'a':
                /* BELL - apparently it's still 1978 */
                *c = '\a';
                break;
        case 'b':
                *c = '\b';
                break;
        case 'e':
                *c = '\033';
                break;
        case 'f':
                *c = '\f';
                break;
        case 'v':
                *c = '\v';
                break;
        case 'n':
                *c = '\n';
                break;
        case 'r':
                *c = '\r';
                break;
        case 't':
                *c = '\t';
                break;
        case '\\':
                *c = '\\';
                break;
        case '\r':
                *c = 0;
                if (p[1] == '\n')
                        *src += 1;
                break;
        case '\n':
                /*
                 * \<eol> means "string is wrapped for readability
                 * but <eol> not part of this string literal."
                 * Otherwise the <eol> will be recorded with the
                 * literal.
                 */
                *c = 0;
                break;
        default:
                return false;
        }
        *src += 1;
        return true;
}

/* parse \NNN, 1 to 3 digits */
static bool
bksl_octal(char **src, int *c)
{
        char *p = *src;
        int v = 0, i;
        for (i = 0; i < 3; i++) {
                if (!isodigit(*p)) {
                        if (p == *src)
                                return false;
                        break;
                }
                v <<= 3;
                /* '0' & 7 happens to be 0 */
                v += (*p++) & 7;
        }
        if (v == 0)
                return false;
        *c = v;
        *src = p;
        return true;
}

/* parse \xHH, 1 to 2 digits */
static bool
bksl_hex(char **src, int *c)
{
        char *p = *src;
        int v, nybble;
        if (*p++ != 'x')
                return false;
        v = nybble = x2bin(*p++);
        if (nybble < 0)
                return false;
        nybble = x2bin(*p);
        if (nybble >= 0) {
                ++p;
                v = (v << 4) | nybble;
        }
        if (v == 0)
                return false;
        *c = v;
        *src = p;
        return true;
}

static bool
isunihex(char *s, int amt)
{
        while (amt--) {
                if (!isxdigit((int)*s))
                        return false;
                s++;
        }
        return true;
}

/*
 * This is for our UTF-8 encoding.
 * can't use strtoul, because @len'th char might be a number,
 * but it's not a part of the escape
 */
static uint32_t
hex_str2val(char *s, int len)
{
        uint32_t v = 0;
        while (len--) {
                v <<= 4;
                v |= x2bin(*s++);
        }
        return v;
}

/*
 * If input is something like "\u1234",
 * stuff the utf-8-encoded equivalent string into @tok.
 */
static bool
bksl_utf8(char **src, int *c, struct buffer_t *tok)
{
        char *s = *src;
        int amt;
        uint32_t point;
        char buf[5];
        size_t bufsize;

        if (s[0] == 'u') {
                amt = 4;
        } else if (s[0] == 'U') {
                amt = 8;
        } else {
                return false;
        }
        s++;
        if (!isunihex(s, amt))
                return false;

        point = hex_str2val(s, amt);
        bufsize = utf8_encode(point, buf);
        if (bufsize == 0)
                return false;
        buffer_nputs(tok, buf, bufsize);

        *src = s + amt;
        *c = 0;
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
        char *pc = state->s;
        int c, q = *pc++;
        if (!isquote(q))
                return false;

retry:
        while ((c = *pc++) != q && c != '\0') {
                if (c == '\\') {
                        /*
                         * If these return true, they changed c.
                         * - bksl_utf8 sets c=0 because it fills tok itself.
                         * - Other bksl_* set c to a value to put into tok, or
                         *   to zero, which means "include neither the backslash
                         *   nor the following char," such as escaped actual
                         *   newlines in string literals.  (They could just make
                         *   adjacent literals on two lines, but I'm a sucker
                         *   for giving them alternatives, I guess, LOL.)
                         * If all return false (unsupported escape), then
                         * c is still '\'.  Throw warning, stuff without
                         * interpreting.
                         */
                        do {
                                if (bksl_utf8(&pc, &c, tok))
                                        break;
                                if (bksl_char(&pc, &c, q))
                                        break;
                                if (bksl_octal(&pc, &c))
                                        break;
                                if (bksl_hex(&pc, &c))
                                        break;
                                warning("Unsupported escape `%c'", *pc);
                        } while (0);
                        if (!c)
                                continue;
                }
                buffer_putc(tok, c);
        }

        if (c == '\0') {
                /*
                 * Got multi-line string such as
                 *      "This is a string that
                 *      spans more than one line"
                 */
                if (tok_next_line(state) == -1)
                        syntax("Unterminated quote");
                goto retry;
        }

        state->s = pc;
        return true;
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
                                        syntax("Unterminated comment");
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
                syntax("invalid chars in identifier or keyword");
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
        syntax("Integer too large");
e_malformed:
        syntax("incorectly expressed numerical value");
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
        syntax("Malformed numerical expression");
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
                        syntax("Unrecognized token '`'");
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

/*
 * returns:
 * 'd' OR'd with ((delim<<8)|flags) if token was a delimiter
 * 'k' OR'd with code<<8 for keyword if token was a keyword
 * 'q' if quoted string.
 * 'i' if integer
 * 'f' if float
 * 'u' if identifier
 * EOF if end of file
 */
static int
tokenize_helper(struct token_state_t *state)
{
        struct buffer_t *tok = &state->tok;
        int ret;

        buffer_reset(tok);

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
        } else if (get_tok_identifier(state)) {
                int k = keyword_seek(tok->s);
                return k >= 0 ? k : 'u';
        } else if ((ret = get_tok_number(state)) != 0) {
                return ret;
        }

        syntax("Unrecognized token");
        return 0;
}

/* getloc callback during tokenize() */
static unsigned int
tok_get_location(const char **file_name, void *unused)
{
        struct token_state_t *state = (struct token_state_t *)unused;
        if (file_name)
                *file_name = state->filename;
        return state->lineno;
}

/* Get the next token from the current input file. */
static int
tokenize(struct token_state_t *state)
{
        int ret;

        getloc_push(tok_get_location, state);
        ret = tokenize_helper(state);
        getloc_pop();

        if (ret == EOF) {
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
 * Return: Type of token stored in @tok
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
                tokenize(state);
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
        for (i = 'a'; i < 'z'; i++)
                tok_charmap[i] |= QIDENT | QIDENT1;
        for (i = 'A'; i < 'Z'; i++)
                tok_charmap[i] |= QIDENT | QIDENT1;
        for (i = '0'; i < '9'; i++)
                tok_charmap[i] |= QIDENT;
        tok_charmap['_'] |= QIDENT | QIDENT1;
}

