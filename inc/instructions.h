#ifndef EGQ_INSTRUCTIONS_H
#define EGQ_INSTRUCTIONS_H

#include "instruction_defs.h"
#include <lib/list.h>
#include <stdint.h>
#include <stdbool.h>

/* PUSH_PTR/PUSH_COPY arg1 enumerations */
enum {
        IARG_PTR_AP = 0,
        IARG_PTR_FP,
        IARG_PTR_CP,
        IARG_PTR_THIS   /* ""   "" */
};

/* CMP arg1 enumerations */
enum {
        IARG_EQ,
        IARG_LEQ,
        IARG_GEQ,
        IARG_NEQ,
        IARG_LT,
        IARG_GT,
        IARG_HAS,
        IARG_EQ3,
        IARG_NEQ3,
};

/* PUSH/POP_BLOCK args */
enum {
        IARG_BLOCK,
        IARG_LOOP,
        IARG_CONTINUE,
        IARG_TRY,
};

/* FUNC_SETATTR args */
enum {
        IARG_FUNC_MINARGS,
        IARG_FUNC_MAXARGS,
        IARG_FUNC_OPTIND,
        IARG_FUNC_KWIND,
};

/* CALL arg1 (needed because func could be variadic) */
enum {
        IARG_NO_DICT,
        IARG_HAVE_DICT,
};

enum {
        IARG_POP_NORMAL,
        IARG_POP_PRINT, /* print me if in interactive */
};

/*
 * B_IF arg1.  These are flags, not sequential enums
 * There used to be more, but I got better at this.
 */
enum {
        /* mask of boolean condition for jumping */
        IARG_COND_COND = 1,
        /*
         * Push condition back onto stack before jumping.  This makes for
         * confusing disassembly (and ultimately more B_IF instructions
         * for any given condition expression), therefore, if configured
         * to do so, assembe_post.c will remove this and change the jump
         * instruction as necessary.
         */
        IARG_COND_SAVEF = 2,
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

/*
 * XXX: At the cost of turning one instruction into two, we could change
 * some of these to LOAD_CONST + instruction, so that only LOAD_CONST
 * uses rodata.  It's a performance hit, but it's way less error-prone
 * whenever we're making changes to time to the instruction set.
 */
static inline bool
instr_uses_rodata(instruction_t ii)
{
        switch (ii.code) {
        case INSTR_LOAD_CONST:
        case INSTR_NEW_GLOBAL:
        case INSTR_LOAD_GLOBAL:
        case INSTR_ASSIGN_GLOBAL:
                return true;
        default:
                return false;
        }
}

static inline bool
instr_uses_jump(instruction_t ii)
{
        switch (ii.code) {
        case INSTR_B:
        case INSTR_B_IF:
        case INSTR_FOREACH_ITER:
                return true;
        case INSTR_PUSH_BLOCK:
                return ii.arg1 != IARG_BLOCK;
        default:
                return false;
        }
}

#endif /* EGQ_INSTRUCTIONS_H */
