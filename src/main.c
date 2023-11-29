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
        return "[unnamed]";
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
                { .initfn = moduleinit_frame },
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
        struct frame_t *fr;

        list_init(&q_.ns);

        init_modules();

        /* Initialize program counter */
        cur_ns = NULL;
        cur_oc = NULL;

        /* Initialize frame */
        fr = frame_alloc();
        frame_add_owners(fr, q_.gbl, var_new());
        frame_push(fr);
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
        frame_pop();
        return 0;
}


