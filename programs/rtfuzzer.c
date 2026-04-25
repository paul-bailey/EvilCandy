/* real-time fuzzer.  April 2026: This is a work in progress. */
#include <lib/buffer.h>
#include <lib/helpers.h>
#include <evilcandy/version.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/debug.h>
#include <evilcandy/assemble.h>
#include <evilcandy/enums.h>
#include <evilcandy/vm.h>
#include <evilcandy/err.h>
#include <evilcandy/global.h>
#include <internal/init.h>
#include <tests/prog_gen.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

struct template_program_t {
        const char *name;
        const char *template_text;
};

static void
run_evilcandy_(const char *program)
{
        Object *xptr, *result;

        xptr = assemble_string(program, false);
        assert(xptr != ErrorVar);
        assert(!err_occurred());
        /* all these programs will have at least one compilable token */
        assert(!!xptr);

        result = vm_exec_script(xptr, NULL);
        assert(result == ErrorVar);
        assert(err_occurred());
        err_clear();
}

/*
 * FIXME: DRY violation with fuzzer.c
 * It's time to start making a second library, containing C functions that
 * are not per-se part of EvilCandy.
 */
static int
run_evilcandy(const char *program)
{
        int status = 0;
        int waited = 0;
        pid_t pid;

        pid = fork();
        if (pid == 0) {
                initialize_program();
                run_evilcandy_(program);
                end_program();

                _exit(EXIT_SUCCESS);
                return 0;
        }
        if (pid == (pid_t)-1) {
                perror("fork failed");
                exit(EXIT_FAILURE);
        }

        while (waited < 100) {
                if (waitpid(pid, &status, WNOHANG) != 0)
                        break;
                usleep(1000);
                waited++;
        }
        if (waited == 100) {
                kill(pid, SIGKILL);
                fprintf(stderr, "Program timed out\n");
                return -1;
        }
        if (WIFSIGNALED(status)) {
                fprintf(stderr, "Program crashed! signal %d\n",
                        WTERMSIG(status));
                if (WCOREDUMP(status))
                        fprintf(stderr, "core dump generated\n");
                return -1;
        }

        if (WIFEXITED(status)) {
                if (status != EXIT_SUCCESS) {
                        fprintf(stderr,
                                "Program exited with status %d\n",
                                WEXITSTATUS(status));
                        return -1;
                }
        }

        return 0;
}

#define FUZZERR(...)  do { \
        fprintf(stderr, "[Evilcandy RT Fuzzer]: "); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
} while (0);

static int
fuzz_loop(unsigned int n_tests, unsigned int seed, int verbose)
{
        char buf[8192];
        unsigned int i;
        for (i = 0; i < n_tests; i++) {
                int result = prog_gen(buf, sizeof(buf), 10);
                if (result < 0) {
                        FUZZERR("Fuzzer for " EVILCANDY_VERSION);
                        FUZZERR("Cannot sufficiently produce tests for seed %u",
                               seed);
                        FUZZERR("buffer size %u likely too small",
                               (int)sizeof(buf));
                        return -1;
                }

                if (verbose)
                        printf("%s\n", buf);

                if (run_evilcandy(buf) < 0) {
                        FUZZERR("Fuzzer for " EVILCANDY_VERSION);
                        FUZZERR("Test #%d failed", i);
                        FUZZERR("Seed:    %u", seed);
                        FUZZERR("Failed program text below\n---\n%s", buf);
                        return -1;
                }
        }
        return 0;
}

int
main(int argc, char **argv)
{
        /* TODO: make these be command-line options */
        int seed = 12345;
        int verbose = 0;

        srand(seed);
        if (fuzz_loop(1000, seed, verbose) < 0)
                return EXIT_FAILURE;
        return EXIT_SUCCESS;
}

