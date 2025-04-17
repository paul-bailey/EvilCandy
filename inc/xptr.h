#ifndef EVILCANDY_XPTR_H
#define EVILCANDY_XPTR_H

#include "instructions.h"

/*
 * Note: you shouldn't need to include this directly.  Either include
 * evilcandy.h or typedefs.h
 */

struct var_t;

/**
 * struct xptrvar_t - executable code of a function or a script body
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
 * A XptrType var is created for every script and every function
 * definition or lambda within the script.  During assembly, if the
 * source file nests a function inside a parent function, the parent
 * function's XptrType var will have a reference to the nested function's
 * XptrType var in its .rodata--that's how it can dynamically create
 * FuncType vars, say in the case of nested functions.  This creating a
 * sort of tree-type structure (though it isn't intended to be thought of
 * that way).  Thus an XptrType var has a reference produced for as long
 * as its parent XptrType var is still in scope.
 *
 * This is important, because FuncType vars are created and destroyed
 * during **runtime** while XptrType vars are created only during
 * **assembly**.  This creates a garbage-collection hazard.  Consider two
 * scenarios:
 *
 *   1. A function executes a nested function and returns.  The var for
 *      the nested function was on the stack, therefore it goes out of
 *      scope and gets destroyed.  When calling the parent function
 *      again, a new nested function has to be created using the same
 *      XptrType var.
 *
 *   2. A script is imported with the 'x' argument; it returns a
 *      function handle to some part of its code to the calling script.
 *      (Or perhaps it adds one of its functions to the global symbol
 *      table.)  The *parent* XptrType var is no longer in scope and
 *      destroyed, but the function being returned can be called again.
 *
 * This means that one reference needs to be produced for an XptrType var
 * for not only the owning XptrType var that references it in its .rodata,
 * but also for every in-scope FuncType var that uses it.  This makes
 * XptrType vars nearly immortal.
 *
 * An example hierarchy of owning structs, names of variables, variables,
 * and XptrType vars might look like this:
 *
 *     owner_1  owner_2 owner_3 <-- owning objects, "this"
 *        |       |       |
 *      name1   name2   name3   <-- different names or dict keys with
 *        \       |       |         references to same function var
 *         \     /        |
 *         funcvar1   funcvar2
 *               \       /
 *                xptrvar
 */
struct xptrvar_t {
        struct var_t base;
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

/* only serializer.c and assembler.c code should need to use these */
extern int xptr_next_label(struct var_t *v);
extern void xptr_set_label(struct var_t *v, int jmp);
extern int xptr_add_rodata(struct var_t *v, struct var_t *new_data);
extern void xptr_add_instr(struct var_t *v, instruction_t ii);
extern struct var_t *xptrvar_new(const char *file_name, int file_line);

#endif /* EVILCANDY_XPTR_H */
