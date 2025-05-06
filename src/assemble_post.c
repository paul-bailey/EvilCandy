/*
 * XXX  This file bravely makes the assumption that 'optimize' and
 *      'front-load' mean the same thing.  This will make for a sluggish
 *      load time, unless we get a good serialization scheme working, so
 *      that this stuff only happens when a byte-code file doesn't exist.
 */
#include <evilcandy.h>
#include <xptr.h>
#include "assemble_priv.h"

/*
 * FIXME: These two functions ought to be automated in some way from
 * tools/instructions and tools/gen.  If I change up the instructions
 * these could end up being wrong.
 */
static bool
instr_uses_rodata(instruction_t ii)
{
        switch (ii.code) {
        case INSTR_LOAD_CONST:
        case INSTR_SYMTAB:
        case INSTR_DEFFUNC:
                return true;
        case INSTR_LOAD:
        case INSTR_ASSIGN:
                return ii.arg1 == IARG_PTR_SEEK;
        default:
                return false;
        }
}

static bool
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

/*
 * If we simplified some operations on consts, then some .rodata may no
 * longer be necessary.  If so, this garbage collects that and adjust
 * instructions' .rodata offsets as necessary.
 */
static void
remove_unused_rodata(struct as_frame_t *fr)
{
        enum {
                DEFAULT_NCONST = 32,
                DEFAULT_NINSTR = 32,
        };
        int16_t **iptrs;
        char *marks;
        instruction_t *idata;
        int i, n_rodata, n_instr;
        Object **rodata;
        char stack_marks[DEFAULT_NCONST];
        int16_t *stack_iptrs[DEFAULT_NINSTR];

        n_instr = as_frame_ninstr(fr);
        if (n_instr <= DEFAULT_NINSTR)
                iptrs = stack_iptrs;
        else
                iptrs = emalloc(n_instr * sizeof(void *));

        n_rodata = as_frame_nconst(fr);
        if (n_rodata <= DEFAULT_NCONST)
                marks = stack_marks;
        else
                marks = emalloc(n_rodata);

        memset(iptrs, 0, n_instr * sizeof(void *));
        memset(marks, 0, n_rodata);
        idata = (instruction_t *)fr->af_instr.s;

        /* set this at start to make things a little faster */
        for (i = 0; i < n_instr; i++) {
                if (instr_uses_rodata(idata[i])) {
                        int16_t *arg = &idata[i].arg2;
                        bug_on(*arg < 0 || *arg >= n_rodata);
                        marks[*arg] = 1;
                        iptrs[i] = arg;
                }
        }

        rodata = as_frame_rodata(fr);
        for (i = n_rodata - 1; i >= 0; i--) {
                int j;

                /* Skip checks for xptr, we know it's sitll needed */
                if (isvar_xptr(rodata[i]))
                        continue;
                if (marks[i] != 0)
                        continue;

                /* no one needs us */
                VAR_DECR_REF(rodata[i]);

                /* Point affected instructions down one index */
                for (j = 0; j < n_instr; j++) {
                        int16_t *arg2 = iptrs[j];
                        if (arg2 && *arg2 > i)
                                *arg2 -= 1;
                }

                /* Move rodata down one */
                for (j = i; j < n_rodata - 1; j++) {
                        rodata[j] = rodata[j + 1];
                }
                n_rodata--;
        }

        /* so dirty... */
        fr->af_rodata.p = n_rodata * sizeof(Object *);

        if (iptrs != stack_iptrs)
                efree(iptrs);
        if (marks != stack_marks)
                efree(marks);
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
remove_nop_instructions(struct assemble_t *a, struct as_frame_t *fr)
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
                int j, amount, after, movsize;

                after = i;
                do {
                        i++;
                } while (i < n_instr && idata[i].code == INSTR_NOP);
                amount = i - after;

                /* shift down labels */
                for (j = 0; j < n_labels; j++) {
                        if (labels[j] > after)
                                labels[j] -= amount;
                }

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

/* reduce_const_operands but for just one function */
static void
reduce_const_operands_(struct assemble_t *a, struct as_frame_t *fr)
{
        bool reduced, reduced_once;
        int n_instr;
        instruction_t *idata = (instruction_t *)fr->af_instr.s;
        Object **rodata = as_frame_rodata(fr);

        reduced_once = false;
        n_instr = as_frame_ninstr(fr);
        do {
                instruction_t *ip = idata;
                instruction_t *stop = ip + n_instr;

                reduced = false;
                for (ip = idata; ip < stop; ip++) {
                        instruction_t *ip2, *ip3;
                        Object *left, *right, *result;

                        if (ip->code != INSTR_LOAD_CONST)
                                continue;

                        ip2 = ip+1;
                        while (ip2->code == INSTR_NOP && ip2 < stop - 1)
                                ip2++;
                        if (ip2 >= stop - 1 || ip2->code != INSTR_LOAD_CONST)
                                continue;

                        ip3 = ip2+1;
                        while (ip3 < stop && ip3->code == INSTR_NOP)
                                ip3++;
                        if (ip3 == stop)
                                break;

                        left = rodata[ip->arg2];
                        right = rodata[ip2->arg2];
                        result = try_binop(left, right, ip3->code);

                        /*
                         * NULL means either ip3 wasn't a bin-op or an
                         * error occurred.  Suppress errors for now, this
                         * could be in a try/catch statement.
                         *
                         * XXX if error, this will be repeated until
                         * entire scan runs through without setting
                         * @reduced below.
                         */
                        if (result == NULL) {
                                err_clear();
                                continue;
                        }

                        ip->arg2 = seek_rodata(a, fr, result);
                        ip2->code = INSTR_NOP;
                        ip3->code = INSTR_NOP;
                        /* iterator will update to ip3+1 */
                        ip = ip3;
                        reduced = true;
                        reduced_once = true;
                }

        } while (reduced);

        if (reduced_once) {
                remove_nop_instructions(a, fr);
                remove_unused_rodata(fr);
        }
}

/*
 * For any A operator B where A and B are both consts, perform the
 * operation here and reduce it to just a single LOAD_CONST instruction
 */
static void
reduce_const_operands(struct assemble_t *a)
{
        struct list_t *li;
        list_foreach(li, &a->finished_frames) {
                struct as_frame_t *fr = list2frame(li);
                reduce_const_operands_(a, fr);
        }
}

/*
 * Jump instructions' arg2 currently holds a label number.
 * Convert that into an offset from the program counter.
 */
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
                if (instr_uses_jump(instrs[i])) {
                        instruction_t *ii = &instrs[i];
                        bug_on(ii->arg2 >= n_label);
                        /*
                         * label holds a positive offset from start of
                         * instructions. We want signed offset from
                         * current pc.  The '-1' is because the pc will
                         * have already incremented by the time this
                         * instruction is processed.
                         */
                        ii->arg2 = labels[ii->arg2] - i - 1;
                }
        }
        a->fr = frsav;
}

static struct as_frame_t *
func_label_to_frame(struct assemble_t *a, long long funcno)
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

/**
 * assemble_frame_to_xptr - Resolve XptrType pointers in .rodata, create
 *                          final XptrType objects, and return entry-point
 *                          XptrType object.
 * @fr: Entry-level frame.  We cannot determine it from @a here, because
 *      it may be set differently between assemble() and reassemble().
 * @id_in_arg:  - If true, then function ID is a small number in DEFDICT
 *                instructions' arg2, and .rodata does not yet have an
 *                entry for this instruction
 *              - If false, then the offset is set already, but .rodata
 *                contains an IdType var instead of an XptrType.
 *
 */
struct xptrvar_t *
assemble_frame_to_xptr(struct assemble_t *a,
                       struct as_frame_t *fr, bool id_in_arg)
{
        struct xptrvar_t *x;
        instruction_t *instrs;
        int i, n_instr;

        /*
         * Resolve any nested function defintions from a magic number to
         * a pointer to another XptrType object.  This means that we have
         * to process the most deeply nested functions first, hence the
         * recursion.  No need for a recursion-count check, these cannot
         * be any deeper than what assembler.c checked for already.
         */
        instrs = (instruction_t *)fr->af_instr.s;
        n_instr = as_frame_ninstr(fr);
        for (i = 0; i < n_instr; i++) {
                instruction_t *ii = &instrs[i];
                if (ii->code != INSTR_DEFFUNC)
                        continue;

                if (id_in_arg) {
                        struct xptrvar_t *x;
                        struct as_frame_t *child;
                        child = func_label_to_frame(a, ii->arg2);
                        bug_on(!child || child == fr);

                        x = assemble_frame_to_xptr(a, child, id_in_arg);
                        ii->arg2 = seek_rodata(a, fr, (Object *)x);
                } else {
                        struct as_frame_t *child;
                        Object **rodata;
                        long long idval;

                        bug_on(as_frame_nconst(fr) <= ii->arg2);
                        rodata = as_frame_rodata(fr);
                        idval = idvar_toll(rodata[ii->arg2]);

                        child = func_label_to_frame(a, idval);
                        bug_on(!child || child == fr);

                        VAR_DECR_REF(rodata[ii->arg2]);
                        rodata[ii->arg2] = (Object *)assemble_frame_to_xptr(a,
                                                        child, id_in_arg);
                }
        }

        do {
                struct xptr_cfg_t cfg;
                cfg.file_name   = a->file_name;
                cfg.file_line   = fr->line;
                cfg.n_label     = as_frame_nlabel(fr);
                cfg.n_rodata    = as_frame_nconst(fr);
                cfg.n_instr     = as_frame_ninstr(fr);
                cfg.label       = buffer_trim(&fr->af_labels);
                cfg.rodata      = buffer_trim(&fr->af_rodata);
                cfg.instr       = buffer_trim(&fr->af_instr);
                x = (struct xptrvar_t *)xptrvar_new(&cfg);
        } while (0);

        return x;
}

/**
 * assemble_post - Helper function for assemble()
 *
 * All the opcodes have been compiled.  Still to do...
 * 1. If any binary operators perform on two consts, perform them here
 *    and reduce three instructions to a single LOAD_CONST.
 * 2. Garbage-collect any .rodata that the above procedure rendered
 *    no longer necessary.
 * 3. Resolve local jump addresses
 * 4. Convert it all into a tree of XptrType objects, with the entry
 *    point at the top.
 */
struct xptrvar_t *
assemble_post(struct assemble_t *a)
{
        struct list_t *li;

        reduce_const_operands(a);

        list_foreach(li, &a->finished_frames) {
                struct as_frame_t *fr = list2frame(li);
                resolve_jump_labels(a, fr);
        }

        /*
         * See as_frame_pop().
         * First child of finished_frames is also our entry point.
         */
        return assemble_frame_to_xptr(a,
                        list2frame(a->finished_frames.next), true);
}


