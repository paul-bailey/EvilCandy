/*
 * symtab.c - Simple symbol table to assist fuzzer program generation.
 *
 * If any of these functions return NULL, it means your generated program
 * is getting too complicated. Reset the state and try again. This
 * library calls rand() in places, so always do the same thing when
 * handling NULL return values, to keep the per-seed behavior
 * deterministic.
 */
#include <tests/fuzzer_symtab.h>
#include <tests/strbuf.h>
#include <lib/helpers.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *VALID_NAMES[] = {
        "hal",
        "falstaff",
        "poins",
        "romeo",
        "juliet",
        "mercutio",
        "bardolf",
        "prospero",
        "miranda",
        "rosalind",
        "celia",
        "lear",
        "othello",
        "hotspur",

        /* the baddies, but here they're "good" :) */
        "macbeth",
        "iago",
        "shylock",
        "aaron",
        "edmund",
        "goneril",
        "regan",
};

enum {
        NR_LETTERS = 26,
        NR_SYMTAB = 32,
};
#define MAX_VALID_NAMES ARRAY_SIZE(VALID_NAMES)

static struct fuzzer_symtab_t symtab_pool[NR_SYMTAB];
static uint32_t symtab_used = 0;

/* Reset fuzzer state.  Call once per program being generated. */
void
fuzzer_symtab_reset_state(void)
{
        symtab_used = 0;
}

struct fuzzer_symtab_t *
fuzzer_symtab_new(void)
{
        struct fuzzer_symtab_t *ret;
        uint32_t t_used = symtab_used;
        int i;

        for (i = 0; i < NR_SYMTAB; i++) {
                if (!(t_used & 1))
                        break;
                t_used >>= 1;
        }
        if (i == NR_SYMTAB)
                return NULL;

        symtab_used |= (1u << i);

        ret = &symtab_pool[i];
        memset(ret, 0, sizeof(*ret));
        return ret;
}

void
fuzzer_symtab_free(struct fuzzer_symtab_t *sym)
{
        size_t off = sym - symtab_pool;
        assert(off < NR_SYMTAB);
        symtab_used &= ~(1u << off);
}

/* Add a new name to @sym and return it */
const char *
fuzzer_symtab_new_name(struct fuzzer_symtab_t *sym)
{
        if (sym->ind >= MAX_VALID_NAMES)
                return NULL;
        return VALID_NAMES[sym->ind++];
}

/* Return an existing name from @sym */
const char *
fuzzer_symtab_existing_name(struct fuzzer_symtab_t *sym)
{
        if (!sym->ind)
                return NULL;
        return VALID_NAMES[rand() % sym->ind];
}

/* Return a name guaranteed not to be in @sym */
const char *
fuzzer_symtab_bad_name(struct fuzzer_symtab_t *sym)
{
        static char BADNAMES[NR_LETTERS * 2];
        if (BADNAMES[0] == '\0') {
                /* initialize to 1-char strings, one for each letter */
                unsigned int c, i;
                for (i = 0, c = 'a'; c <= 'z'; c++, i+=2) {
                        BADNAMES[i] = c;
                        BADNAMES[i+1] = '\0';
                }
        }

        return &BADNAMES[(rand() % NR_LETTERS) * 2];
}

