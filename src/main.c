#include <evilcandy.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

/* configuration stuff */
static struct {
        struct {
                bool disassemble;
                bool disassemble_only;
                char *disassemble_outfile;
                char *infile;
        } opt;
} q_;

/*
 * Global User-Type Variables
 *
 *      When returning NullVar to mean 'null' (as the script user sees
 *      it), produce a reference, just as if you would for any other
 *      variable.  Ditto with GlobalObject, this is what the user sees
 *      as '__gbl__'.
 *
 *      Do not produce a reference for the ErrorVar, since it tells you
 *      an error occurred.  You should never use ErrorVar such that it
 *      could be 'seen' by the user, eg. never push it onto the user
 *      stack.
 *
 *      The others (ParserError et al.) are visible to the user in
 *      __gbl__._builtins.  Produce a reference if they are requested
 *      with the SYMTAB instruction, but do not produce a reference
 *      when passing these as the first argument to err_setstr.
 */
Object *ErrorVar;
Object *NullVar;
Object *GlobalObject;
Object *ParserError;
Object *RuntimeError;
Object *SystemError;

static void
init_lib(void)
{
        /* ewrappers.c */
        extern void moduleinit_ewrappers(void);
        /* var.c */
        extern void moduleinit_var(void);
        /* vm.c */
        extern void moduleinit_vm(void);
        /* builtin/builtin.c */
        extern void moduleinit_builtin(void);
        /* token.c */
        extern void moduleinit_token(void);

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
                { .initfn = moduleinit_ewrappers },
                { .initfn = moduleinit_var },
                { .initfn = moduleinit_vm },
                { .initfn = moduleinit_builtin },
                { .initfn = moduleinit_token },
                { .initfn = NULL },
        };
        const struct initfn_tbl_t *t;

        for (t = INITFNS; t->initfn != NULL; t++)
                t->initfn();

        /*
         * GlobalObject and the XxxError vars should have been
         * initialized by moduleinit_builtin.  These two remain.
         */
        ErrorVar = stringvar_new("If you can see this from the console, this is a BUG!!!\n");
        NullVar  = emptyvar_new();
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
                                q_.opt.disassemble = true;
                                if (*s != '\0')
                                        goto er;
                                break;
                        case '-':
                                if (!strcmp(s, "disassemble-only")) {
                                        q_.opt.disassemble = true;
                                        q_.opt.disassemble_only = true;
                                } else if (!strcmp(s, "disassemble-to")) {
                                        q_.opt.disassemble = true;
                                        argi++;
                                        if (argi == argc)
                                                goto er;
                                        q_.opt.disassemble_outfile = argv[argi];
                                } else if (!strcmp(s, "disassemble-only-to")) {
                                        q_.opt.disassemble = true;
                                        q_.opt.disassemble_only = true;
                                        argi++;
                                        if (argi == argc)
                                                goto er;
                                        q_.opt.disassemble_outfile = argv[argi];
                                } else {
                                        goto er;
                                }
                                break;
                        }
                } else {
                        if (q_.opt.infile != NULL) {
                                fprintf(stderr, "You may only specify one input file\n");
                                goto er;
                        }
                        q_.opt.infile = s;
                }
                argi++;
        }
        return 0;

er:
        fprintf(stderr, "Expected: '%s [OPTIONS] INFILE'\n", argv[0]);
        return -1;
}

static void
run_script(const char *filename, FILE *fp, Frame *fr)
{
        Object *ex;
        int status;

        ex = assemble(filename, fp, true, &status);
        if (ex != NULL && status == RES_OK) {
                Object *retval;
                if (q_.opt.disassemble) {
                        static bool once = false;
                        FILE *dfp;
                        if (once)
                                return;
                        once = true;

                        if (q_.opt.disassemble_outfile) {
                                dfp = fopen(q_.opt.disassemble_outfile, "w");
                                if (!dfp) {
                                        err_errno("Cannot output to %s",
                                                  q_.opt.disassemble_outfile);
                                        goto er;
                                }
                        } else {
                                dfp = stdout;
                        }

                        disassemble(dfp, ex, filename);
                        if (dfp != stdout)
                                fclose(dfp);
                }
                if (!q_.opt.disassemble_only) {
                        retval = vm_exec_script(ex, fr);
                } else {
                        VAR_INCR_REF(NullVar);
                        retval = NullVar;
                }

                VAR_DECR_REF(ex);

                if (retval == ErrorVar)
                        goto er;

                if (retval)
                        VAR_DECR_REF(retval);
        }
        return;

er:
        /*
         * Philosophical ish--
         *
         * We could have recursed into a file imported with the "load"
         * command.  Call exit? or just return to parent script, where
         * another error will likely be symbol-not-found?  This would
         * cascade with a nest of uncompleted scripts, all the way to
         * the top, which will finally return early.  Meanwhile there'd
         * be a paper trail on stderr, making the culprit easy to find.
         *
         * ...but for now I'll just exit early.
         */
        err_print_last(stderr);
        exit(1);
}

static void
run_tty(void)
{
        FILE *dfp = NULL;
        if (q_.opt.disassemble) {
                if (q_.opt.disassemble_outfile) {
                        dfp = fopen(q_.opt.disassemble_outfile, "w");
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
                        if (!q_.opt.disassemble_only) {
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
        init_lib();

        if (parse_args(argc, argv) < 0)
                return -1;

        if (q_.opt.infile) {
                FILE *fp = push_path(q_.opt.infile);
                if (!fp)
                        fail("Could not open '%s'", q_.opt.infile);
                run_script(q_.opt.infile, fp, NULL);
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

        /*
         * TODO: Before this, if debug mode, atexit() some function that
         * will scan through GlobalObject and count how many entries have
         * a refcount > 1.  The only ones ought to be 'XxxError'.
         */
        VAR_DECR_REF(GlobalObject);
        VAR_DECR_REF(ErrorVar);
        return 0;
}


