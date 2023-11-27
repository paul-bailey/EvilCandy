#ifndef EGQ_OPCODES_H
#define EGQ_OPCODES_H

#define TO_TOK(c1_, c2_)        ((c1_) | ((c2_) << 8))
#define TO_DTOK(c_)             TO_TOK('d', c_)
#define TO_KTOK(c_)             TO_TOK('k', c_)

/* delimiter codes */
enum {
        QD_PLUS = 1,
        QD_MINUS,
        QD_LT,
        QD_GT,
        QD_EQ,
        QD_AND,
        QD_OR,
        QD_PER,
        QD_EXCLAIM,
        QD_SEMI,
        QD_COMMA,
        QD_DIV,
        QD_MUL,
        QD_MOD,
        QD_XOR,
        QD_LPAR,
        QD_RPAR,
        QD_LBRACK,
        QD_RBRACK,
        QD_LBRACE,
        QD_RBRACE,
        QD_COLON,
        QD_TILDE,

        QD_PLUSPLUS,
        QD_MINUSMINUS,
        QD_LSHIFT,
        QD_RSHIFT,
        QD_EQEQ,
        QD_ANDAND,
        QD_OROR,

        QD_LEQ,
        QD_GEQ,
        QD_NEQ,

        QD_LAMBDA,
        /* technically this is one more, but whatever... */
        QD_NCODES,
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
        N_KW,
};

enum {
        /*
         * If the 'opcode' is not one of "fiuq",
         * it will be one of these
         */
        OC_PLUS         = TO_DTOK(QD_PLUS),
        OC_MINUS        = TO_DTOK(QD_MINUS),
        OC_GT           = TO_DTOK(QD_GT),
        OC_LT           = TO_DTOK(QD_LT),
        OC_EQ           = TO_DTOK(QD_EQ),
        OC_AND          = TO_DTOK(QD_AND),
        OC_OR           = TO_DTOK(QD_OR),
        OC_PER          = TO_DTOK(QD_PER),
        OC_EXCLAIM      = TO_DTOK(QD_EXCLAIM),
        OC_SEMI         = TO_DTOK(QD_SEMI),
        OC_COMMA        = TO_DTOK(QD_COMMA),
        OC_DIV          = TO_DTOK(QD_DIV),
        OC_MUL          = TO_DTOK(QD_MUL),
        OC_MOD          = TO_DTOK(QD_MOD),
        OC_XOR          = TO_DTOK(QD_XOR),
        OC_LPAR         = TO_DTOK(QD_LPAR),
        OC_RPAR         = TO_DTOK(QD_RPAR),
        OC_LBRACK       = TO_DTOK(QD_LBRACK),
        OC_RBRACK       = TO_DTOK(QD_RBRACK),
        OC_LBRACE       = TO_DTOK(QD_LBRACE),
        OC_RBRACE       = TO_DTOK(QD_RBRACE),
        OC_COLON        = TO_DTOK(QD_COLON),
        OC_TILDE        = TO_DTOK(QD_TILDE),
        OC_PLUSPLUS     = TO_DTOK(QD_PLUSPLUS),
        OC_MINUSMINUS   = TO_DTOK(QD_MINUSMINUS),
        OC_LSHIFT       = TO_DTOK(QD_LSHIFT),
        OC_RSHIFT       = TO_DTOK(QD_RSHIFT),
        OC_EQEQ         = TO_DTOK(QD_EQEQ),
        OC_ANDAND       = TO_DTOK(QD_ANDAND),
        OC_OROR         = TO_DTOK(QD_OROR),
        OC_LEQ          = TO_DTOK(QD_LEQ),
        OC_GEQ          = TO_DTOK(QD_GEQ),
        OC_NEQ          = TO_DTOK(QD_NEQ),
        OC_LAMBDA       = TO_DTOK(QD_LAMBDA),

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
};

#endif /* EGQ_OPCODES_H */

