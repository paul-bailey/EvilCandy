#ifndef EVILCANDY_RECURSION_H
#define EVILCANDY_RECURSION_H

/**
 * DOC: Recursion traps
 *
 * "Recursion" was chosen for lack of a better word.  So far I'm aware of
 * the following places where user code could cause the sort of runaway
 * recursion that would also affect the irl C stack:
 *
 *    - Cyclic dependencies of imported scripts, causing recursion of the
 *      import() built-in function.
 *    - Recursive user functions.  This can cause a stack overrun on not
 *      only the user stack (which is recoverable) but also the C stack
 *      (which is not).  I'd rather not find out which happens first.
 *    - Cyclic reference in mutable objects like dictionaries and lists,
 *      which may cause infinite recursion of their .str and .cmp methods.
 *    - Recursion in the assembler, where expressions and functions get
 *      too deeply nested.  (Unlike the above cases, this would have to
 *      be deliberate, but protect against it anyway.)
 *
 * Use these macros like so:
 *
 *      int foo(args)
 *      {
 *              RECURSION_DECLARE_FUNC();
 *              RECURSION_START_FUNC(some_limit);
 *
 *              bar = ...code that could call foo() recursively...
 *
 *              RECURSION_END_FUNC();
 *              return bar;
 *      }
 *
 * Obviously this is not tail-call optimized, but this is not for tail
 * calls anyway.  It's for cases where the recursion could be caused by
 * improper user input.
 *
 * Do not use the non-function versions (the ones without xxx_FUNC).
 * They are used only by assemble(), where (1) a longjmp takes program
 * flow upstream from the recursion wrappers, and (2) unlike with vm.c
 * and elsewhere, it's known that assemble()'s recursion count _can_
 * indeed be reset to zero when finished, because the recursion is only
 * internal.
 */

/* do not use side-effects for any of these macro args */
#define RECURSION_DECLARE(name_) static long recursion_counter_##name_ = 0

/*
 * limit should be a macro or enum, not a hard-coded number,
 * or the message to the user will make no sense.
 */
#define RECURSION_START(name_, limit_) \
do { \
        if (recursion_counter_##name_ >= limit_) \
                fail("Recursion limit reached: you may need to adjust " #limit_); \
        recursion_counter_##name_++; \
} while (0)

#define RECURSION_END(name_) \
do { \
        bug_on(recursion_counter_##name_ <= 0); \
        recursion_counter_##name_--; \
} while (0)

/*
 * For cases only where a longjmp call could take program flow
 * upstream of recursive function
 */
#define RECURSION_RESET(name_) \
do { \
        recursion_counter_##name_ = 0; \
} while (0)

#define RECURSION_DEFAULT_START(name_) \
        RECURSION_START(name_, RECURSION_MAX)

#define RECURSION_DECLARE_FUNC()        RECURSION_DECLARE(__FUNCTION__)
#define RECURSION_START_FUNC(limit_)    RECURSION_START(__FUNCTION__, limit_)
#define RECURSION_END_FUNC()            RECURSION_END(__FUNCTION__)


#endif /* EVILCANDY_RECURSION_H */
