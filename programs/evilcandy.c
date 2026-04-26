#include <evilcandy/version.h>
#include <evilcandy/assemble.h>
#include <evilcandy/debug.h>
#include <evilcandy/err.h>
#include <evilcandy/errmsg.h>
#include <evilcandy/disassemble.h>
#include <evilcandy/global.h>
#include <evilcandy/var.h>
#include <evilcandy/vm.h>
#include <internal/init.h>
#include <internal/path.h>
#include <internal/token.h>
#include <internal/vm.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

struct options_t {
        bool disassemble;
        bool disassemble_only;
        bool check_only;
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
                "        --check         Compile but do not execute\n"
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
                                } else if (!strcmp(s, "check")) {
                                        opt->check_only = true;
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

        /* FIXME: isatty() used in multiple places to determine type of input */
        if (opt->check_only && !opt->infile
            && !opt->program_text && isatty(fileno(stdin))) {
                fprintf(stderr,
                        "--check option unavailable for interactive mode.\n");
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

static int
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
                goto done;
        }

        if (opt->check_only) {
                retval = NULL;
                goto done;
        }

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

done:
        VAR_DECR_REF(ex);
done_skip_ex:
        if (retval == ErrorVar) {
                err_print_last(stderr);
                return -1;
        }
        if (retval)
                VAR_DECR_REF(retval);
        return 0;
}

static int
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
                        token_flush_tty(NULL);
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
        return 0;
}

static int
run_text(const char *text, struct options_t *opt)
{
        Object *result, *ex;
        int ret;

        ex = assemble_string(text, false);
        if (ex == ErrorVar) {
                err_print_last(stderr);
                return -1;
        } else if (!ex) {
                return 0;
        }

        if (opt->disassemble) {
                FILE *dfp;
                bug_on(!opt->disassemble_only);
                if (opt->disassemble_outfile) {
                        dfp = fopen(opt->disassemble_outfile, "w");
                        if (!dfp) {
                                perror("Cannot disassemble, failed to open output file");
                                VAR_DECR_REF(ex);
                                return -1;
                        }
                } else  {
                        dfp = stdout;
                }
                disassemble_lite(dfp, ex);
                ret = 0;
                goto out;
        }

        if (opt->check_only) {
                ret = 0;
                goto out;
        }

        result = vm_exec_script(ex, NULL);
        if (result == ErrorVar) {
                err_print_last(stderr);
                ret = -1;
        } else {
                VAR_DECR_REF(result);
                ret = 0;
        }

out:
        VAR_DECR_REF(ex);
        return ret;
}

int
main(int argc, char **argv)
{
        static struct options_t opt;
        int ret;
        initialize_program();

        if (parse_args(argc, argv, &opt) < 0)
                return EXIT_FAILURE;

        if (opt.program_text) {
                ret = run_text(opt.program_text, &opt);
        } else if (opt.infile) {
                FILE *fp = push_path(opt.infile);
                if (!fp)
                        fail("Could not open '%s'", opt.infile);
                ret = run_script(opt.infile, fp, &opt);
                pop_path(fp);
        } else if (isatty(fileno(stdin))) {
                gbl_set_interactive(true);
                ret = run_tty(&opt);
        } else {
                /*
                 * in a pipe; parse entire file
                 * but don't push file path.
                 */
                ret = run_script("<stdin>", stdin, &opt);
        }

        end_program();
        return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}


