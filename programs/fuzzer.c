#include <evilcandy.h>
#include <internal/init.h>

#include <assert.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

/*
 * Set to 1 to see occasional "test #nnn" progress.
 * Set to 2 when debugging, to see "Program: bigLiStOfGiBberIsh",
 *      which the assembler is trying to parse, before each test
 *      is run.  (Extremely verbose, will drag progress to a crawl).
 *
 * FIXME: This should be a command-line option
 */
static int fuzzer_verbose = 0;

struct strbuf_t {
        char *buf;
        size_t len;
        size_t cap;
};

static bool opt_nofork = false;
static long opt_ntests = 50 * 1000;

static void
sb_init(struct strbuf_t *sb, char *buffer, size_t cap)
{
        sb->buf = buffer;
        sb->len = 0;
        sb->cap = cap;
        sb->buf[0] = '\0';
}

static void
sb_append(struct strbuf_t *sb, const char *s)
{
        while (*s != '\0' && sb->len + 1 < sb->cap)
                sb->buf[sb->len++] = *s++;
        sb->buf[sb->len] = '\0';
}

static const char *idents[] = {
        "x", "y", "z", "foo", "bar"
};

static const char *ops[] = {
        "+", "-", "*", "/"
};

static int
rnd(int n)
{
        return rand() % n;
}

static void
gen_number(struct strbuf_t *out)
{
        char buf[64];
        evc_sprintf(buf, sizeof(buf), "%d", rnd(100));
        sb_append(out, buf);
}

static void
gen_ident(struct strbuf_t *out)
{
        sb_append(out, idents[rnd(5)]);
}

static void
gen_expr(struct strbuf_t *out, int depth)
{
        int choice;

        if (depth <= 0) {
                if (rnd(2))
                        gen_number(out);
                else
                        gen_ident(out);
                return;
        }

        choice = rnd(5);
        switch (choice) {
        case 0: /* binary operator */
                sb_append(out, "(");
                gen_expr(out, depth - 1);
                sb_append(out, ops[rnd(4)]);
                gen_expr(out, depth - 1);
                sb_append(out, ")");
                break;

        case 1: /* function call */
                gen_ident(out);
                sb_append(out, "(");
                gen_expr(out, depth - 1);
                sb_append(out, ")");
                break;

        case 2: /* nested */
                sb_append(out, "(");
                gen_expr(out, depth - 1);
                sb_append(out, ")");
                break;
        default:
                if (rnd(2))
                        gen_number(out);
                else
                        gen_ident(out);
        }
}

static void
gen_stmt(struct strbuf_t *out, int depth)
{
        int choice = rnd(3);

        switch (choice) {
        case 0: /* assignment */
                sb_append(out, "let ");
                gen_ident(out);
                sb_append(out, "=");
                gen_expr(out, depth);
                sb_append(out, ";");
                break;

        case 1: /* expression statement */
                gen_expr(out, depth);
                sb_append(out, ";");
                break;

        case 2: /* nested block */
                sb_append(out, "{");
                gen_stmt(out, depth - 1);
                sb_append(out, "} ");
                break;
        }
}

static void
gen_program(char *buffer, size_t size)
{
        struct strbuf_t sb;
        int i, n;

        sb_init(&sb, buffer, size);

        n = 1 + rnd(5);
        for (i = 0; i < n; i++)
                gen_stmt(&sb, 3);
}

/* return true if this got as far as execution */
static bool
run_evilcandy_(const char *program)
{
        Object *result, *xptr;

        xptr = assemble_string(program, false);
        if (xptr == ErrorVar) {
                assert(err_occurred());
                err_clear();
                /* TODO: debug_errclear() */
                return false;
        }

        assert(!err_occurred());

        if (!xptr)
                return false;

        result = vm_exec_script(xptr, NULL);
        if (result == ErrorVar) {
                assert(err_occurred());
                err_clear();
        } else {
                assert(!err_occurred());
                assert(!!result);
                VAR_DECR_REF(result);
        }
        return true;
}

static int
run_evilcandy(const char *program)
{
        int status = 0;
        int waited = 0;
        pid_t pid;

        if (opt_nofork) {
                run_evilcandy_(program);
                return 0;
        }

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

static void
mutate(char *buf, size_t len, size_t cap)
{
        int i, j, choice = rnd(4);

        if (!len)
                return;

        switch (choice) {
        case 0: /* delete char */
                i = rnd(len);
                memmove(&buf[i], &buf[i+1], len - i);
                break;
        case 1: /* duplicate chunk */
                i = rnd(len);
                j = rnd(len);
                if (i < j && (cap - len) > (j - i)) {
                        memmove(&buf[j + (j - i)], &buf[j], len - j);
                        memcpy(&buf[j], &buf[i], j - i);
                        buf[j + len - i] = '\0';
                }
                break;
        case 2: /* flip random char */
                i = rnd(len);
                buf[i] = (char)(32 + rnd(95));
                break;
        case 3: /* insert random char */
                if (cap - len > 2) {
                        i = rnd(len);
                        memmove(&buf[i+1], &buf[i], len - i);
                        buf[i] = (char)(32 + rnd(95));
                        buf[len+1] = '\0';
                }
                break;
        }
}

/* TODO: add option to not bail on the first failure */
static int
fuzz_loop(unsigned int n_tests, unsigned int seed)
{
        static char program[8096];
        int i;
        for (i = 0; i < n_tests; i++) {
                int m, j;

                gen_program(program, sizeof(program));
                m = rnd(5);

                for (j = 0; j < m; j++) {
                        mutate(program, strlen(program), sizeof(program));
                }

                if (fuzzer_verbose) {
                        if (i % (10 * 1000) == 0)
                                fprintf(stderr, "%d\n", i);
                        if (fuzzer_verbose > 1)
                                fprintf(stderr, "test %d\n", i);
                }

                if (run_evilcandy(program) < 0) {
                        fprintf(stderr, "[Evilcandy Fuzzer]: Fuzzer for " EVILCANDY_VERSION);
                        fprintf(stderr, "[Evilcandy Fuzzer]: Test #%d failed\n", i);
                        fprintf(stderr, "[Evilcandy Fuzzer]: Seed:    %u\n", seed);
                        fprintf(stderr, "[Evilcandy Fuzzer]: Program: %s\n", program);
                        return -1;
                }
        }
        return 0;
}

static void
print_help(FILE *fp)
{
        static const char *HELPSTR =
        "OPTIONS:\n"
        "    --nofork        Do not fork the program.  Not recommended for automated\n"
        "                    regression testing, but it's a lot faster for manual\n"
        "                    testing.\n"
        "    --seed SEED     Select seed.  If this option is not used, a random seed\n"
        "                    will be chosen and printed to stderr.\n"
        "    -n COUNT        Select number of tests to run.  By default, this is\n"
        "                    50000, unless --nofork is used, in which it will be\n"
        "                    1000000\n"
        "    -vN             Set verbosity level.  N is single-digit, with no space\n"
        "                    separating it from -v.  N is zero by default.  Only the\n"
        "                    start/stop messages and any errors detected will be\n"
        "                    printed to stderr.  If N is one, then a status update\n"
        "                    will be printed every 10000 lines.  If N is greater than\n"
        "                    one, then the test about to be run is printed for every\n"
        "                    test.\n";

        fprintf(fp, "fuzzer - fuzzer test for " EVILCANDY_VERSION "\n\n");
        fprintf(fp, "%s\n", HELPSTR);
}

int
main(int argc, char **argv)
{
        int opt, ret;
        unsigned int seed = clock();

        /* TODO: "--help" option */
        for (opt = 1; opt < argc; opt++) {
                /*
                 * --nofork: You should only use this either in conj.
                 * with --seed or by simply monitoring the output, so
                 * that if there's a failure, you can re-run the program.
                 * This is because --nofork is much faster than
                 * otherwise and failures are the exception; but failures
                 * in an unforked program means that you will likely not
                 * see the printout what actually failed.
                 */
                if (!strcmp(argv[opt], "--nofork")) {
                        opt_nofork = true;
                        opt_ntests = 1000 * 1000;
                } else if (!strcmp(argv[opt], "--seed")) {
                        char *endptr;
                        opt++;
                        if (opt >= argc)
                                goto err_parse_seed;
                        seed = strtoul(argv[opt], &endptr, 0);
                        if (endptr == argv[opt] || errno) {
err_parse_seed:
                                fprintf(stderr, "Expected: --seed <n>\n");
                                return EXIT_FAILURE;
                        }
                } else if (!strcmp(argv[opt], "-n")) {
                        char *endptr;
                        opt++;
                        if (opt >= argc)
                                goto err_parse_n;
                        opt_ntests = strtoul(argv[opt], &endptr, 0);
                        if (endptr == argv[opt] || errno) {
err_parse_n:
                                fprintf(stderr, "Execpted: -n <n>\n");
                                return EXIT_FAILURE;
                        }
                } else if (argv[opt][0] == '-' && argv[opt][1] == 'v') {
                        if (!isdigit(argv[opt][2])) {
                                fprintf(stderr, "Expected -v[0-9]");
                                return EXIT_FAILURE;
                        }
                        fuzzer_verbose = argv[opt][2] - '0';
                } else if (!strcmp(argv[opt], "--help")
                           || !strcmp(argv[opt], "-h")) {
                        print_help(stdout);
                        return EXIT_SUCCESS;
                } else {
                        fprintf(stderr, "invalid option\n");
                        return EXIT_FAILURE;
                }
        }

        /* deterministic seed, so we can repeat this test */
        srand(seed);

        /* TODO: logging mechanism, not the shell. */
        fprintf(stderr,
                "[EvilCandy Fuzzer]: Testing with seed %u...\n",
                seed);

        if (opt_nofork)
                initialize_program();
        ret = fuzz_loop(opt_ntests, seed);
        if (opt_nofork)
                end_program();

        fprintf(stderr, "[Evilcandy Fuzzer]: ...Test complete\n");
        return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

