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
 * @nref:       Reference count, for garbage collection
 * @uuid:       Identifier for the sake of serialization and disassembly.
 *              (Internal pointers have no meaning except when executing.)
 *              This is the text representation, not the binary bitstream.
 *
 *      Instantiation:
 *
 * A struct excutable_t is created for every script and every function
 * definition or lambda within the script.  These are different from
 * TYPE_FUNCTION variables; TYPE_FUNCTION variables are instantiated
 * during **runtime**, and may contain their own metadata such as
 * closures and defaults.  struct executable_t's are instantiated during
 * **assembly time**, since any instantiation of a function built from
 * the same definition in the script will have the same instruction set.
 * An example hierarchy of owning structs, names of variables, variables,
 * and struct executable_t's might look like this:
 *
 *      owner_1 owner_2 owner_3 <-- owning objects, "this"
 *        |       |       |
 *      name1   name2   name3   <-- different names or dict keys with
 *        \       |       |         references to same function var
 *         \     /        |
 *           var1       var2    <-- TYPE_FUNCTION variables using
 *               \       /          the same executable code
 *              struct executable_t
 *
 *      TODO: The following remarks mostly justify how to serialize.
 *            When serializer is written, move these comments there.
 *
 *      Referencing:
 *
 * The struct executable_t for the script will have pointers in its
 * .rodata field to the other struct executable_t's (unless the script
 * contains no functions), creating a network of singly-linked struct
 * executables.
 *
 * These .rodata entries are for _definitions_, not _calls_. eg. any
 * time the assembler encounters a "function" keyword or lambda "``"
 * token it sets up a "create function var" bunch of instructions,
 * DEFFUNC, ADD_CLOSURE, ADD_DEFAULT... where the DEFFUNC has an argument
 * to its opcode meaning "the struct executable for it is at this address
 * in .rodata".
 *
 * For calls to code from *outside* the script, (such as when a script
 * is a loaded module), the module would need to have made their
 * functions in some way accessible in the global namespace, so the
 * .rodata entries for each call is a string.  For calls *within* the
 * script, they may refer to it by name if the function is global or
 * by calling a function variable on the stack if the function is
 * nested.
 *
 * This means that for every struct executable_t that exists,
 *      * just one .rodata pointer to this struct exists anywhere
 *        (except with the code for scripts outside of functions,
 *        which have zero such pointers).
 *      * any number of .rodata strings could exist refering to
 *        TYPE_FUNCTION variables that use this struct
 *      * any number of struct var_t's could also have a pointer
 *        to the executable in runtime, which is not a concern for
 *        serialization.
 *
 * So when serializing a struct executable_t to a binary byte-code
 * file, we can print it, all the descendants it refers to, all the
 * descendants those descendants refer to, etc., to a file without
 * worrying about duplicating the same code everywhere.
 *
 *      Garbage Collection:
 *
 * Even though these are all related by a pointer network as mentioned,
 * they could be used by more than one instantiation of a function,
 * either because a variable was copied the old fashioned way
 * ("let x=y;"), or because a variable is a lambda which possibly
 * contains its own set of closures.  So the "network" is only meaningful
 * for the sake of serialization; we cannot garbage-collect this whole
 * group just because a script has concluded, since a calling script may
 * still have access to some of its functions via the global namespace.
 * Function variables' destructor functions must use the
 * EXECUTABLE_RELEASE macro rather than delete it directly.
 *      (XXX 4/2025, GC does not work, because IIFEs will get deleted
 *      before they are called.)
 */
struct executable_t {
        /* hot items used by VM */
        instruction_t *instr;
        struct var_t **rodata;
        /* warm items */
        int n_instr;
        int n_rodata;
        /* cold items used by disassembly and serializer */
        unsigned short *label;
        int n_label;
        const char *file_name;
        int file_line;
        int nref;
        /*
         * XXX: I'd rather this be uuid_t, but I want to limit the
         * platform dependence and header namespace to the C modules
         * as much as possible.
         *
         * XXX: Overkill?  These only have to be unique **per file**
         * Can it not just be an incrementing integer when serializing?
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
