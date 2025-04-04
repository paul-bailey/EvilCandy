#ifndef EGQ_OPCODES_H
#define EGQ_OPCODES_H

#define TO_TOK(c1_, c2_)        ((c1_) | ((c2_) << 8))
#define TO_DTOK(c_)             TO_TOK('d', c_)
#define TO_KTOK(c_)             TO_TOK('k', c_)
#define TOKEN_ERROR             TO_TOK('!', TD_ERROR)

enum {
        TF_LOGICAL      = 0x00010000,
        TF_BITWISE      = 0x00020000,
        TF_RELATIONAL   = 0x00040000,
        TF_SHIFT        = 0x00080000,
        TF_ADDSUB       = 0x00100000,
        TF_MULDIVMOD    = 0x00200000,
        TF_UNARY        = 0x00400000,
        TF_INDIRECT     = 0x00800000,
        TF_ASSIGN       = 0x01000000,
};

/* delimiter codes */
enum {
        TD_PLUS = 1,
        TD_MINUS,
        TD_LT,
        TD_GT,
        TD_EQ,
        TD_AND,
        TD_OR,
        TD_PER,
        TD_EXCLAIM,
        TD_SEMI,
        TD_COMMA,
        TD_DIV,
        TD_MUL,
        TD_MOD,
        TD_XOR,
        TD_LPAR,
        TD_RPAR,
        TD_LBRACK,
        TD_RBRACK,
        TD_LBRACE,
        TD_RBRACE,
        TD_COLON,
        TD_TILDE,

        TD_PLUSPLUS,
        TD_MINUSMINUS,
        TD_LSHIFT,
        TD_RSHIFT,
        TD_EQEQ,
        TD_ANDAND,
        TD_OROR,

        TD_LEQ,
        TD_GEQ,
        TD_NEQ,

        TD_LAMBDA,

        TD_PLUSEQ,
        TD_MINUSEQ,
        TD_MULEQ,
        TD_DIVEQ,
        TD_MODEQ,
        TD_XOREQ,
        TD_LSEQ,
        TD_RSEQ,
        TD_OREQ,
        TD_ANDEQ,

        TD_ERROR,
};

/* keyword codes */
enum {
        KW_FUNC = 1,
        KW_LET,
        KW_THIS,
        KW_RETURN,
        KW_BREAK,
        KW_IF,
        KW_WHILE,
        KW_ELSE,
        KW_DO,
        KW_FOR,
        KW_LOAD,
        KW_CONST,
        KW_PRIV,
        KW_TRUE,
        KW_FALSE,
        KW_NULL,
        N_KW,
};

enum {
        /*
         * If the token is not one of "fiuq",
         * it will be one of these
         */
        OC_PLUS         = TO_DTOK(TD_PLUS) | TF_ADDSUB | TF_UNARY,
        OC_MINUS        = TO_DTOK(TD_MINUS) | TF_ADDSUB | TF_UNARY,
        OC_GT           = TO_DTOK(TD_GT) | TF_RELATIONAL,
        OC_LT           = TO_DTOK(TD_LT) | TF_RELATIONAL,
        OC_EQ           = TO_DTOK(TD_EQ) | TF_ASSIGN,
        OC_AND          = TO_DTOK(TD_AND) | TF_BITWISE,
        OC_OR           = TO_DTOK(TD_OR) | TF_BITWISE,
        OC_PER          = TO_DTOK(TD_PER) | TF_INDIRECT,
        OC_EXCLAIM      = TO_DTOK(TD_EXCLAIM) | TF_UNARY,
        OC_SEMI         = TO_DTOK(TD_SEMI),
        OC_COMMA        = TO_DTOK(TD_COMMA),
        OC_DIV          = TO_DTOK(TD_DIV) | TF_MULDIVMOD,
        OC_MUL          = TO_DTOK(TD_MUL) | TF_MULDIVMOD,
        OC_MOD          = TO_DTOK(TD_MOD) | TF_MULDIVMOD,
        OC_XOR          = TO_DTOK(TD_XOR) | TF_BITWISE,
        OC_LPAR         = TO_DTOK(TD_LPAR) | TF_INDIRECT,
        OC_RPAR         = TO_DTOK(TD_RPAR),
        OC_LBRACK       = TO_DTOK(TD_LBRACK) | TF_INDIRECT,
        OC_RBRACK       = TO_DTOK(TD_RBRACK),
        OC_LBRACE       = TO_DTOK(TD_LBRACE),
        OC_RBRACE       = TO_DTOK(TD_RBRACE),
        OC_COLON        = TO_DTOK(TD_COLON),
        OC_TILDE        = TO_DTOK(TD_TILDE) | TF_UNARY,
        OC_PLUSPLUS     = TO_DTOK(TD_PLUSPLUS) | TF_ASSIGN,
        OC_MINUSMINUS   = TO_DTOK(TD_MINUSMINUS) | TF_ASSIGN,
        OC_LSHIFT       = TO_DTOK(TD_LSHIFT) | TF_SHIFT,
        OC_RSHIFT       = TO_DTOK(TD_RSHIFT) | TF_SHIFT,
        OC_EQEQ         = TO_DTOK(TD_EQEQ) | TF_RELATIONAL,
        OC_ANDAND       = TO_DTOK(TD_ANDAND) | TF_LOGICAL,
        OC_OROR         = TO_DTOK(TD_OROR) | TF_LOGICAL,
        OC_LEQ          = TO_DTOK(TD_LEQ) | TF_RELATIONAL,
        OC_GEQ          = TO_DTOK(TD_GEQ) | TF_RELATIONAL,
        OC_NEQ          = TO_DTOK(TD_NEQ) | TF_RELATIONAL,
        OC_LAMBDA       = TO_DTOK(TD_LAMBDA),

        /*
         * other flags not paired with TF_ASSIGN, or eval code will
         * get confused.
         */
        OC_PLUSEQ       = TO_DTOK(TD_PLUSEQ) | TF_ASSIGN,
        OC_MINUSEQ      = TO_DTOK(TD_MINUS) | TF_ASSIGN,
        OC_MULEQ        = TO_DTOK(TD_MULEQ) | TF_ASSIGN,
        OC_DIVEQ        = TO_DTOK(TD_DIVEQ) | TF_ASSIGN,
        OC_MODEQ        = TO_DTOK(TD_MODEQ) | TF_ASSIGN,
        OC_XOREQ        = TO_DTOK(TD_XOREQ) | TF_ASSIGN,
        OC_LSEQ         = TO_DTOK(TD_LSEQ) | TF_ASSIGN,
        OC_RSEQ         = TO_DTOK(TD_RSEQ) | TF_ASSIGN,
        OC_OREQ         = TO_DTOK(TD_OREQ) | TF_ASSIGN,
        OC_ANDEQ        = TO_DTOK(TD_ANDEQ) | TF_ASSIGN,

        OC_FUNC         = TO_KTOK(KW_FUNC),
        OC_LET          = TO_KTOK(KW_LET),
        OC_THIS         = TO_KTOK(KW_THIS),
        OC_RETURN       = TO_KTOK(KW_RETURN),
        OC_BREAK        = TO_KTOK(KW_BREAK),
        OC_IF           = TO_KTOK(KW_IF),
        OC_WHILE        = TO_KTOK(KW_WHILE),
        OC_ELSE         = TO_KTOK(KW_ELSE),
        OC_DO           = TO_KTOK(KW_DO),
        OC_FOR          = TO_KTOK(KW_FOR),
        OC_LOAD         = TO_KTOK(KW_LOAD),
        OC_CONST        = TO_KTOK(KW_CONST),
        OC_PRIV         = TO_KTOK(KW_PRIV),
        OC_TRUE         = TO_KTOK(KW_TRUE),
        OC_FALSE        = TO_KTOK(KW_FALSE),
        OC_NULL         = TO_KTOK(KW_NULL),
};

/**
 * struct token_t - Token metadata
 * @t:          Type of token, an OC_* enum, or one of "fiuq"
 * @line:       Line number in file where this token was parsed,
 *              used for tracing for error messages.
 *              XXX Wasteful, there are ways to reduce this.
 * @s:          Content of the token parsed
 * @f:          Value of the token, if @t is 'f'
 * @i:          Value of the token, if @t is 'i'
 */
struct token_t {
        unsigned int t;
        unsigned int line;
        char *s;
        union {
                double f;
                long long i;
        };
};

static inline int tok_delim(int t) { return (t >> 8) & 0x7fu; }
static inline int tok_type(int t) { return t & 0x7fu; }
static inline int tok_keyword(int t) { return (t >> 8) & 0x7fu; }

#endif /* EGQ_OPCODES_H */

