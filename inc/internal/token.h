#ifndef EGQ_OPCODES_H
#define EGQ_OPCODES_H

#include <stdio.h> /* for FILE */

#include "typedefs.h"
#include "token_gen.h"

/**
 * struct token_t - Token metadata
 * @t:          Type of token, an OC_* enum, or one of "fiuq"
 * @v:          Value of the token, if @t is for a literal expression
 *              of a user variable.
 */
struct token_t {
        unsigned int t;
        unsigned int start_line;
        unsigned int stop_line;
        unsigned int start_col;
        unsigned int stop_col;
        Object *v;
};

/* opaque struct, used only in token.c */
struct token_state_t;

typedef int token_pos_t;

/* token.c */
extern void token_state_trim(struct token_state_t *state);
extern void token_state_free(struct token_state_t *state);
extern struct token_state_t *token_state_new(FILE *fp);
extern int get_tok(struct token_state_t *state, struct token_t **tok);
extern void unget_tok(struct token_state_t *state, struct token_t **tok);
extern token_pos_t token_get_pos(struct token_state_t *state);
extern token_pos_t token_swap_pos(struct token_state_t *state, token_pos_t pos);
extern char *token_get_this_line(struct token_state_t *state);
extern void token_flush_tty(struct token_state_t *state);
extern struct token_t *get_tok_at(struct token_state_t *state, token_pos_t pos);
extern struct token_state_t *token_state_from_string(const char *cstring);

struct gbl_token_subsys_t;
extern void token_deinit_gbl(struct gbl_token_subsys_t *subsys);
extern struct gbl_token_subsys_t *token_init_gbl(void);

#endif /* EGQ_OPCODES_H */

