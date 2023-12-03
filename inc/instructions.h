#ifndef EGQ_INSTRUCTIONS_H
#define EGQ_INSTRUCTIONS_H

#include <stdint.h>
#include "list.h"
#include "instruction_defs.h"

/* GETATTR, SETATTR, arg1 enumerations */
enum {
        IARG_ATTR_CONST = 0,
        IARG_ATTR_STACK = 1,
};

/* PUSH_PTR/PUSH_COPY arg1 enumerations */
enum {
        IARG_PTR_AP = 0,
        IARG_PTR_FP,
        IARG_PTR_CP,
        IARG_PTR_SEEK,
        IARG_PTR_GBL,   /* arg2 ignored */
        IARG_PTR_THIS   /* ""   "" */
};

/*
 * CALL_FUNC arg1 enumerations
 *
 * Stack at function call time is:
 *      SP
 *      argN
 *      ...
 *      arg1
 *      arg0
 *      function
 *      parent (if IARG_WITH_PARENT)
 *
 * XXX REVISIT: If we default parent with a swap before evaluating args,
 * then we could use the same great big stack for every frame, just each
 * function has its own FP & AP.
 */
enum {
        IARG_NO_PARENT       = 0,
        IARG_WITH_PARENT     = 1,
};

/* CMP arg1 enumerations */
enum {
        IARG_EQ,
        IARG_LEQ,
        IARG_GEQ,
        IARG_NEQ,
        IARG_LT,
        IARG_GT
};

typedef struct {
        uint8_t code;
        uint8_t arg1;  /* usually an IARG... enum */
        int16_t arg2;  /* usually a data offset, signed */
} instruction_t;

/**
 * struct executable_t - Handle to the actual execution code of a
 *                       function or a script
 * @instr:      Opcode array
 * @rodata:     Constants used by the function
 * @n_instr:    Number of opcodes
 * @n_rodata:   Number of constants
 * @label:      Labels.  Unused at this stage, except to make the
 *              disassembly more readable.
 * @n_label:    Number of labels
 * @file_name:  Name of source file where this was defined
 * @file_line:  Starting line in source file where this was defined
 * @list:       Sibling list
 * @nref:       Reference count. Unlike other objects' reference count,
 *              this one starts at zero, since nothing has a handle to
 *              it.  Garbage collection will only occur if it is
 *              decremented back to zero from one.  (FIXME: Too many ways
 *              to count how this could go wrong.)  Except...
 * @flags:      If FE_TOP is set, delete this after it has been executed
 *              once.  Do not delete anything it added to the symbol
 *              table, since later-executed code may use them.
 */
struct executable_t {
        instruction_t *instr;
        struct var_t **rodata;
        int n_instr;
        int n_rodata;
        unsigned short *label;
        int n_label;
        const char *file_name;
        int file_line;
        struct list_t list;
        int nref;
        unsigned flags;
};

#endif /* EGQ_INSTRUCTIONS_H */
