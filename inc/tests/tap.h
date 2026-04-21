#ifndef EVC_INC_TESTS_TAP_H
#define EVC_INC_TESTS_TAP_H

#include <stdio.h>
#include <stdbool.h>
#include <evilcandy/enums.h>

struct tap_t {
        FILE *fp;
        int ntests;
        int testno;
        int nr_err;
};

extern void tap_init(struct tap_t *tap, FILE *fp, int ntests);
extern void tap_end_tests(struct tap_t *tap);
static inline int tap_nr_error(struct tap_t *tap) { return tap->nr_err; }

/**
 * tap_test - test a condition and print result in TAP format
 * @tap: tap state machine
 * @cond: If true, test is ok; if false, test is bad.
 *
 * Return: RES_OK or RES_ERROR, depending on @cond.  This return value
 * is simply a convenience way to wrap the test with `if`, in case you
 * need to quit early if there's a failure.
 */
#define tap_test(tap_, cond_) \
        tap_test__(tap_, cond_, #cond_, __FILE__, __LINE__)

/* private */
extern enum result_t tap_test__(struct tap_t *tap, bool cond,
                                const char *test, const char *file,
                                unsigned int line);

#endif /* EVC_INC_TESTS_TAP_H */
