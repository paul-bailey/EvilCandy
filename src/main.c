#include "egq.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

struct global_t q_;

/* for debugging and builtin functions */
const char *
typestr(int magic)
{
        if (magic < 0 || magic >= Q_NMAGIC)
                return "[bug]";
        return TYPEDEFS[magic].name;
}

const char *
nameof(struct var_t *v)
{
        return v->name ? v->name : "[unnamed]";
}

static void
init_modules(void)
{
        static const struct initfn_tbl_t {
                void (*initfn)(void);
        } INITFNS[] = {
                /* Note: the order of this table matters */
                { .initfn = moduleinit_keyword },
                { .initfn = moduleinit_literal },
                { .initfn = moduleinit_eval },
                { .initfn = moduleinit_var },
                { .initfn = moduleinit_builtin },
                { .initfn = moduleinit_stack },
                { .initfn = moduleinit_lex },
                { .initfn = NULL },
        };
        const struct initfn_tbl_t *t;

        for (t = INITFNS; t->initfn != NULL; t++)
                t->initfn();
}


static void
init_lib(void)
{
        static struct var_t notafunc;
        list_init(&q_.ns);

        init_modules();

        /* Initialize stack regs */
        q_.sp = 0;
        q_.fp = 0;

        /* Initialize program counter */
        cur_ns = NULL;
        cur_oc = NULL;

        var_init(&notafunc);

        /* point initial fp to "__gbl__" */
        stack_push(q_.gbl);
        /*
         * At top level, push empty not-a-function
         * variable onto the "this-function" part of the stack
         */
        stack_push(&notafunc);
}

/**
 * script_read - Read and execute a script
 */
int
main(int argc, char **argv)
{
        init_lib();

        if (argc < 2) {
                fprintf(stderr, "Expected: file name\n");
                return 1;
        }

        load_file(argv[1]);
        return 0;
}


