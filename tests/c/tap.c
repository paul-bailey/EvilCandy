#include <tests/tap.h>
#include <stdio.h>

/**
 * tap_init - Initialize a TAP state machine.
 * @tap: state machine to initialize
 * @fp:  file to print to, usually stderr
 * @ntests: Number of tests that will be run, or -1 if you don't know
 *          this number in advance.
 */
void
tap_init(struct tap_t *tap, FILE *fp, int ntests)
{
        tap->fp = fp;
        tap->testno = 1;
        tap->ntests = ntests;
        tap->nr_err = 0;
}

/* public wrapper for this documented in tests/tap.h */
enum result_t
tap_test__(struct tap_t *tap, bool cond, const char *test,
           const char *file, unsigned int line)
{
        if (tap->testno == 1 && tap->ntests >= 0)
                fprintf(tap->fp, "1..%d\n", tap->ntests);

        if (cond) {
                fprintf(tap->fp, "ok %d\n", tap->testno++);
                return RES_OK;
        } else {
                fprintf(tap->fp, "not ok %d - %s line %u: %s\n",
                        tap->testno++, file, line, test);
                tap->nr_err++;
                return RES_ERROR;
        }
}

/**
 * tap_end_tests - If number of tests was unknown at start, print that
 *                 number now.  Else, do nothing.
 *
 * Do not call this more than once, without first re-initializing with
 * tap_init().
 */
void
tap_end_tests(struct tap_t *tap)
{
        if (tap->ntests < 0) {
                if (tap->testno > 0)
                        tap->testno--;
                fprintf(tap->fp, "1..%d\n", tap->testno);
        }
}

