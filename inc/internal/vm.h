#ifndef EVC_INC_INTERNAL_VM_H
#define EVC_INC_INTERNAL_VM_H

#include <instructions.h>
#include <vm.h>

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
 * @stack_end:  Stack limit.  @stackptr may not exceed this position.
 * @ex:         Executable code being run by this frame
 * @ap:         Number of locals which are the function's arguments.
 *              Used as a safeguard when executing LOADARG opcode.
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
 *
 * Its fields should only be used by vm.c and (for now) types/function.c
 */
struct vmframe_t {
        enum {
                FRAME_NORMAL = 0,
                FRAME_GENERATOR = 1,
        } kind;
        Object *owner, *func;
        Object **stackptr;
        Object **stack;
        Object **stack_end;
        struct xptrvar_t *ex;
        int ap;
        int n_blocks;
        int n_locals;
        struct block_t blocks[FRAME_NEST_MAX];
        instruction_t *ppii;
        Object **clo;
        struct list_t alloc_list;
};

static inline Object *vm_get_arg(Frame *fr, unsigned int idx)
        { return idx >= (unsigned)fr->ap ? NULL : fr->stack[idx]; }
static inline int vm_get_argc(Frame *fr)
        { return fr->ap; }
extern Object *vm_localdict(void);
extern Object *vm_globaldict(void);
extern Object *vm_get_locals(void);

extern Object *execute_loop(Frame *fr);

extern enum result_t vmframe_unpack_args(Frame *fr, int optind,
                                         Object *args, Object *kwargs,
                                         size_t *nr_args);
extern enum result_t vmframe_finish_stack_setup(
                        Frame *fr, struct xptrvar_t *xptr,
                        Object **closures);
extern void vm_clear_frames_for_exit(void);

#endif /* EVC_INC_INTERNAL_VM_H */
