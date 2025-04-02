#include <evilcandy.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>

struct global_t q_;

static void
init_lib(void)
{
        static const struct initfn_tbl_t {
                void (*initfn)(void);
        } INITFNS[] = {
                /* Note: the order of this table matters */
                { .initfn = moduleinit_keyword },
                { .initfn = moduleinit_literal },
                { .initfn = moduleinit_var },
                { .initfn = moduleinit_builtin },
                { .initfn = moduleinit_lex },
                { .initfn = moduleinit_vm },
                { .initfn = NULL },
        };
        const struct initfn_tbl_t *t;

        for (t = INITFNS; t->initfn != NULL; t++)
                t->initfn();
}

static int
parse_args(int argc, char **argv)
{
        int argi;
        bool expect_disfile = false;

        for (argi = 1; argi < argc; argi++) {
                char *s = argv[argi];
                if (*s == '-') {
                        s++;
                        switch (*s++) {
                        case 'd':
                                q_.opt.disassemble = true;
                                expect_disfile = true;
                                if (*s != '\0')
                                        goto er;
                                continue;
                        case 'D':
                                q_.opt.disassemble = true;
                                q_.opt.disassemble_only = true;
                                expect_disfile = true;
                                if (*s != '\0')
                                        goto er;
                                continue;
                        default:
                                goto er;
                        }
                } else if (expect_disfile) {
                        expect_disfile = false;
                        if (q_.opt.disassemble_outfile != NULL) {
                                fprintf(stderr, "-D and -d options must be exclusive\n");
                                goto er;
                        }
                        q_.opt.disassemble_outfile = s;
                } else {
                        if (q_.opt.infile != NULL) {
                                fprintf(stderr, "You may only specify one input file\n");
                                goto er;
                        }
                        q_.opt.infile = s;
                }
        }
        return 0;

er:
        fprintf(stderr, "Expected: '%s [OPTIONS] INFILE'\n", argv[0]);
        return -1;
}

/*
 * parallel to run_tty, but has extern linkage so that load_file()
 * can wrap around it.
 */
void
run_script(const char *filename, FILE *fp)
{
        struct executable_t *ex;
        struct assemble_t *a;
        a = new_assembler(filename, fp);
        if ((ex = assemble_next(a, true)) == NULL)
                syntax("Failed to assemble");
        free_assembler(a, ex == NULL);

        if (ex && !q_.opt.disassemble_only)
                vm_execute(ex);
}

static void
run_tty(void)
{
        struct executable_t *ex;
        struct assemble_t *a;
        a = new_assembler("(stdin)", stdin);

        printf("\n>>>> ");
        while ((ex = assemble_next(a, false)) != NULL) {
                vm_execute(ex);
                trim_assembler(a);
                printf("\n>>>> ");
        }
        free_assembler(a, true);
}

/**
 * script_read - Read and execute a script
 */
int
main(int argc, char **argv)
{
        init_lib();

        if (parse_args(argc, argv) < 0)
                return -1;

        if (q_.opt.infile) {
                load_file(q_.opt.infile);
        } else {
                if (isatty(fileno(stdin))) {
                        run_tty();
                } else {
                        /*
                         * in a pipe; parse entire file
                         * but don't push file path.
                         */
                        run_script("(stdin)", stdin);
                }
        }

        return 0;
}


