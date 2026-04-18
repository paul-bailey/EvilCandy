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

static const struct template_program_t TEMPLATES[] = {
        {
                .name = "adder",
                .template_text =
                        "function add(a, b) {\n"
                        "  return a + b;\n"
                        "}\n"
                        "print(add(@EXPR1, @EXPR2));\n",
        }, {
                .name = "dict",
                .template_text =
                        "let @IDENTIFIER1 = { 'x': @EXPR1 };\n"
                        "print(@IDENTIFIER1.@IDENTIFIER2);\n",
        }
};

/*
 * DOC: Substitution
 *
 * For a given @xxxxN, N is '1' or '2'.
 * The arrays below are conceptually double-arrays.
 * Row is a don't-care.  Column is determined by `which`.
 * We always want @xxx1 to take from the first column
 * and @xxx2 to take from the second column.
 */
static const char *
substitute_expression(unsigned int which)
{
        static const char *EXPR_SUBST[8] = {
                "some_name",                     "1",
                "f'{some_other_name}'",          "'a'",
                "{ 'a': 1 }",                    "b'x08'",
                "function(x) { return x + 1; }", "[]"
        };
        bug_on(which > 1);
        return EXPR_SUBST[2 * (rand() % 4) + which];
}

static const char *
substitute_identifier(unsigned int which)
{
        static const char *IDENTIFIER_SUBST[6] = {
                "x", "falstaff",
                "y", "hal",
                "z", "poins"
        };

        bug_on(which > 1);
        return IDENTIFIER_SUBST[2 * (rand() % 3) + which];
}

static void
gen_program(struct buffer_t *b, const char *template)
{
        const char *s;
        int c;


        s = template;
        while ((c = *s++) != '\0') {
                int n;
                const char *start, *end;
                if (c != '@') {
                        buffer_putc(b, c);
                        continue;
                }
                start = s;
                while (isupper((int)*s))
                        s++;
                end = s;
                bug_on(*s != '1' && *s != '2');
                n = *s - '1';
                s++;

                if (!strncmp(start, "IDENTIFIER", end - start)) {
                        buffer_puts(b, substitute_identifier(n));
                } else if (!strncmp(start, "EXPR", end - start)) {
                        buffer_puts(b, substitute_expression(n));
                } else {
                        bug();
                }
        }
}

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

static int
fuzz_loop(unsigned int n_tests, unsigned int seed, int verbose)
{
        struct buffer_t b;
        unsigned int i;
        int ret = -1;
        buffer_init(&b);

        for (i = 0; i < n_tests; i++) {
                size_t tpli = rand() % ARRAY_SIZE(TEMPLATES);
                buffer_reset(&b);
                gen_program(&b, TEMPLATES[tpli].template_text);
                if (verbose)
                        printf("%s\n", b.s);

                if (run_evilcandy(b.s) < 0) {
                        fprintf(stderr, "[Evilcandy RT Fuzzer]: Fuzzer for " EVILCANDY_VERSION);
                        fprintf(stderr, "[Evilcandy RT Fuzzer]: Test #%d failed\n", i);
                        fprintf(stderr, "[Evilcandy RT Fuzzer]: Seed:    %u\n", seed);
                        fprintf(stderr, "[Evilcandy RT Fuzzer]: Program: %s\n", b.s);
                        ret = -1;
                        goto out;
                }
        }
        ret = 0;

out:
        buffer_free(&b);
        return ret;
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

