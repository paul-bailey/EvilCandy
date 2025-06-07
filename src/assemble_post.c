/*
 * XXX  This file bravely makes the assumption that 'optimize' and
 *      'front-load' mean the same thing.  This will make for a sluggish
 *      load time, unless we get a good serialization scheme working, so
 *      that this stuff only happens when a byte-code file doesn't exist.
 *
 * XXX REVISIT: A lot of the LOAD_CONST checks in this file can be both
 * LOAD_CONST and PUSH_LOCAL, since PUSH_LOCAL followed immediately by an
 * operator instruction or a B_IF instruction cannot mean "declare a
 * local variable"; instead it must mean "LOAD_CONST (null)"; (The reason
 * it doubles up for this purpose is because we don't waste space storing
 * NullVar in .rodata.)  So we could do one of the following:
 *      1. Every place in this file check if LOAD_CONST _or_ PUSH_LOCAL,
 *         or if that's considered too dangerous or has too many corner-
 *         cases...
 *      2: have assembler.c only use PUSH_LOCAL for declaring variables,
 *         and have this file replace LOAD_CONST (null) with PUSH_LOCAL
 *         after we have finished optimization.
 *      3: create a new instruction LOAD_NULL which does the same thing
 *         as PUSH_LOCAL.
 * We also have scenarios where DEFDICT, DEFTUPLE, and DEFLIST may take
 * all LOAD_CONST's for their definitions, in which we could replace
 * these instructions with DEFDICT_CONST, etc., start allowing tuples in
 * .rodata, and adding these to the checks along with LOAD_CONST.
 *
 * XXX REVISIT: There's a lot of PUSH_BLOCK instructions that can be
 * reduced in this file as well.  In particular, check for a lack of
 * 'break' or 'continue' in block.
 */
#include <evilcandy.h>
#include <xptr.h>
#include "assemble_priv.h"

/*
 * Simplification of labels and removal of detected unreachable code is
 * optional, set by this macro.  Enabling it isn't the most time-
 * consuming algorithm, but it does unfortunately make the disassembly
 * more difficult for a planet-earth homo sapiens to interpret.
 *
 * The primary benefits are minimal: if certain values in program-flow
 * conditional expressions are known consts, then there may be fewer
 * branch instructions, fewer LOAD_CONST instructions, and better
 * locality of reference.
 *
 * XXX: ought to go in configure.ac
 */
#define TRY_SIMPLIFY_LABELS 0

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
                        if (labels[j] > i)
                                labels[j] -= amount;
                        else if (labels[j] > after)
                                labels[j] = after;
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
        if (ii == base)
                return NULL;
        do {
                ii--;
        } while (ii->code == INSTR_NOP && ii > base);
        return ii->code == INSTR_NOP ? NULL : ii;
}

/*
 * Reduce LOAD_CONST + B_IF to either nothing or B, depending whether
 * or not the conditions match.
 *
 * Need to consider the following scenarios
 *      instr           flags   action
 *                      ds  c
 *
 * A    LOAD_CONST          c   Change to unconditional jump B
 *      B_IF            00  c
 *
 * B    LOAD_CONST          c   Delete both these instructions, let instr's
 *      B_IF            00 !c   fall through.
 */
static bool
simplify_conditional_jumps(struct assemble_t *a, struct as_frame_t *fr)
{
        bool reduced;
        instruction_t *ip;
        instruction_t *idata = (instruction_t *)fr->af_instr.s;
        Object **rodata = as_frame_rodata(fr);

        ip = idata;
        reduced = false;
        while (ip->code != INSTR_END) {
                instruction_t *ip2;
                Object *left;
                bool cond1, cond2, do_jump;
                enum result_t status;
                ip2 = next_instr(ip);
                if (ip2->code == INSTR_END)
                        break;

                if (ip->code != INSTR_LOAD_CONST ||
                    ip2->code != INSTR_B_IF ||
                    !!(ip2->arg1 & IARG_COND_SAVEF)) {
                        ip = ip2;
                        continue;
                }

                left = rodata[ip->arg2];
                cond1 = !var_cmpz(left, &status);
                bug_on(status == RES_ERROR);
                cond2 = !!(ip2->arg1 & IARG_COND_COND);
                do_jump = (cond1 == cond2);

                if (do_jump) {
                        /* scenario A */
                        ip->code = INSTR_NOP;
                        ip2->code = INSTR_B;
                        ip2->arg1 = 0;
                } else {
                        /* scenario B */
                        ip->code = INSTR_NOP;
                        ip2->code = INSTR_NOP;
                }
                ip = next_instr(ip2);
                reduced = true;
        }

        return reduced;
}

/*
 * Get rid of IARG_COND_SAVEF by changing where we jump to.
 *
 * These SAVEF flags are easy enough to implement, but they make the
 * disassembly confusing af, since we do or do not pop the condition off
 * the stack depending on whether we jump or not.  So go through the
 * effort of removing them; it has the added benefit of fewer B_IF
 * instructions in the program flow path.
 */
static bool
remove_save_flags(struct assemble_t *a, struct as_frame_t *fr)
{
        bool reduced;
        instruction_t *ip;
        instruction_t *idata = (instruction_t *)fr->af_instr.s;
        const short *labels = (short *)fr->af_labels.s;

        ip = idata;
        reduced = false;
        while (ip->code != INSTR_END) {
                instruction_t *ip2, *ip3;
                bool cond1, cond2;

                if (ip->code != INSTR_B_IF ||
                    !(ip->arg1 & IARG_COND_SAVEF)) {
                        ip = next_instr(ip);
                        continue;
                }

                cond1 = !!(ip->arg1 & IARG_COND_COND);
                ip2 = idata + labels[ip->arg2];
                if (ip2->code != INSTR_B_IF ||
                    !!(ip2->arg1 & IARG_COND_SAVEF)) {
                        /*
                         * We go FORWARDS from the first instance of the
                         * same-label B_IF(SAVEF), to make sure we get
                         * all of them for the same label in a row.
                         * However, we go BACKWARDS from the last set of
                         * these whose terminating B_IF has no
                         * IARG_COND_SAVEF.  Otherwise we'd have no way
                         * to keep track of our state.  So we have to
                         * skip this ip/ip2 combo for now; we'll hit it
                         * again on a future call.
                         *
                         * XXX: I'd like to speed this up and do it
                         * recursively, skipping all the iterations of
                         * do-while(reduced), but the only algo I can
                         * think up cannot use tail-call optimization;
                         * that's necessary if we have like a zillion of
                         * these little guys.
                         */
                        ip = next_instr(ip);
                        continue;
                }
                cond2 = !!(ip2->arg1 & IARG_COND_COND);
                if (cond1 != cond2) {
                        short newlabel = assemble_frame_next_label(fr);
                        unsigned long newlptr = (unsigned long)(next_instr(ip2) - idata);
                        assemble_frame_set_label(fr, newlabel, newlptr);
                        /* reassign, since a realloc may have occurred */
                        labels = (short *)fr->af_labels.s;
                        ip3 = next_instr(ip);
                        while (ip3 < ip2) {
                                if (ip3->code == ip->code &&
                                    ip3->arg2 == ip->arg2 &&
                                    !!(ip3->arg1 & IARG_COND_SAVEF)) {
                                        ip3->arg1 &= ~IARG_COND_SAVEF;
                                        ip3->arg2 = newlabel;
                                }
                                ip3 = next_instr(ip3);
                        }
                        ip->arg1 &= ~IARG_COND_SAVEF;
                        ip->arg2 = newlabel;
                } else {
                        /* cond1 == cond2 */
                        ip3 = next_instr(ip);
                        while (ip3 < ip2) {
                                if (ip3->code == ip->code &&
                                    ip3->arg2 == ip->arg2 &&
                                    !!(ip3->arg1 & IARG_COND_SAVEF)) {
                                        bug_on(ip3->arg1 != ip->arg1);
                                        ip3->arg1 &= ~IARG_COND_SAVEF;
                                        ip3->arg2 = ip2->arg2;
                                }
                                ip3 = next_instr(ip3);
                        }
                        ip->arg1 &= ~IARG_COND_SAVEF;
                        ip->arg2 = ip2->arg2;
                }
                ip = next_instr(ip2);
                reduced = true;
        }
        return reduced;
}

/*
 * In some cases, we've reduced down to branching to the very next
 * instruction.  Remove this trivial branch instruction.  We do not
 * bother trying to delete code between 'B ... label' in this pass.
 *
 * We cannot do this for every conditional jump, because we'd have to
 * delete everything previous which resulted in the condition being
 * loaded onto the stack.  We can at least be sure of LOAD or LOAD_CONST.
 */
static bool
remove_trivial_jumps(struct assemble_t *a, struct as_frame_t *fr)
{
        bool reduced;
        instruction_t *idata = (instruction_t *)fr->af_instr.s;
        short *labels = (short *)fr->af_labels.s;
        instruction_t *ip, *iplast;

        reduced = false;
        for (ip = idata, iplast = NULL;
             ip->code != INSTR_END; iplast = ip, ip = next_instr(ip)) {

                instruction_t *ip2, *ip3;

                if (ip->code != INSTR_B && ip->code != INSTR_B_IF)
                        continue;

                ip2 = next_instr(ip);
                ip3 = idata + labels[ip->arg2];
                if (ip2 != ip3)
                        continue;

                if (ip->code == INSTR_B_IF) {
                        /* No instruction before B_IF?!?! */
                        bug_on(iplast == NULL);
                        if ((iplast->code != INSTR_LOAD
                             && iplast->code != INSTR_LOAD_CONST)) {
                                /*
                                 * Previous instructions too complicated
                                 * to reverse-engineer, we'll have to let
                                 * this one stay put.
                                 */
                                continue;
                        }
                        iplast->code = INSTR_NOP;
                        iplast = prev_instr(idata, ip);
                }
                ip->code = INSTR_NOP;
                reduced = true;
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

enum {
        STACK_NLABEL = 32,
        STACK_NINSTR = 128
};

/*
 * helper to remove_unreachable_code - Traverse both paths of
 * INSTR_B_IF, one path of INSTR_B; recursive to fulfull this.
 *
 * This is not the most thorough way to check for deletion.  'if' state-
 * ments in particular have B_IF branching into an unreachable area (see
 * comment in simplify_conditional_jumps why a const conditional does
 * not guarantee reduction), resulting in unreachable code not getting
 * marked for deletion.
 *
 * I'm OK with this flaw, because...
 *   1) There's hardly any unreachable code left; IARG_COND_DELF already
 *      took care of most of it.
 *   2) The better alternative would require a completely different
 *      parser, so I can do this at parse time instead of at compile
 *      time.  The only 'reliable' method for doing this with just
 *      instructions is an O(s**tload) reverse-engineering of the
 *      instruction sequence.
 *   3) It isn't worth more than the following algorithm anyway, since
 *      our only benefits are the removal of a single branch instruction
 *      and a microscopic improvement in locality of reference.
 */
static void
traverse_code(const instruction_t *idata, const short *labels,
              char *hits, const instruction_t *ip)
{
        while (ip->code != INSTR_END && !hits[ip - idata]) {
                hits[ip - idata] = 1;
                if (instr_uses_jump(*ip)) {
                        traverse_code(idata, labels,
                                      hits, idata + labels[ip->arg2]);
                        if (ip->code == INSTR_B)
                                return;
                        /* B_IF... fall through */
                }
                ip++;
        }
}

static bool
remove_unreachable_code(struct assemble_t *a, struct as_frame_t *fr)
{
        instruction_t *idata = (instruction_t *)fr->af_instr.s;
        short *labels = (short *)fr->af_labels.s;
        size_t i, ninstr = as_frame_ninstr(fr);
        char *hits, hits_stack[STACK_NINSTR];
        bool reduced;

        if (ninstr <= STACK_NINSTR)
                hits = hits_stack;
        else
                hits = emalloc(ninstr);
        memset(hits, 0, ninstr);

        traverse_code(idata, labels, hits, idata);

        reduced = false;
        /* -1 to not accidentally NOP-ify INSTR_END */
        for (i = 0; i < ninstr-1; i++) {
                if (!hits[i]) {
                        if (idata[i].code != INSTR_NOP)
                                reduced = true;
                        idata[i].code = INSTR_NOP;
                }
        }
        if (hits != hits_stack)
                efree(hits);
        return reduced;
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
                        if (remove_save_flags(a, fr))
                                reduced = true;
                        if (reduced)
                                reduced_once = true;
                } while (reduced);

                do {
                        reduced = false;
                        if (simplify_const_operands(a, fr))
                                reduced = true;
                        if (TRY_SIMPLIFY_LABELS) {
                                if (simplify_conditional_jumps(a, fr))
                                        reduced = true;
                                if (remove_trivial_jumps(a, fr))
                                        reduced = true;
                                if (remove_unreachable_code(a, fr))
                                        reduced = true;
                        }
                        if (reduced)
                                reduced_once = true;
                } while (reduced);

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
        size_t i, n_instr;
        short *labels;
        instruction_t *instrs;
        struct as_frame_t *frsav;

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
        Object **rodata;
        int i, n_rodata;

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

        n_rodata = as_frame_nconst(fr);
        rodata = as_frame_rodata(fr);
        for (i = 0; i < n_rodata; i++) {
                long long idval;
                struct as_frame_t *child;
                Object *obj;

                obj = rodata[i];
                if (obj->v_type != &IdType)
                        continue;
                idval = idvar_toll(obj);
                child = func_label_to_frame(a, idval);
                bug_on(!child || child == fr);
                VAR_DECR_REF(rodata[i]);
                rodata[i] = (Object *)assemble_frame_to_xptr(a, child);
        }

        do {
                struct xptr_cfg_t cfg;
                cfg.file_name   = a->file_name;
                cfg.file_line   = fr->line;
                cfg.n_rodata    = as_frame_nconst(fr);
                cfg.n_instr     = as_frame_ninstr(fr);
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


