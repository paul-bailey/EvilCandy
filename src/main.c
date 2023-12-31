#include <evilcandy.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <getopt.h>

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
        if (!q_.opt.infile) {
                fprintf(stderr, "Input file not specified");
                goto er;
        }
        return 0;

er:
        fprintf(stderr, "Expected: '%s [OPTIONS] INFILE'\n", argv[0]);
        return -1;
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

        load_file(q_.opt.infile);

        return 0;
}


