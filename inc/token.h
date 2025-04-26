#ifndef EGQ_OPCODES_H
#define EGQ_OPCODES_H

#include <stdio.h> /* for FILE */

#include "typedefs.h"
#include "token_gen.h"

#if 0
enum {
        /*
         * If the token is not one of "fiuq",
         * it will be one of these
         */
        OC_PLUS = 1,
        OC_MINUS,
        OC_GT,
        OC_LT,
        OC_EQ,
        OC_AND,
        OC_OR,
        OC_PER,
        OC_EXCLAIM,
        OC_SEMI,
        OC_COMMA,
        OC_DIV,
        OC_MUL,
        OC_POW,
        OC_MOD,
        OC_XOR,
        OC_LPAR,
        OC_RPAR,
        OC_LBRACK,
        OC_RBRACK,
        OC_LBRACE,
        OC_RBRACE,
        OC_COLON,
        OC_TILDE,
        OC_PLUSPLUS,
        OC_MINUSMINUS,
        OC_LSHIFT,
        OC_RSHIFT,
        OC_EQEQ,
        OC_ANDAND,
        OC_OROR,
        OC_LEQ,
        OC_GEQ,
        OC_NEQ,
        OC_LAMBDA,
        OC_PLUSEQ,
        OC_MINUSEQ,
        OC_MULEQ,
        OC_DIVEQ,
        OC_MODEQ,
        OC_XOREQ,
        OC_LSEQ,
        OC_RSEQ,
        OC_OREQ,
        OC_ANDEQ,
        OC_FUNC,
        OC_LET,
        OC_THIS,
        OC_RETURN,
        OC_BREAK,
        OC_CONTINUE,
        OC_IF,
        OC_WHILE,
        OC_ELSE,
        OC_DO,
        OC_FOR,
        OC_GBL,
        OC_CONST,
        OC_PRIV,
        OC_TRUE,
        OC_FALSE,
        OC_TRY,
        OC_CATCH,
        OC_FINALLY,
        OC_NULL,
        OC_EOF,
        OC_STRING,      /* used to be 'q' */
        OC_BYTES,       /* used to be 'b' */
        OC_IDENTIFIER,  /* used to be 'u' */
        OC_INTEGER,     /* used to be 'i' */
        OC_FLOAT,       /* used to be 'f' */
        OC_NTOK
};
#endif /* 0 */
/**
 * struct token_t - Token metadata
 * @t:          Type of token, an OC_* enum, or one of "fiuq"
 * @line:       Line number in file where this token was parsed,
 *              used for tracing for error messages.
 *              XXX Wasteful, this is only used for disassembly, and for
 *              only the first opcode of an executable, nowhere else.
 * @s:          Content of the token parsed
 * @v:          Value of the token, if @t is for a literal expression
 *              of a user variable.
 */
struct token_t {
        unsigned int t;
        unsigned int line;
        char *s;
        Object *v;
};

/* opaque struct, used only in token.c */
struct token_state_t;

typedef int token_pos_t;

/* token.c */
extern void token_state_trim(struct token_state_t *state);
extern void token_state_free(struct token_state_t *state);
extern struct token_state_t *token_state_new(FILE *fp,
                                        const char *filename);
extern int get_tok(struct token_state_t *state, struct token_t **tok);
extern void unget_tok(struct token_state_t *state, struct token_t **tok);
extern token_pos_t token_get_pos(struct token_state_t *state);
extern token_pos_t token_swap_pos(struct token_state_t *state, token_pos_t pos);
extern char *token_get_this_line(struct token_state_t *state, int *col);

#endif /* EGQ_OPCODES_H */

