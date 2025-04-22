/* vm.h - API with vm.c functions */
#ifndef EVILCANDY_VM_H
#define EVILCANDY_VM_H

#include "instructions.h"

struct block_t {
        struct var_t **stack_level;
        unsigned char type;
};

/**
 * struct vmframe_t - VM's per-function frame.
 * @owner:      'this', as user code sees it
 * @func:       Handle to the function being executed
 * @stackptr:   Current stack position
 * @stack:      Base of the frame stack, which actually points into a
 *              shared global stack.
 * @ex:         Executable code being run by this frame
 * @ap:         Array offset from @stack where arguments end.  This is
 *              the start of the evaluation stack, where local variables
 *              are stored and temporary variables are manipulated for
 *              evalutation.
 * @n_blocks:   Number of blocks currently being used by this frame
 * @blocks:     Blocks used by this frame.  One is taken (and @n_blocks
 *              increases by one) every time we descend into a block-type
 *              program-flow statement
 * @ppii:       Pointer to the next instruction to execute.
 * @clo:        Closures.  These are 'borrowed' from @func, so we do not
 *              consume or any references to them when the frame is
 *              deconstructed
 * @alloc_list: Used for some memory-management bookkeepping.  See
 *              comments above vmframe_alloc/vmframe_free in vm.c
 * @freed:      Sanity checker, used only on debug builds.
 *
 * Its fields should only be used by vm.c and (for now) types/function.c
 */
struct vmframe_t {
        struct var_t *owner, *func;
        struct var_t **stackptr;
        struct var_t **stack;
        struct xptrvar_t *ex;
        int ap;
        int n_blocks;
        struct block_t blocks[FRAME_NEST_MAX];
        instruction_t *ppii;
        struct var_t **clo;
        struct list_t alloc_list;
#ifndef NDEBUG
        bool freed;
#endif
};

/* vm.c */
extern struct var_t *vm_exec_script(struct var_t *top_level,
                                struct vmframe_t *fr);
extern struct var_t *vm_exec_func(struct vmframe_t *fr, struct var_t *func,
                                struct var_t *owner, int argc,
                                struct var_t **argv);
extern void vm_add_global(struct var_t *name, struct var_t *var);
static inline struct var_t *vm_get_this(struct vmframe_t *fr)
        { return fr->owner; }
static inline struct var_t *vm_get_arg(struct vmframe_t *fr, unsigned int idx)
        { return idx >= fr->ap ? NULL : fr->stack[idx]; }
static inline int vm_get_argc(struct vmframe_t *fr)
        { return fr->ap; }
/* execute_loop shared between vm.c and function.c, else private */
extern struct var_t *execute_loop(struct vmframe_t *fr);

/* TODO: Get rid of references to frame_get_arg */
# define frame_get_arg(fr, i)   vm_get_arg(fr, i)
# define get_this(fr)           vm_get_this(fr)

#endif /* EVILCANDY_VM_H */
