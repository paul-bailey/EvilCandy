/* All the enumerations which should be visible to the whole project */
#ifndef EVILCANDY_EVCENUMS_H
#define EVILCANDY_EVCENUMS_H

/* Tunable parameters */
enum {
        /*
         * XXX: Arbitrary choice for value, do some research and find out
         * if there's a known reason for a specific pick/method for stack
         * overrun protection.
         */
        RECURSION_MAX   = 256,

        /* for vm.c */
        /* TODO: Make VM_STACK_SIZE configurable by the command-line. */
        VM_STACK_SIZE   = 1024 * 16,

        /*
         * These are static definitions of array sizes in struct
         * asframe_t, a temporary struct used by the parser.  I could
         * replace these limits with something more dynamic, though I'm
         * getting sick of calling malloc everywhere.  The heap doesn't
         * grow on trees.
         */
        FRAME_ARG_MAX   = 24,
        FRAME_STACK_MAX = 128,
        FRAME_NEST_MAX  = 32,
        FRAME_CLOSURE_MAX = 24,
};

/**
 * DOC: Result values
 *
 * Fatal errors--mostly bug traps or running out of memory--cause the
 * program to exit immediately after printing an error message, so they
 * don't have any return values enumerated.  The following are for
 * runtime (ie. post-parsing) errors caused by user, system errors that
 * are not considered fatal, or exceptions intentionally raised by the
 * user.  They will eventually trickle their way back into the VM's
 * main loop, which will decide what to do next.
 *
 * For functions that must return a struct var_t (which is like half of
 * them), return ErrorVar if there is an error.  (I'm trying to reduce
 * the number of points where NULL could be returned, since it's so easy
 * to accidentally de-reference them and cause a segmentation fault.)
 *
 * @RES_OK:             Success
 * @RES_EXCEPTION:      User raised an exception
 * @RES_RETURN:         Return from function or script.  Used only by VM
 * @RES_ERROR:          Marklar error. Sometimes I plan ahead and think
 *                      things through.  Other times I type away YOLO-like
 *                      and say "I should return an error code here but I
 *                      haven't defined any yet, so I'll just return my
 *                      trusty old -1 for now and change it later."
 *                      Don't judge, you KNOW you do it too.
 *                      Anyway, it's "later" now, and I can't be bothered
 *                      to track down all those -1s.
 */
enum result_t {
        RES_OK = 0,
        RES_EXCEPTION = 1,
        RES_RETURN = 2,
        RES_ERROR = -1,
};


#endif /* EVILCANDY_EVCENUMS_H */
