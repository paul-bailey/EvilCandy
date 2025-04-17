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
extern void vm_add_global(const char *name, struct var_t *var);
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
