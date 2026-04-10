#include <evilcandy.h>
#include <evilcandy/init.h>
#include <stdlib.h>
#include <assert.h>

/*
 * Set to 1 to see occasional "test #nnn" progress.
 * Set to 2 when debugging, to see "Program: bigLiStOfGiBberIsh",
 *      which the assembler is trying to parse, before each test
 *      is run.  (Extremely verbose, will drag progress to a crawl).
 */
#define FUZZER_VERBOSE 0

struct strbuf_t {
        char *buf;
        size_t len;
        size_t cap;
};

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
        snprintf(buf, sizeof(buf), "%d", rnd(100));
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

/*
 * TODO: move this to a child process, so recovery can occur in case
 * of a spinlock or a crash.
 */
/* return true if this got as far as execution */
static bool
run_evilcandy(const char *program, int test_no)
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

static int
fuzz_loop(unsigned int n_tests)
{
        static char program[8096];
        int i, count = 0;
        for (i = 0; i < n_tests; i++) {
                int m, j;

                gen_program(program, sizeof(program));
                m = rnd(5);

                for (j = 0; j < m; j++) {
                        mutate(program, strlen(program), sizeof(program));
                }

                if (FUZZER_VERBOSE) {
                        if (i % (10 * 1000) == 0)
                                fprintf(stderr, "%d\n", i);
                        if (FUZZER_VERBOSE > 1)
                                fprintf(stderr, "test %d\n", i);
                }

                count += run_evilcandy(program, i);
        }
        return count;
}

int
main(int argc, char **argv)
{
        enum { N_TESTS = 1000 * 1000 };
        const unsigned int seed = 12345;
        int count;

        /* TODO: Let seed, verbose levels be command-line options */

        /* deterministic seed, so we can repeat this test */
        srand(seed);

        printf("[EvilCandy Fuzzer]: Testing with seed %u...\n", seed);

        initialize_program();
        fuzz_loop(N_TESTS);
        end_program();

        printf("[Evilcandy Fuzzer]: ...Test complete\n");
}

