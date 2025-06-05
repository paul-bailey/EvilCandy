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
try_binop(Object *left, Object *right, instruction_t *ii)
{
        binary_operator_t func = NULL;
        switch (ii->code) {
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
        case INSTR_LSHIFT:
                func = qop_lshift;
                break;
        case INSTR_RSHIFT:
                func = qop_rshift;
                break;
        case INSTR_CMP:
            {
                bool cmp = var_compare_iarg(left, right, ii->arg1);
                return intvar_new((int)cmp);
            }
        default:
                /* default, not a binary operator */
                return NULL;
        }

        return func(left, right);
}

static Object *
try_unaryop(Object *v, instruction_t *ii)
{
        unary_operator_t func = NULL;
        switch (ii->code) {
        case INSTR_BITWISE_NOT:
                func = qop_bit_not;
                break;
        case INSTR_NEGATE:
                func = qop_negate;
                break;
        case INSTR_LOGICAL_NOT:
                func = var_lnot;
                break;
        default:
                return NULL;
        }
        return func(v);
}

static void
remove_nop_instructions(struct assemble_t *a, struct as_frame_t *fr)
{
        int i, n_instr;
        int n_labels = as_frame_nlabel(fr);
        short *labels = (short *)fr->af_labels.s;
        struct buffer_t *b = &fr->af_instr;
        instruction_t *idata = (instruction_t *)b->s;

        /*
         * FIXME: The sum-total lengths in the memmove calls will be
         * shorter if we do this from the top instead of the bottom.
         */
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
                i = after;
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
replace_fake_instructions(struct assemble_t *a, struct as_frame_t *fr)
{
        /* Currently nothing to do */
        ;
}

static instruction_t *
next_instr(instruction_t *ii)
{
        /* Don't need an end because INSTR_END terminates the array */
        do {
                ii++;
        } while (ii->code == INSTR_NOP);
        return ii;
}

static instruction_t *
prev_instr(instruction_t *base, instruction_t *ii)
{
        /* shouldn't pass base to this function */
        bug_on(ii == base);
        do {
                ii--;
        } while (ii->code == INSTR_NOP && ii > base);
        return ii;
}

/* start <= NOP < end, end is exclusive */
static void
nopify(instruction_t *start, instruction_t *end)
{
        while (start < end) {
                start->code = INSTR_NOP;
                start = next_instr(start);
        }
}

/*
 * If there's a bug in this file it's probably here.  Disabling this
 * function means that there may be more jump instructions and a lot of
 * unreachable code that won't get deleted.  Unreachable code is not the
 * worst thing in the world; there are likely too few instructions for
 * locality of reference to take a hit.
 *
 * FIXME: This can be and ought to be greatly simplified now, because I
 * no longer need to delete blocks of code; instead I just need to reduce
 * these to unconditional branches where appropriate.
 *
 * Need to consider the following scenarios
 *      instr           flags   action
 *                      ds  c
 * A    LOAD_CONST          c   Delete both these instructions; delete the
 *      B_IF            10  c   part we're skipping past.
 *
 * B    LOAD_CONST          c   Delete both these instructions; if command
 *      B_IF            10 !c   before jump location is unconditional jump,
 *                              Delete all of that jump's skipped code.
 *
 * C    LOAD_CONST          c   Change to unconditional jump B
 *      B_IF            00  c
 *
 * D    LOAD_CONST          c   Delete both these instructions, let instr's
 *      B_IF            00 !c   fall through.
 *
 * E1   LOAD_CONST          c   Delete block skipped over, but do not delete
 *      B_IF (1b)       11  c   these instrs.
 *      ...
 *  1b: ...
 *
 * E2   LOAD_CONST          c   If last instr in not-skipped block is B,
 *      B_IF (1b)       11 !c   delete the else block, but keep these instrs.
 *      ...                     If it is not B, keep block but delete these
 *  1b: ...                     instrs.
 *
 * F    LOAD_CONST          c   Delete first jump (second instruction),
 *      B_IF (1 instr)  x1  x   this will turn into one of A...D on a
 *      B_IF            x0  x   future pass.  Ignore if jump is not to next
 *                              instruction.  Bug if next instruction is not
 *                              'B_IF x0x'.
 *
 * Any two B_IF's in a row with their s-flag set is also a bug.
 */
static bool
simplify_conditional_jumps(struct assemble_t *a, struct as_frame_t *fr)
{
        bool reduced;
        instruction_t *idata = (instruction_t *)fr->af_instr.s;
        Object **rodata = as_frame_rodata(fr);
        instruction_t *ip = idata;

        reduced = false;
        while (ip->code != INSTR_END) {
                instruction_t *ip2;
                Object *left;
                bool cond1, cond2, do_jump;
                enum result_t status;
                int arg1;

                ip2 = next_instr(ip);
                if (ip2->code == INSTR_END)
                        break;

                if (ip->code != INSTR_LOAD_CONST ||
                    ip2->code != INSTR_B_IF) {
                        ip = ip2;
                        continue;
                }
                left = rodata[ip->arg2];
                cond1 = !var_cmpz(left, &status);
                bug_on(status == RES_ERROR);
                arg1 = ip2->arg1 & ~IARG_COND_COND;
                cond2 = !!(ip2->arg1 & IARG_COND_COND);
                do_jump = (cond1 == cond2);

                if ((arg1 & (IARG_COND_DELF | IARG_COND_SAVEF))
                    == IARG_COND_DELF) {
                        /* case A & B */
                        short *labels = (short *)fr->af_labels.s;
                        instruction_t *where = idata + labels[ip2->arg2];
                        if (do_jump) {
                                nopify(ip, where);
                                ip = where;
                                reduced = true;
                        } else {
                                instruction_t *wherenext;
                                where = prev_instr(idata, where);
                                if (where->code != INSTR_B) {
                                        ip = ip2;
                                        continue;
                                }

                                wherenext = idata + labels[where->arg2];
                                if (where >= wherenext) {
                                        ip = ip2;
                                        continue;
                                }
                                ip->code = INSTR_NOP;
                                ip2->code = INSTR_NOP;
                                nopify(where, wherenext);
                                reduced = true;
                                ip = wherenext;
                        }
                } else if (arg1 == 0) {
                        /* Case C & D */
                        if (do_jump) {
                                ip->code = INSTR_B;
                                ip->arg1 = 0;
                                ip->arg2 = ip2->arg2;
                                ip2->code = INSTR_NOP;
                        } else {
                                ip->code = INSTR_NOP;
                                ip2->code = INSTR_NOP;
                        }
                        ip = next_instr(ip2);
                        reduced = true;
                } else if (!!(arg1 & IARG_COND_SAVEF)) {
                        /* Could be case E or F */
                        short *labels = (short *)fr->af_labels.s;
                        instruction_t *ip3, *ip4;
                        ip3 = next_instr(ip2);
                        ip4 = idata + labels[ip2->arg2];
                        if (ip4 == ip3) {
                                /* Case F */
                                bug_on(ip3->code != INSTR_B_IF);
                                bug_on(!!(ip3->arg1 & IARG_COND_SAVEF));
                                ip2->code = INSTR_NOP;
                                reduced = true;
                                ip = ip3;
                        } else if (ip4 > ip2 &&
                                   !!(arg1 & IARG_COND_DELF)) {
                                /* Case E */
                                if (do_jump) {
                                        nopify(ip3, ip4);
                                        reduced = true;
                                        ip = ip4;
                                } else {
                                        ip3 = prev_instr(idata, ip4);
                                        if (ip3->code != INSTR_B) {
                                                ip->code = INSTR_NOP;
                                                ip2->code = INSTR_NOP;
                                                ip = next_instr(ip2);
                                                reduced = true;
                                                continue;
                                        }
                                        ip4 = idata + labels[ip3->arg2];
                                        if (ip4 < ip3) {
                                                ip = ip2;
                                                continue;
                                        }
                                        nopify(ip3, ip4);
                                        reduced = true;
                                        ip = ip4;
                                }
                        } else {
                                ip = ip2;
                        }
                } else {
                        ip = ip2;
                }
        }
        return reduced;
}

/*
 * In some cases, we've reduced down to branching to the very next
 * instruction.  Remove this trivial branch instruction.  We do not
 * bother trying to delete code between 'B ... label' in this pass.
 */
static bool
simplify_unconditional_jumps(struct assemble_t *a, struct as_frame_t *fr)
{
        bool reduced;
        instruction_t *idata = (instruction_t *)fr->af_instr.s;
        short *labels = (short *)fr->af_labels.s;
        instruction_t *ip = idata;

        reduced = false;
        while (ip->code != INSTR_END) {
                instruction_t *ip2, *ip3;
                if (ip->code != INSTR_B) {
                        ip = next_instr(ip);
                        continue;
                }

                ip2 = idata + labels[ip->arg2];
                ip3 = next_instr(ip);
                if (ip2 == ip3) {
                        ip->code = INSTR_NOP;
                        reduced = true;
                }
                ip = ip3;
        }
        return reduced;
}

static bool
simplify_const_operands(struct assemble_t *a, struct as_frame_t *fr)
{
        bool reduced;
        instruction_t *idata = (instruction_t *)fr->af_instr.s;
        Object **rodata = as_frame_rodata(fr);
        instruction_t *ip = idata;

        reduced = false;
        while (ip->code != INSTR_END) {
                instruction_t *ip2, *ip3;
                Object *left, *result;

                bug_on(ip >= &idata[as_frame_ninstr(fr)]);

                if (ip->code != INSTR_LOAD_CONST) {
                        ip = next_instr(ip);
                        continue;
                }

                ip2 = next_instr(ip);
                if (ip2->code == INSTR_END)
                        break;

                left = rodata[ip->arg2];

                bug_on(ip >= &idata[as_frame_ninstr(fr)]);
                if (ip2->code != INSTR_LOAD_CONST) {
                        result = try_unaryop(left, ip2);
                        ip3 = ip2;
                } else {
                        Object *right;

                        ip3 = next_instr(ip2);
                        if (ip3->code == INSTR_END)
                                break;

                        right = rodata[ip2->arg2];
                        result = try_binop(left, right, ip3);
                }

                /*
                 * NULL means either ip3 wasn't a bin-op or an error
                 * occurred.  Suppress errors for now, this could be in a
                 * try/catch statement.
                 *
                 * XXX if error, need to mark instruction positions as
                 * unreduceable so I'm not repeating at these points for
                 * every scan.
                 */
                if (result == NULL) {
                        err_clear();
                        ip = ip3;
                        continue;
                }

                ip->arg2 = seek_rodata(a, fr, result);
                ip2->code = INSTR_NOP;
                if (ip3 != ip2)
                        ip3->code = INSTR_NOP;
                ip = next_instr(ip3);
                reduced = true;
        }
        return reduced;
}

static bool
remove_unreachable_code(struct assemble_t *a, struct as_frame_t *fr)
{
        instruction_t *idata = (instruction_t *)fr->af_instr.s;
        short *labels = (short *)fr->af_labels.s;
        size_t nlabel = as_frame_nlabel(fr);
        size_t ninstr = as_frame_ninstr(fr);
        instruction_t *ip;
        bool reduced;

        /*
         * This is not the most thorough way to check for deletion.
         * If we get something like:
         *         B   2
         *         B   1
         *         ...
         *      1:
         *         ...
         *      2:
         * then testing whether the outer B...2 can be removed will
         * yield 'no' because the inner label '1' is still valid.
         *
         * I'm OK with this flaw, because
         *   1) there's hardly any unreachable code left; IARG_COND_DELF
         *      already took care of most of it.
         *   2) the alternative (as far as I can figure on my current
         *      coffee ration) is a nearly O(n*n) algorithm, and
         *   3) it isn't worth more than the following algorithm anyway,
         *      since our only benefits are the removal of a single
         *      branch instruction and a microscopic improvement in
         *      locality of reference.
         *
         * XXX: Consider using the O(n*n) algorithm after all if it's
         * known that the script being loaded is marked for printing to
         * an .evcd file; future loads will then skip all this
         * optimization crap.
         */
        reduced = false;
        for (ip = idata; ip < &idata[ninstr]; ip++) {
                size_t j;
                instruction_t *end;
                if (ip->code != INSTR_B)
                        continue;
                end = idata + labels[ip->arg2];
                if (end < ip)
                        continue;
                for (j = 0; j < nlabel; j++) {
                        if (j == ip->arg2)
                                continue;
                        if (labels[j] >= (short)(ip - idata) &&
                            labels[j] < (short)(end - idata)) {
                                break;
                        }
                }
                if (j != nlabel)
                        continue;
                nopify(ip, end);
                reduced = true;
                /* minus one because it's in the 'for' iterator */
                ip = end - 1;
        }
        return reduced;
}

enum {
        STACK_NLABEL = 32,
        STACK_NINSTR = 128
};

static void
mark_unused_labels(struct assemble_t *a, struct as_frame_t *fr)
{
        short *labels;
        instruction_t *instrs;
        size_t i, nlabel, ninstr;
        char *used, used_stack[STACK_NLABEL];

        labels = (short *)fr->af_labels.s;
        instrs = (instruction_t *)fr->af_instr.s;
        nlabel = as_frame_nlabel(fr);
        ninstr = as_frame_ninstr(fr);

        if (nlabel <= STACK_NLABEL)
                used = used_stack;
        else
                used = emalloc(nlabel);
        memset(used, 0, nlabel);

        for (i = 0; i < ninstr; i++) {
                if (instr_uses_jump(instrs[i]))
                        used[instrs[i].arg2] = 1;
        }

        for (i = 0; i < nlabel; i++) {
                if (!used[i])
                        labels[i] = -1;
        }

        if (used != used_stack)
                efree(used);
}

/*
 * Optimize out any instruction or group of instructions that can be
 * reduced due to them operating on known consts.
 * EvilCandy is not yet srmt enough for loop invariants or things
 * like that.
 */
static void
optimize_instructions(struct assemble_t *a)
{
        struct list_t *li;
        list_foreach(li, &a->finished_frames) {
                struct as_frame_t *fr = list2frame(li);
                bool reduced, reduced_once;
                reduced_once = false;
                do {
                        reduced = false;
                        if (simplify_const_operands(a, fr))
                                reduced = true;
                        if (simplify_conditional_jumps(a, fr))
                                reduced = true;
                        if (simplify_unconditional_jumps(a, fr))
                                reduced = true;
                        if (reduced)
                                reduced_once = true;
                } while (reduced);

                mark_unused_labels(a, fr);
                if (remove_unreachable_code(a, fr)) {
                        reduced_once = true;
                        /*
                         * Gotta call this again; I had to call it before
                         * remove_unreachable_code, to prevent it from
                         * getting confused with unused labels.
                         */
                        mark_unused_labels(a, fr);
                }
                if (reduced_once) {
                        remove_nop_instructions(a, fr);
                        remove_unused_rodata(fr);
                }
        }
}

/*
 * Jump instructions' arg2 currently holds a label number.
 * Convert that into an offset from the program counter.
 */
static void
resolve_jump_labels(struct assemble_t *a, struct as_frame_t *fr)
{
        size_t i, n_instr, n_label;
        short *labels;
        instruction_t *instrs;
        struct as_frame_t *frsav;
        struct buffer_t labelbuf;

        labels  = (short *)fr->af_labels.s;

        instrs = (instruction_t *)fr->af_instr.s;
        n_instr = as_frame_ninstr(fr);

        frsav = a->fr;
        a->fr = fr;

        for (i = 0; i < n_instr; i++) {
                if (instr_uses_jump(instrs[i])) {
                        instruction_t *ii = &instrs[i];
                        bug_on((unsigned)ii->arg2 >= as_frame_nlabel(fr));
                        bug_on(labels[ii->arg2] < 0);
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

        /*
         * Reduce 'unneeded' jump labels.  (We actually don't need
         * any of them anymore.)
         *
         * XXX: This is purely cosmetic, for the disassembly.  It's
         * probably better to get rid of labels in XptrType object
         * altogether, and let disassembler rebuild them from the
         * instructions, since human-readable disassembly is the
         * exception rather than the rule.
         */
        n_label = as_frame_nlabel(fr);
        buffer_init(&labelbuf);
        for (i = 0; i < n_label; i++) {
                if (labels[i] < 0)
                        continue;
                buffer_putd(&labelbuf, &labels[i], sizeof(labels[i]));
        }
        buffer_free(&fr->af_labels);
        memcpy(&fr->af_labels, &labelbuf, sizeof(labelbuf));
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
 * @fr: Entry-level assembly frame.  This function will recursively call
 *      itself to create all the descendant XptrType objects.
 */
struct xptrvar_t *
assemble_frame_to_xptr(struct assemble_t *a, struct as_frame_t *fr)
{
        struct xptrvar_t *x;
        instruction_t *instrs;
        int i, n_instr;

        /*
         * Resolve any nested function defintions from a magic number to
         * a pointer to another XptrType object.  This means that we have
         * to process the most deeply nested functions first, hence the
         * recursion.  assembler.c already checked against runaway
         * recursion for us, and in the case of reassemble(), that
         * disassembly was generated from code that also was checked by
         * assemble() some time in the past.
         *
         * ...but we'd be reckless to assume it, so add this inexpensive
         * recursion guard anyway.
         */
        RECURSION_DECLARE_FUNC();
        RECURSION_START_FUNC(RECURSION_MAX);

        instrs = (instruction_t *)fr->af_instr.s;
        n_instr = as_frame_ninstr(fr);
        for (i = 0; i < n_instr; i++) {
                instruction_t *ii = &instrs[i];
                struct as_frame_t *child;
                Object **rodata;
                long long idval;

                if (ii->code != INSTR_DEFFUNC)
                        continue;

                bug_on(as_frame_nconst(fr) <= ii->arg2);
                rodata = as_frame_rodata(fr);
                idval = idvar_toll(rodata[ii->arg2]);

                child = func_label_to_frame(a, idval);
                bug_on(!child || child == fr);

                VAR_DECR_REF(rodata[ii->arg2]);
                rodata[ii->arg2] = (Object *)assemble_frame_to_xptr(a, child);
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

        RECURSION_END_FUNC();

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

        optimize_instructions(a);

        list_foreach(li, &a->finished_frames) {
                struct as_frame_t *fr = list2frame(li);
                /*
                 * Do this before resolving jump labels,
                 * because some "fake" instructions exist
                 * within.
                 */
                replace_fake_instructions(a, fr);
                resolve_jump_labels(a, fr);
        }

        /*
         * See as_frame_pop().
         * First child of finished_frames is also our entry point.
         */
        return assemble_frame_to_xptr(a,
                                list2frame(a->finished_frames.next));
}


