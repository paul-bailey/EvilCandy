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
        list_init(&q_.ns);

        init_modules();

        /* Initialize PC (its initial location will be set later) */
        var_init(&q_.pc);
        q_.pc.magic = QPTRXU_MAGIC;

        /* Initialize stack regs */
        q_.sp = q_.stack;
        q_.fp = q_.stack;

        /* Initialize program counter */
        var_init(&q_.pc);
        q_.pc.magic = QPTRXU_MAGIC;
        cur_ns = NULL;
        cur_oc = NULL;

        /* Initialize link register */
        var_init(&q_.lr);
        qop_mov(&q_.lr, &q_.pc);

        /* point initial fp to "__gbl__" */
        stack_push(q_.gbl);
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


