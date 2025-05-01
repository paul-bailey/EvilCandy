/* vm.h - API with vm.c functions */
#ifndef EVILCANDY_VM_H
#define EVILCANDY_VM_H

#include "instructions.h"

struct block_t {
        Object **stack_level;
        instruction_t *jmpto;
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
        Object *owner, *func;
        Object **stackptr;
        Object **stack;
        struct xptrvar_t *ex;
        int ap;
        int n_blocks;
        struct block_t blocks[FRAME_NEST_MAX];
        instruction_t *ppii;
        Object **clo;
        struct list_t alloc_list;
#ifndef NDEBUG
        bool freed;
#endif
};

/* vm.c */
extern Object *vm_exec_script(Object *top_level, Frame *fr);
extern Object *vm_exec_func(Frame *fr, Object *func, int argc,
                            Object **argv, bool have_dict);
extern void vm_add_global(Object *name, Object *var);
extern Object *vm_get_global(const char *name);
extern bool vm_symbol_exists(Object *key);
static inline Object *vm_get_this(Frame *fr)
        { return fr->owner; }
static inline Object *vm_get_arg(Frame *fr, unsigned int idx)
        { return idx >= fr->ap ? NULL : fr->stack[idx]; }
static inline int vm_get_argc(Frame *fr)
        { return fr->ap; }
/* execute_loop shared between vm.c and function.c, else private */
extern Object *execute_loop(Frame *fr);

/* TODO: Get rid of references to frame_get_arg */
# define frame_get_arg(fr, i)   vm_get_arg(fr, i)
# define get_this(fr)           vm_get_this(fr)

#endif /* EVILCANDY_VM_H */
