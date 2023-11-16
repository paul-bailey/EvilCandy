#include "egq.h"
#include <ctype.h>

static inline bool q_isascii(int c) { return c && c == (c & 0x7fu); }
static inline bool
q_isflags(int c, unsigned char flags)
{
        return q_isascii(c) && (q_.charmap[c] & flags) == flags;
}

static inline bool q_isdelim(int c) { return q_isflags(c, QDELIM); }
static inline bool q_isident(int c) { return q_isflags(c, QIDENT); }
static inline bool q_isident1(int c) { return q_isflags(c, QIDENT1); }
static inline bool q_isdelim2(int c) { return q_isflags(c, QDDELIM); }

/*
 * because this is @#$%!-ing tedious to type,
 * and we don't go to next ns in this module
 */
#define cur_pc q_.pc.px.s

/* if at '\0' after this, then end of namespace */
static void
qslide(void)
{
        const char *s = cur_pc;
        while (*s != '\0' && isspace((int)(*s)))
                ++s;
        cur_pc = s;

        /*
         * If '\0', do not advance to next ns.
         * Exectution code in one ns may not run on into another
         */
}

/* parse the usual backslash suspects */
static bool
bksl_char(const char **src, int *c, int q)
{
        const char *p = *src;
        if (!!q && *p == q) {
                *c = q;
        } else switch (*p) {
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
bksl_octal(const char **src, int *c)
{
        const char *p = *src;
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
        *c = v;
        *src = p;
        return true;
}

/* parse \xHH, 1 to 2 digits */
static bool
bksl_hex(const char **src, int *c)
{
        const char *p = *src;
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
        *c = v;
        *src = p;
        return true;
}

/* pc points at quote */
static bool
qlex_string(void)
{
        struct token_t *tok = &q_.tok;
        const char *pc = cur_pc;
        int c, q = *pc++;
        if (!isquote(q))
                return false;

        while ((c = *pc++) != q && c != '\0') {
                if (c == '\\') {
                        do {
                                if (bksl_char(&pc, &c, q))
                                        break;
                                if (bksl_octal(&pc, &c))
                                        break;
                                if (bksl_hex(&pc, &c))
                                        break;
                                qsyntax("Unsupported escape `%c'", *pc);
                        } while (0);
                        if (!c)
                                continue;
                }
                token_putc(tok, c);
        }

        if (c == '\0')
                qsyntax("Unterminated quote");

        cur_pc = pc;
        return true;
}

static bool
qlex_comment(void)
{
        const char *pc = cur_pc;
        if (*pc++ != '/')
                return false;

        if (*pc == '/') {
                /* single-line comment */
                while (*pc != '\n' && *pc != '\0')
                        ++pc;
                cur_pc = pc;
                return true;
        }

        if (*pc == '*') {
                /* block comment */
                do {
                        ++pc;
                } while (*pc != '\0'
                         && !(pc[0] == '*' && pc[1] == '/'));
                if (*pc == '\0')
                        qsyntax("Unterminated comment");
                cur_pc = pc;
                return true;
        }
        return false;
}

static bool
qlex_identifier(void)
{
        struct token_t *tok = &q_.tok;
        const char *pc = cur_pc;
        if (!q_isident1(*pc))
                return false;
        while (q_isident(*pc))
                token_putc(tok, *pc++);
        if (!q_isdelim(*pc))
                qsyntax("invalid chars in identifier or keyword");
        cur_pc = pc;
        return true;
}

static bool
qlex_hex(void)
{
        struct token_t *tok = &q_.tok;
        const char *pc = cur_pc;
        if (pc[0] != '0' || toupper((int)(pc[1])) != 'x')
                return false;
        token_putc(tok, *pc++);
        token_putc(tok, *pc++);
        if (!isxdigit((int)(*pc)))
                qsyntax("incorrectly expressed numerical value");
        while (isxdigit((int)(*pc)))
                token_putc(tok, *pc++);
        if (!q_isdelim(*pc))
                qsyntax("Excess characters after hex literal");
        cur_pc = pc;
        return true;
}

static int
qlex_number(void)
{
        const char *pc, *start;
        int ret;

        if (qlex_hex())
                return 'i';

        pc = start = cur_pc;

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
                        const char *e = pc;
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
                token_putc(&q_.tok, *start++);
        cur_pc = pc;
        return ret;

malformed:
        qsyntax("Malformed numerical expression");
        return 0;
}

static int
qlex_delim2(const char **src, int *d)
{
        const char *s = *src;
        if (!q_isdelim2(*d))
                return false;
        if (*s == *d) {
                *d = q_.char_x2tbl[*d];
        } else if (*s != '=') {
                return false;
        } else switch (*d) {
        case '<':
                *d = QD_LEQ;
                break;
        case '>':
                *d = QD_GEQ;
                break;
        case '!':
                *d = QD_NEQ;
                break;
        default:
                return false;
        }
        *src = s + 1;
        token_putc(&q_.tok, *(s-1));
        token_putc(&q_.tok, *s);
        return true;
}

static bool
qlex_delim(int *res)
{
        const char *s = cur_pc;
        int d = *s++;
        if (!q_isdelim(d))
                return false;
        if (!qlex_delim2(&s, &d)) {
                token_putc(&q_.tok, d);
                d = q_.char_xtbl[d];
        }

        bug_on(!d);
        *res = TO_DTOK(d);
        cur_pc = s;
        return true;
}

static int
qlex_helper(void)
{
        struct token_t *tok = &q_.tok;
        int ret;

        token_reset(tok);
        qop_mov(&q_.pclast, &q_.pc);

        do {
                qslide();
                if (*cur_pc == '\0')
                        return EOF;
        } while (qlex_comment());

        if (qlex_delim(&ret)) {
                return ret;
        } else if (qlex_string()) {
                return 'q';
        } else if (qlex_identifier()) {
                int *lu = hashtable_get(q_.kw_htbl, tok->s, NULL);
                return lu ?  TO_KTOK(*lu) : 'u';
        } else if ((ret = qlex_number()) != 0) {
                return ret;
        }

        qsyntax("Unrecognized token");
        return 0;
}

/*
 * returns:
 * 'd' OR'd with delim<<8 if token was a delimiter
 * 'k' OR'd with code<<8 for keyword if token was a keyword
 * 'q' if q_.tok is filled with quoted string.
 * 'i' if integer
 * 'f' if float
 * 'u' if q_.tok has identifier
 * EOF if end of namespace
 */
int
qlex(void)
{
        int t = qlex_helper();
        q_.t = t;
        return t;
}

