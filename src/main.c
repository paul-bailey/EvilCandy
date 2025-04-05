#include <evilcandy.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>

struct global_t q_;

static void
init_lib(void)
{
        /*
         * "moduleinit" was a poorly chosen name for these constructors.
         * They are "modules" because they are for C files that have
         * private data to initialize; they are not related to the
         * loadable script "modules" in this source tree's lib/ folder,
         * or to the C accelerators in builtin/, unless by coincidence.
         */
        static const struct initfn_tbl_t {
                void (*initfn)(void);
        } INITFNS[] = {
                /* Note: the order of this table matters */
                { .initfn = moduleinit_literal },
                { .initfn = moduleinit_var },
                { .initfn = moduleinit_builtin },
                { .initfn = moduleinit_token },
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

static void
run_script(const char *filename, FILE *fp)
{
        struct executable_t *ex;
        struct assemble_t *a;
        a = new_assembler(filename, fp);
        if (!a)
                return;
        if ((ex = assemble_next(a, true, NULL)) == NULL)
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
        if (!a)
                return;

        for (;;) {
                int status;
                ex = assemble_next(a, false, &status);
                if (ex != NULL)
                        vm_execute(ex);
                else if (status == EPIPE)
                        break;
                /*
                 * XXX REVISIT: would be great if I could delete
                 * assembler and create new one for every assemble_next
                 * while in TTY mode--still faster than user typing--
                 * with the idea of resetting user prompt.  For example,
                 * Python prompts with ">>> " for first line of a
                 * compound statement and "... " for the remaining lines,
                 * resetting for the start of the next statement.
                 * But Python has a newline at the end of every
                 * statement, while EvilCandy could hypothetically have
                 * more than one statement per line.
                 */
                trim_assembler(a);
        }
        free_assembler(a, true);
}

/**
 * load_file - Read in a file, tokenize it, assemble it, execute it.
 * @filename:   Path to file as written after the "load" keyword or on
 *              the command line.
 */
void
load_file(const char *filename)
{
        FILE *fp = push_path(filename);
        run_script(filename, fp);
        pop_path(fp);
}

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
                        if (q_.opt.disassemble_only) {
                                fprintf(stderr,
                                        "Error: Disassembly not available in interactive mode\n");
                                return 1;
                        }
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


