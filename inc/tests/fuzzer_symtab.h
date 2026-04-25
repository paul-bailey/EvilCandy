#ifndef EVC_INC_TESTS_FUZZER_SYMTAB_H
#define EVC_INC_TESTS_FUZZER_SYMTAB_H

#include <stddef.h>
#include <evilcandy/compiler.h>

struct fuzzer_symtab_t {
        size_t ind;
};

extern void fuzzer_symtab_reset_state(void);
extern WARN_UNUSED_RESULT struct fuzzer_symtab_t *fuzzer_symtab_new(void);
extern void fuzzer_symtab_free(struct fuzzer_symtab_t *sym);
extern WARN_UNUSED_RESULT const char *fuzzer_symtab_new_name(struct fuzzer_symtab_t *sym);
extern WARN_UNUSED_RESULT const char *fuzzer_symtab_existing_name(struct fuzzer_symtab_t *sym);
extern const char *fuzzer_symtab_bad_name(struct fuzzer_symtab_t *sym);

#endif /* EVC_INC_TESTS_FUZZER_SYMTAB_H */
