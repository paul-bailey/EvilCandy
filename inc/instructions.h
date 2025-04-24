#ifndef EGQ_INSTRUCTIONS_H
#define EGQ_INSTRUCTIONS_H

#include "instruction_defs.h"
#include <lib/list.h>
#include <stdint.h>

/* GETATTR, SETATTR, arg1 enumerations */
enum {
        /* do not confuse with IARG_FLAG_CONST! */
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

/* CMP arg1 enumerations */
enum {
        IARG_EQ,
        IARG_LEQ,
        IARG_GEQ,
        IARG_NEQ,
        IARG_LT,
        IARG_GT
};

/*
 * ASSIGN, ADDATTR arg1 enumerations
 * (these are flags, not a sequence)
 */
enum {
        IARG_FLAG_CONST = 0x01,
        /* ADDATR only */
        IARG_FLAG_PRIV = 0x02,
};

/*
 * PUSH/POP_BLOCK args
 */
enum {
        IARG_BLOCK,
        IARG_LOOP,
};

/**
 * DOC: Instructions
 *
 * Executable byte code is found in an array of 32-bit values of type
 * instruction_t.  Its bitmap fields are:
 *      @code:  An 8-bit INSTR_xxx enum, defined in instruction_defs.h,
 *              which was auto-generated from the original source,
 *              tools/instructions.  The VM uses this to jump into its
 *              instruction-callback lookup table.
 *      @arg1:  An 8-bit first argument, usually an IARG... enum
 *      @arg2:  A 16-bit signed second argument, usually a data offset
 *              from a starting point defined by @arg1
 *
 * XXX REVISIT: Many instructions do not use args.  We could compress
 * this array by setting opcode values such that a macro can decide
 * whether the next byte is an argument or it is another opcode.  The
 * trade-off is the additional 'if', and unpacking a potentially
 * unaligned 16-bit value.  The advantage is a smaller memory footprint,
 * therefore fewer cache misses.
 */
typedef struct {
        uint8_t code;
        uint8_t arg1;
        int16_t arg2;
} instruction_t;

#endif /* EGQ_INSTRUCTIONS_H */
