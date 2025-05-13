#include <evilcandy.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

static void
initialize_program(void)
{
        /* Note: the order matters */
        cfile_init_ewrappers();
        cfile_init_var();
        cfile_init_vm();
        cfile_init_global();
        cfile_init_instruction_name();
}

static void
end_program(void)
{
        cfile_deinit_instruction_name();
        cfile_deinit_global();
        cfile_deinit_vm();
        /* must be last */
        cfile_deinit_var();
        /* no deinit for ewrappers.c */
}

static int
parse_args(int argc, char **argv)
{
        int argi;

        argi = 1;
        while (argi < argc) {
                char *s = argv[argi];
                if (*s == '-') {
                        s++;
                        switch (*s++) {
                        case 'd':
                                gbl.opt.disassemble = true;
                                if (*s != '\0')
                                        goto er;
                                break;
                        case 'D':
                                gbl.opt.disassemble = true;
                                gbl.opt.disassemble_minimum = true;
                                break;
                        case '-':
                                if (!strcmp(s, "disassemble-to")) {
                                        gbl.opt.disassemble = true;
                                        argi++;
                                        if (argi == argc)
                                                goto er;
                                        gbl.opt.disassemble_outfile = argv[argi];
                                } else {
                                        goto er;
                                }
                                break;
                        case 'V':
                                printf("%s\n", PACKAGE_STRING);
                                end_program();
                                exit(EXIT_SUCCESS);
                                break;
                        default:
                                goto er;
                        }
                } else {
                        if (gbl.opt.infile != NULL) {
                                fprintf(stderr, "You may only specify one input file\n");
                                goto er;
                        }
                        gbl.opt.infile = s;
                }
                argi++;
        }
        if (gbl.opt.disassemble)
                gbl.opt.disassemble_only = true;
        return 0;

er:
        fprintf(stderr, "Expected: '%s [OPTIONS] INFILE'\n", argv[0]);
        return -1;
}

static void
run_script(const char *filename, FILE *fp, Frame *fr)
{
        Object *ex;
        Object *retval;
        int status;

        ex = assemble(filename, fp, true, &status);
        if (!ex)
                return;
        bug_on(status != RES_OK);

        if (gbl.opt.disassemble) {
                FILE *dfp;
                if (gbl.opt.disassemble_outfile) {
                        dfp = fopen(gbl.opt.disassemble_outfile, "w");
                        if (!dfp) {
                                err_errno("Cannot output to %s",
                                          gbl.opt.disassemble_outfile);
                                retval = ErrorVar;
                                goto done;
                        }
                } else {
                        dfp = stdout;
                }

                if (gbl.opt.disassemble_minimum)
                        disassemble_minimal(dfp, ex);
                else
                        disassemble(dfp, ex, filename);
                if (dfp != stdout)
                        fclose(dfp);
                retval = NULL;
        } else {
                bug_on(gbl.opt.disassemble_only);
                retval = vm_exec_script(ex, fr);
                if (retval == ErrorVar || err_occurred()) {
                        /* semi bug */
                        if (!err_occurred())
                                err_setstr(RuntimeError, "Unreported Error");
                        retval = ErrorVar;
                }
        }

done:
        VAR_DECR_REF(ex);

        if (retval == ErrorVar)
                err_print_last(stderr);
        else if (retval)
                VAR_DECR_REF(retval);
}

static void
run_tty(void)
{
        FILE *dfp = NULL;
        if (gbl.opt.disassemble) {
                if (gbl.opt.disassemble_outfile) {
                        dfp = fopen(gbl.opt.disassemble_outfile, "w");
                        if (!dfp)
                                perror("Cannot disassemble, failed to open output file");
                } else {
                        dfp = stdout;
                }
        }

        while (!feof(stdin)) {
                int status;
                Object *ex;

                ex = assemble("<stdin>", stdin, false, &status);
                if (ex == NULL) {
                        if (status == RES_OK) {
                                /* normal EOF, user typed ^d */
                                break;
                        }
                        err_print_last(stderr);
                } else {
                        bug_on(status != RES_OK);

                        if (dfp)
                                disassemble_lite(dfp, ex);
                        if (!gbl.opt.disassemble_only) {
                                Object *res;
                                res = vm_exec_script(ex, NULL);
                                if (res == ErrorVar)
                                        err_print_last(stderr);
                                else
                                        VAR_DECR_REF(res);
                        }
                        VAR_DECR_REF(ex);
                }
        }
}

int
main(int argc, char **argv)
{
        initialize_program();

        if (parse_args(argc, argv) < 0)
                return -1;

        if (gbl.opt.infile) {
                FILE *fp = push_path(gbl.opt.infile);
                if (!fp)
                        fail("Could not open '%s'", gbl.opt.infile);
                run_script(gbl.opt.infile, fp, NULL);
                pop_path(fp);
        } else {
                if (isatty(fileno(stdin))) {
                        run_tty();
                } else {
                        /*
                         * in a pipe; parse entire file
                         * but don't push file path.
                         */
                        run_script("<stdin>", stdin, NULL);
                }
        }

        end_program();
        return 0;
}


