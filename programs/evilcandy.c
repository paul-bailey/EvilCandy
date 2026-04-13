#include <evilcandy.h>
#include <internal/init.h>
#include <internal/path.h>
#include <internal/token.h>
#include <internal/vm.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

struct options_t {
        bool disassemble;
        bool disassemble_only;
        bool disassemble_minimum;
        char *disassemble_outfile;
        char *infile;
        char *program_text;
};

static void
print_version_and_quit(void)
{
        printf("%s\n", EVILCANDY_VERSION);
        end_program();
        exit(EXIT_SUCCESS);
}

static void
print_help_and_quit(FILE *fp)
{
        static const char *EVILCANDY_HELP_MSG =
                "evilcandy [OPTIONS] [INFILE]\n"
                "\n"
                "Options:\n"
                "        -d              Disassembly mode\n"
                "        -c STRING       Interpret STRING instead of a file\n"
                "        -e STRING       same as -c\n"
                "        -V              Print version and quit\n"
                "        -h              Print this help and quit\n"
                "        --version       Same as -V\n"
                "        --help          Same as -h\n"
                "\n"
                "Common usage:\n"
                "        REPL mode:      evilcandy\n"
                "        pipe mode:      cat FILE | evilcandy\n"
                "        script mode     evilcandy FILE\n";

        fprintf(fp, "%s\n", EVILCANDY_HELP_MSG);
        end_program();
        exit(EXIT_SUCCESS);
}

static int
parse_args(int argc, char **argv, struct options_t *opt)
{
        int argi;

        argi = 1;
        while (argi < argc) {
                char *s = argv[argi];
                if (*s == '-') {
                        s++;
                        switch (*s++) {
                        case 'd':
                                opt->disassemble = true;
                                if (*s != '\0')
                                        goto er;
                                break;
                        case 'c':
                        case 'e':
                                argi++;
                                if (argi == argc)
                                        goto er;
                                opt->program_text = argv[argi];
                                break;
                        case 'D':
                                opt->disassemble = true;
                                opt->disassemble_minimum = true;
                                break;
                        case '-':
                                if (!strcmp(s, "disassemble-to")) {
                                        opt->disassemble = true;
                                        argi++;
                                        if (argi == argc)
                                                goto er;
                                        opt->disassemble_outfile = argv[argi];
                                } else if (!strcmp(s, "version")) {
                                        print_version_and_quit();
                                } else if (!strcmp(s, "help")) {
                                        print_help_and_quit(stdout);
                                } else {
                                        goto er;
                                }
                                break;
                        case 'h':
                                print_help_and_quit(stdout);
                                break;
                        case 'V':
                                print_version_and_quit();
                                break;
                        default:
                                goto er;
                        }
                } else {
                        /* TODO: support multiple files */
                        if (opt->infile != NULL) {
                                fprintf(stderr, "You may only specify one input file\n");
                                goto er;
                        }
                        opt->infile = s;
                }
                argi++;
        }
        if (opt->infile != NULL && opt->program_text != NULL) {
                fprintf(stderr, "You may not specify both infile and -c <string>\n");
                goto er;
        }
        if (opt->disassemble)
                opt->disassemble_only = true;
        return 0;

er:
        fprintf(stderr, "Invalid Option\n");
        print_help_and_quit(stderr);
        return -1;
}

static void
run_script(const char *filename, FILE *fp, struct options_t *opt)
{
        Object *ex;
        Object *retval;

        ex = assemble(filename, fp, NULL);
        if (!ex || ex == ErrorVar) {
                retval = ex;
                goto done_skip_ex;
        }

        if (opt->disassemble) {
                FILE *dfp;
                if (opt->disassemble_outfile) {
                        dfp = fopen(opt->disassemble_outfile, "w");
                        if (!dfp) {
                                err_errno("Cannot output to %s",
                                          opt->disassemble_outfile);
                                retval = ErrorVar;
                                goto done;
                        }
                } else {
                        dfp = stdout;
                }

                if (opt->disassemble_minimum)
                        disassemble_minimal(dfp, ex);
                else
                        disassemble(dfp, ex, filename);
                if (dfp != stdout)
                        fclose(dfp);
                retval = NULL;
        } else {
                bug_on(opt->disassemble_only);
                retval = vm_exec_script(ex, NULL);
                if (retval == ErrorVar || err_occurred()) {
                        /* semi bug */
                        if (!err_occurred())
                                err_setstr(RuntimeError, "Unreported Error");
                        if (retval != ErrorVar)
                                VAR_DECR_REF(retval);
                        retval = ErrorVar;
                }
        }

done:
        VAR_DECR_REF(ex);
done_skip_ex:
        if (retval == ErrorVar) {
                err_print_last(stderr);
                debug_print_trace(stderr, true);
        } else if (retval) {
                VAR_DECR_REF(retval);
        }
}

static void
run_tty(struct options_t *opt)
{
        FILE *dfp = NULL;
        if (opt->disassemble) {
                if (opt->disassemble_outfile) {
                        dfp = fopen(opt->disassemble_outfile, "w");
                        if (!dfp)
                                perror("Cannot disassemble, failed to open output file");
                } else {
                        dfp = stdout;
                }
        }

        while (!feof(stdin)) {
                Object *ex = assemble("<stdin>", stdin, vm_localdict());
                if (!ex) {
                        break;
                } else if (ex == ErrorVar) {
                        err_print_last(stderr);
                } else {
                        if (dfp)
                                disassemble_lite(dfp, ex);
                        if (!opt->disassemble_only) {
                                Object *res;
                                res = vm_exec_script(ex, NULL);
                                if (res == ErrorVar) {
                                        err_print_last(stderr);
                                        token_flush_tty(NULL);
                                } else {
                                        VAR_DECR_REF(res);
                                }
                        }
                        VAR_DECR_REF(ex);
                }
        }
        fprintf(stderr, "\n");
        fflush(stderr);
}

static void
run_text(const char *text, struct options_t *opt)
{
        Object *result, *ex;

        /*
         * TODO: support this instead of fail.
         */
        if (opt->disassemble) {
                fprintf(stderr,
                        "Disassembly not supported for -c <text> option\n");
                return;
        }

        ex = assemble_string(text, false);
        if (ex == ErrorVar) {
                err_print_last(stderr);
                return;
        } else if (!ex) {
                return;
        }
        result = vm_exec_script(ex, NULL);
        if (result == ErrorVar) {
                err_print_last(stderr);
        } else {
                VAR_DECR_REF(result);
        }
        VAR_DECR_REF(ex);
}

int
main(int argc, char **argv)
{
        static struct options_t opt;
        initialize_program();

        if (parse_args(argc, argv, &opt) < 0)
                return -1;

        if (opt.program_text) {
                run_text(opt.program_text, &opt);
        } else if (opt.infile) {
                FILE *fp = push_path(opt.infile);
                if (!fp)
                        fail("Could not open '%s'", opt.infile);
                run_script(opt.infile, fp, &opt);
                pop_path(fp);
        } else if (isatty(fileno(stdin))) {
                gbl.interactive = true;
                run_tty(&opt);
        } else {
                /*
                 * in a pipe; parse entire file
                 * but don't push file path.
                 */
                run_script("<stdin>", stdin, &opt);
        }

        end_program();
        return 0;
}


