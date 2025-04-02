/* lex.c - Tokenizer and prescan code */
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
                          in a single file.
 * @lineno:     Current line number in file.
 * @tok:        Last parsed token, not literal()-ized yet
 * @s:          Current pointer into @line, where to look for next token
 * @_slen:      Length of line buffer, for getline calls
 * @line:       line buffer, for getline calls
 * @fp:         File we're getting input from
 * @filename:   name of @fp
 */
struct token_state_t {
        int lineno;
        struct buffer_t tok;
        char *s;
        size_t _slen;
        char *line;
        FILE *fp;
        char *filename;
};

/* a sort of "ctype" for tokens */
static unsigned char lexer_charmap[128];

static inline bool q_isascii(int c) { return c && c == (c & 0x7fu); }
static inline bool
q_isflags(int c, unsigned char flags)
{
        return q_isascii(c) && (lexer_charmap[c] & flags) == flags;
}

static inline bool q_isdelim(int c) { return q_isflags(c, QDELIM); }
/* may be in identifier */
static inline bool q_isident(int c) { return q_isflags(c, QIDENT); }
/* may be 1st char of identifier */
static inline bool q_isident1(int c) { return q_isflags(c, QIDENT1); }

static int
lexer_next_line(struct token_state_t *state)
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

/* if at '\0' after this, then end of namespace */
static void
qslide(struct token_state_t *state)
{
        char *s;
        do {
                s = state->s;
                while (*s != '\0' && isspace((int)(*s)))
                        ++s;
        } while (*s == '\0' && lexer_next_line(state) != -1);
        state->s = s;
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
 * can't use strtoul, because 5th char might be a number,
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

/* pc points at quote */
static bool
qlex_string(struct token_state_t *state)
{
        struct buffer_t *tok = &state->tok;
        char *pc = state->s;
        int c, q = *pc++;
        if (!isquote(q))
                return false;

retry:
        while ((c = *pc++) != q && c != '\0') {
                if (c == '\\') {
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
                if (lexer_next_line(state) == -1)
                        syntax("Unterminated quote");
                goto retry;
        }

        state->s = pc;
        return true;
}

static bool
qlex_comment(struct token_state_t *state)
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
                                if (lexer_next_line(state) == -1)
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

static bool
qlex_identifier(struct token_state_t *state)
{
        struct buffer_t *tok = &state->tok;
        char *pc = state->s;
        if (!q_isident1(*pc))
                return false;
        while (q_isident(*pc))
                buffer_putc(tok, *pc++);
        if (!q_isdelim(*pc))
                syntax("invalid chars in identifier or keyword");
        state->s = pc;
        return true;
}

/* parse hex/binary int if '0x' or '0b' */
static bool
qlex_int_hdr(struct token_state_t *state)
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


        if (!q_isdelim(*pc))
                goto e_malformed;

        state->s = pc;
        return true;

e_toobig:
        syntax("Integer too large");
e_malformed:
        syntax("incorectly expressed numerical value");
        return false;
}

static int
qlex_number(struct token_state_t *state)
{
        char *pc, *start;
        int ret;

        if (qlex_int_hdr(state))
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
        if (!q_isdelim(*pc))
                goto malformed;

        while (start < pc)
                buffer_putc(&state->tok, *start++);
        state->s = pc;
        return ret;

malformed:
        syntax("Malformed numerical expression");
        return 0;
}

static int
qlex_delim_helper(int *ret, const char *s)
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

static bool
qlex_delim(int *ret, struct token_state_t *state)
{
        int count = qlex_delim_helper(ret, state->s);
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

static int
qlex_slide(struct token_state_t *state)
{
        do {
                qslide(state);
                if (*(state->s) == '\0')
                        return EOF;
        } while (qlex_comment(state));
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

        if ((ret = qlex_slide(state)) == EOF)
                return ret;

        if (qlex_delim(&ret, state)) {
                return ret;
        } else if (qlex_string(state)) {
                do {
                        ret = qlex_slide(state);
                } while (ret != EOF && qlex_string(state));
                return 'q';
        } else if (qlex_identifier(state)) {
                int k = keyword_seek(tok->s);
                return k >= 0 ? k : 'u';
        } else if ((ret = qlex_number(state)) != 0) {
                return ret;
        }

        syntax("Unrecognized token");
        return 0;
}

/**
 * tokenize - Get the next token from the current
 *            input file.
 * @oc: where to store the result
 *
 * Return: Same value as oc->t
 *
 * XXX: not ready for extern linkage yet, need to do the getloc
 * thing from parent (currently prescan), then we'll add this as
 * the function as_lex calls in assembler.c, so we can have
 * interactive mode.
 */
int
tokenize(struct token_t *oc, struct token_state_t *state)
{
        int ret = tokenize_helper(state);
        if (ret == EOF) {
                static const struct token_t eofoc = {
                        .t = EOF,
                        .line = 0,
                        .s = NULL,
                        .i = 0LL,
                };
                memcpy(oc, &eofoc, sizeof(*oc));
        } else {
                oc->t = ret;
                oc->line = state->lineno;
                oc->s = literal_put(state->tok.s);
                bug_on(oc->s == NULL);
                if (oc->t == 'f') {
                        double f = strtod(oc->s, NULL);
                        oc->f = f;
                } else if (oc->t == 'i') {
                        long long i = strtoul(oc->s, NULL, 0);
                        oc->i = i;
                } else {
                        oc->i = 0LL;
                }
        }
        return ret;
}

static void
buffer_putcode(struct buffer_t *buf, struct token_t *oc)
{
        buffer_putd(buf, oc, sizeof(*oc));
}

static unsigned int
lexer_get_location(const char **file_name, void *unused)
{
        struct token_state_t *state = (struct token_state_t *)unused;
        if (file_name)
                *file_name = state->filename;
        return state->lineno;
}

static void
token_state_free(struct token_state_t *state)
{
        buffer_free(&state->tok);
        if (state->line)
                free(state->line);
}

static struct token_state_t *
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

        /*
         * Get first line, so that the
         * above functions don't all have to start
         * with "if (state->s == NULL)"
         */
        if (lexer_next_line(state) == -1) {
                token_state_free(state);
                return NULL;
        }

        return state;
}

/*
 * XXX REVISIT: cf. assembler.c, ``as_lex'' and ``as_unlex''.  Instead of
 * prescanning the whole file, this could be done a token at a time.  It
 * saves some steps and makes it easier to transition the project into a
 * VM that can run in interactive mode.
 */

/*
 * Unless error interrupts this, returns array of tokens whose last
 * member has a .t value of EOF.
 */
struct token_t *
prescan(FILE *fp, const char *filename)
{
        struct token_t oc;
        struct token_state_t *state;
        int t;
        struct buffer_t pgm;

        bug_on(!filename);

        state = token_state_new(fp, filename);
        if (!state)
                return NULL;

        getloc_push(lexer_get_location, state);

        buffer_init(&pgm);
        do {
                t = tokenize(&oc, state);
                /* yes, also stuff the EOF token */
                buffer_putcode(&pgm, &oc);
        } while (t != EOF);

        getloc_pop();

        token_state_free(state);
        return (struct token_t *)pgm.s;
}

void
moduleinit_lex(void)
{
        /*
         * IMPORTANT!! These two strings must be in same order as
         *             their QD_* enums in opcode.h
         */
        static const char *const DELIMS = "+-<>=&|.!;,/*%^()[]{}:~ \t\n";
        static const char *const DELIMDBL = "+-<>=&|";
        const char *s;
        int i;

        /*
         * Set up lexer_charmap
         * XXX: more optimal to put this in a code generator
         * so it's all done at compile time instead.
         */

        /* delimiter */
        for (s = DELIMS; *s != '\0'; s++)
                lexer_charmap[(int)*s] |= QDELIM;
        /* double-delimeters */
        for (s = DELIMDBL; *s != '\0'; s++)
                lexer_charmap[(int)*s] |= QDDELIM;

        /* special case */
        lexer_charmap[(int)'`'] |= (QDELIM | QDDELIM);
        lexer_charmap[0] |= QDELIM;

        /* permitted identifier chars */
        for (i = 'a'; i < 'z'; i++)
                lexer_charmap[i] |= QIDENT | QIDENT1;
        for (i = 'A'; i < 'Z'; i++)
                lexer_charmap[i] |= QIDENT | QIDENT1;
        for (i = '0'; i < '9'; i++)
                lexer_charmap[i] |= QIDENT;
        lexer_charmap['_'] |= QIDENT | QIDENT1;
}

