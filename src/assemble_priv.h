#ifndef ASSEMBLE_PRIV_H
#define ASSEMBLE_PRIV_H

#include <evilcandy.h>
#include <setjmp.h>

/**
 * struct as_frame_t - Temporary frame during assembly
 * @funcno:      Temporary magic number identifying this during the first
 *               pass before jump labels are resolved.
 * @af_locals:   Symbol table of stack variables.
 * @fp:          Index into @af_locals defining current scope
 * @af_args:     Symbol table of argument names, in order of argument
 * @af_closures: Symbol table of closure names
 * @af_rodata:   ie. a function's consts, array of Object *
 * @af_labels:   Jump labels, array of short ints
 * @af_instr;    Instructions, array of instruction_t
 * @scope:       Current {...} scope within the function
 * @nest:        Pointer to current top of @scope
 * @line:        Line number of first line of code for this frame
 * @list:        Link to sibling frames
 *
 * This wraps @x (the true intended result of this assembly, and will
 * be thrown away when we're done, leaving only @x remaining.
 *
 * One of these frames is allocated for each function, and one for the
 * top-level script.  Internal scope (if, while, anything in a {...}
 * block) is managed by scope[].
 */
struct as_frame_t {
        long long funcno;
        struct buffer_t af_locals;
        int fp;
        struct buffer_t af_args;
        struct buffer_t af_closures;
        struct buffer_t af_rodata;
        struct buffer_t af_labels;
        struct buffer_t af_instr;
        int scope[FRAME_NEST_MAX];
        int nest;
        int line;
        struct list_t list;
};

/**
 * struct assemble_t - The top-level assembler, contains all the
 *                     function definitions in the same source file.
 * @prog:       The token state machine
 * @oc:         Pointer into current parsed token in @prog
 * @func:       Label number for next function
 * @env:        Buffer to longjmp from in case of error
 * @active_frames:
 *              Linked list of frames that have not been fully parsed.
 *              Because functions can be declared and defined in the
 *              middle of wrapper functions, this is not necessarily
 *              size one.
 * @finished:frames:
 *              Linked list of frames that have been fully parsed.
 * @fr:         Current active frame, should be last member of
 *              @active frames
 */
struct assemble_t {
        char *file_name;
        FILE *fp;
        struct token_state_t *prog;
        struct token_t *oc;
        int func;
        jmp_buf env;
        struct list_t active_frames;
        struct list_t finished_frames;
        struct as_frame_t *fr;
};

#define list2frame(li) container_of(li, struct as_frame_t, list)

static inline int as_buffer_ptr_size(struct buffer_t *b)
        { return buffer_size(b) / sizeof(void *); }

static inline Object **as_frame_rodata(struct as_frame_t *fr)
        { return (Object **)fr->af_rodata.s; }

static inline int as_frame_nconst(struct as_frame_t *fr)
        { return as_buffer_ptr_size(&fr->af_rodata); }

static inline int as_frame_ninstr(struct as_frame_t *fr)
        { return buffer_size(&fr->af_instr) / sizeof(instruction_t); }

static inline int as_frame_nlabel(struct as_frame_t *fr)
        { return buffer_size(&fr->af_labels) / sizeof(short); }

/* assemble.c */
extern int assemble_seek_rodata(struct assemble_t *a, Object *v);
extern void assemble_label_here(struct assemble_t *a);
extern ssize_t assemble_get_line(struct assemble_t *a,
                                 struct token_t *tok, size_t max,
                                 int *lineno);
extern void assemble_frame_push(struct assemble_t *a, long long funcno);
extern void assemble_frame_pop(struct assemble_t *a);
extern void assemble_add_instr(struct assemble_t *a, int opcode,
                               int arg1, int arg2);
extern int assemble_frame_next_label(struct as_frame_t *fr);
extern void assemble_frame_set_label(struct as_frame_t *fr,
                                     int jmp, unsigned long val);

/* assemble_post.c */
extern struct xptrvar_t *assemble_post(struct assemble_t *a);
extern struct xptrvar_t *assemble_frame_to_xptr(struct assemble_t *a,
                                                struct as_frame_t *fr);

/* reassemble.c */
extern struct xptrvar_t *reassemble(struct assemble_t *a);

#endif /* ASSEMBLE_PRIV_H */
