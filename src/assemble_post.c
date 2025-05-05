#include <evilcandy.h>
#include <xptr.h>
#include "assemble_priv.h"

static int
frame_n_rodata(struct as_frame_t *fr)
{
        return as_buffer_ptr_size(&fr->af_rodata);
}

static Object *
try_binop(Object *left, Object *right, int opcode)
{
        binary_operator_t func = NULL;
        switch (opcode) {
        case INSTR_MUL:
                func = qop_mul;
                break;
        case INSTR_POW:
                func = qop_pow;
                break;
        case INSTR_DIV:
                func = qop_div;
                break;
        case INSTR_MOD:
                func = qop_mod;
                break;
        case INSTR_ADD:
                func = qop_add;
                break;
        case INSTR_SUB:
                func = qop_sub;
                break;
        case INSTR_BINARY_AND:
                func = qop_bit_and;
                break;
        case INSTR_BINARY_OR:
                func = qop_bit_or;
                break;
        case INSTR_BINARY_XOR:
                func = qop_xor;
                break;
        /*
         * XXX These either take args or are otherwise do not have pure
         * qop_XXX functions, but that's no reason we can't support them
         */
        case INSTR_LOGICAL_OR:
        case INSTR_LOGICAL_AND:
        case INSTR_LSHIFT:
        case INSTR_RSHIFT:
        case INSTR_CMP:
        default:
                /* default, not a binary operator */
                return NULL;
        }

        return func(left, right);
}

static void
shift_down_labels(short *labels, int n_labels, int after, int amount)
{
        int i;
        for (i = 0; i < n_labels; i++) {
                if (labels[i] > after)
                        labels[i] -= amount;
        }
}

static void
shift_out_nops(struct assemble_t *a, struct as_frame_t *fr)
{
        int i, n_instr;
        int n_labels = as_frame_nlabel(fr);
        short *labels = (short *)fr->af_labels.s;
        struct buffer_t *b = &fr->af_instr;
        instruction_t *idata = (instruction_t *)b->s;

        i = 0;
        while (i < (n_instr = as_frame_ninstr(fr))) {
                if (idata[i].code != INSTR_NOP) {
                        i++;
                        continue;
                }
                int amount, after, movsize;
                after = i;
                do {
                        i++;
                } while (i < n_instr && idata[i].code == INSTR_NOP);
                amount = i - after;

                shift_down_labels(labels, n_labels, after, amount);

                movsize = (n_instr - i) * sizeof(instruction_t);
                bug_on(movsize < 0);
                if (movsize > 0)
                        memmove(&idata[after], &idata[i], movsize);

                /* XXX No! low-level manipulation of buffer! */
                b->p -= amount * sizeof(instruction_t);
        }
}

static int
seek_rodata(struct assemble_t *a, struct as_frame_t *fr, Object *obj)
{
        int ret;
        struct as_frame_t *frsav = a->fr;
        a->fr = fr;
        ret = assemble_seek_rodata(a, obj);
        VAR_DECR_REF(obj);
        a->fr = frsav;
        return ret;
}

static void
reduce_const_operators_(struct assemble_t *a, struct as_frame_t *fr)
{
        bool reduced, reduced_once;
        instruction_t *idata = (instruction_t *)fr->af_instr.s;
        Object **rodata = (Object **)fr->af_rodata.s;

        reduced_once = false;
        do {
                int i = 0;
                int n_instr = as_frame_ninstr(fr);
                reduced = false;
                while (i < (n_instr - 2)) {
                        /* XXX Am I sure this doesn't straddle a label? */
                        if (idata[i].code == INSTR_LOAD_CONST &&
                            idata[i+1].code == INSTR_LOAD_CONST) {
                                Object *left, *right, *result;

                                left = rodata[idata[i].arg2];
                                right = rodata[idata[i+1].arg2];
                                result = try_binop(left, right,
                                                   idata[i+2].code);

                                /*
                                 * NULL means either it wasn't a bin-op or
                                 * an error occurred.  Suppress errors for
                                 * now, this could be in a try/catch statement.
                                 */
                                if (result == NULL) {
                                        err_clear();
                                        i++;
                                        continue;
                                }

                                idata[i].arg2 = seek_rodata(a, fr, result);
                                idata[i+1].code = INSTR_NOP;
                                idata[i+2].code = INSTR_NOP;
                                i += 3;
                                reduced = true;
                        } else {
                                i++;
                        }
                }

                if (reduced) {
                        shift_out_nops(a, fr);
                        reduced_once = true;
                }
        } while (reduced);

        if (reduced_once) {
                /*
                 * TODO: This is a placeholder for where we shift out
                 * no longer needed .rodata.  But to do that safely, we
                 * need a more automated way of determining which
                 * op-codes would be affected than we have now.  Until
                 * then, there will likely be some unused .rodata, which
                 * may confuse people looking at the disassembly.
                 */
                (void)reduced_once;
        }
}

static void
reduce_const_operators(struct assemble_t *a)
{
        struct list_t *li;
        list_foreach(li, &a->finished_frames) {
                struct as_frame_t *fr = list2frame(li);
                reduce_const_operators_(a, fr);
        }
}

static void
resolve_jump_labels(struct assemble_t *a, struct as_frame_t *fr)
{
        int i, n_instr, n_label;
        short *labels;
        instruction_t *instrs;
        struct as_frame_t *frsav;

        labels  = (short *)fr->af_labels.s;
        n_label = as_frame_nlabel(fr);

        instrs = (instruction_t *)fr->af_instr.s;
        n_instr = as_frame_ninstr(fr);

        frsav = a->fr;
        a->fr = fr;

        for (i = 0; i < n_instr; i++) {
                instruction_t *ii = &instrs[i];
                if (ii->code == INSTR_B
                    || ii->code == INSTR_B_IF
                    || ii->code == INSTR_FOREACH_ITER
                    || ii->code == INSTR_PUSH_BLOCK) {
                        int arg2 = ii->arg2;

                        if (ii->code == INSTR_PUSH_BLOCK
                            && ii->arg1 == IARG_BLOCK) {
                                /* ignore labels for this one */
                                continue;
                        }

                        bug_on(arg2 >= n_label);
                        /*
                         * minus one because pc will have already
                         * been incremented.
                         */
                        ii->arg2 = labels[arg2] - i - 1;
                        continue;
                }
        }
        a->fr = frsav;
}

static struct as_frame_t *
func_label_to_frame(struct assemble_t *a, int funcno)
{
        struct list_t *li;
        list_foreach(li, &a->finished_frames) {
                struct as_frame_t *sib = list2frame(li);
                if (sib->funcno == funcno)
                        return sib;
        }
        bug();
        return NULL;
}

static struct xptrvar_t *
frame_to_xptr(struct assemble_t *a, struct as_frame_t *fr)
{
        struct xptrvar_t *x;
        instruction_t *instrs;
        int i, n_instr;

        instrs = (instruction_t *)fr->af_instr.s;
        n_instr = as_frame_ninstr(fr);
        for (i = 0; i < n_instr; i++) {
                instruction_t *ii = &instrs[i];
                if (ii->code == INSTR_DEFFUNC) {
                        struct as_frame_t *child;
                        struct xptrvar_t *x;

                        child = func_label_to_frame(a, ii->arg2);
                        bug_on(!child || child == fr);

                        x = frame_to_xptr(a, child);

                        ii->arg2 = seek_rodata(a, fr, (Object *)x);
                }
        }

        do {
                struct xptr_cfg_t cfg;
                cfg.file_name   = a->file_name;
                cfg.file_line   = fr->line;
                cfg.n_label     = as_frame_nlabel(fr);
                cfg.n_rodata    = frame_n_rodata(fr);
                cfg.n_instr     = as_frame_ninstr(fr);
                cfg.label       = buffer_trim(&fr->af_labels);
                cfg.rodata      = buffer_trim(&fr->af_rodata);
                cfg.instr       = buffer_trim(&fr->af_instr);
                x = (struct xptrvar_t *)xptrvar_new(&cfg);
        } while (0);

        return x;
}

/* resolve local jump addresses */
struct xptrvar_t *
assemble_post(struct assemble_t *a)
{
        struct list_t *li;

        /*
         * TODO: Right here we can find any instance of
         * INSTR_LOAD_CONST + INSTR_LOAD_CONST + binary op, or
         * INSTR_LOAD_CONST + unary op, execute it here, reduce
         * the instruction set and number of runtime operations.
         * We'll need to do that while the labels are still in
         * fr->af_labels instead of the instructions or it will
         * be a lot harder.
         */
        reduce_const_operators(a);

        list_foreach(li, &a->finished_frames) {
                struct as_frame_t *fr = list2frame(li);
                resolve_jump_labels(a, fr);
        }

        /*
         * See as_frame_pop().
         * First child of finished_frames is also our entry point.
         */
        return frame_to_xptr(a, list2frame(a->finished_frames.next));
}


