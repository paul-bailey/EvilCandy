#ifndef EGQ_OPCODES_H
#define EGQ_OPCODES_H

#include <stdio.h> /* for FILE */

#include "typedefs.h"
#include "token_gen.h"

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
extern int get_tok_from_cstring(const char *s, char **endptr, struct token_t *dst);
extern void unget_tok(struct token_state_t *state, struct token_t **tok);
extern token_pos_t token_get_pos(struct token_state_t *state);
extern token_pos_t token_swap_pos(struct token_state_t *state, token_pos_t pos);
extern char *token_get_this_line(struct token_state_t *state, int *col);

#endif /* EGQ_OPCODES_H */

