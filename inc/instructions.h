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

/**
 * struct executable_t - Handle to the actual execution code of a
 *                       function or a script body
 * @instr:      Opcode array
 * @rodata:     Constants used by the function
 * @n_instr:    Number of opcodes
 * @n_rodata:   Number of constants
 * @label:      Labels.  Unused at execution stage, except to make the
 *              disassembly more readable.
 * @n_label:    Number of labels
 * @file_name:  Name of source file where this was defined
 * @file_line:  Starting line in source file where this was defined
 * @nref:       Reference count. Unlike other objects' reference count,
 *              this one starts at zero, since nothing has a handle to
 *              it.  Garbage collection will only occur if it is
 *              decremented back to zero from one.  (FIXME: Too many ways
 *              to count how this could go wrong.)  Except...
 * @flags:      If FE_TOP is set, delete this after it has been executed
 *              once.  Do not delete anything it added to the symbol
 *              table, since later-executed code may use them.
 * @uuid:       Identifier for the sake of serialization and disassembly.
 *              (Internal pointers have no meaning except when executing.)
 *              This is the text representation, not the binary bitstream.
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
        int nref;
        unsigned flags;
        /*
         * XXX: I'd rather this be uuid_t, but I want to limit the
         * platform dependence and header namespace to the C modules
         * as much as possible.
         */
        char *uuid;
};

/*
 * FIXME: currently executable code has to stay in RAM for the duration
 * of the program.  Consider the exmample:
 *
 *      x.foreach(function(e, s) { ...code... });
 *
 * Because it is anonymously defined in the argument, it will go out of
 * scope after it is removed from x.foreach's argument stack, destroying
 * the code.  The next time an object's .foreach method is called, the
 * program will crash due to the executable code being deleted.
 */
#if 1
# define EXECUTABLE_CLAIM(ex) do { (void)0; } while (0)
# define EXECUTABLE_RELEASE(ex) do { (void)0; } while (0)
#else
# define EXECUTABLE_CLAIM(ex) do { (ex)->nref++; } while (0)
# define EXECUTABLE_RELEASE(ex) do { \
        struct executable_t *ex_ = (ex); \
        ex_->nref--; \
        if (ex_->nref <= 0) \
                executable_free__(ex_); \
} while (0)
#endif

/* in assembler.c */
extern void executable_free__(struct executable_t *ex);

#endif /* EGQ_INSTRUCTIONS_H */
