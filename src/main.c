#include "egq.h"
#include <string.h>
#include <ctype.h>

struct q_private_t q_;

#ifndef arraylen
# define arraylen(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* for debugging and builtin functions */
const char *
q_typestr(int magic)
{
        if (magic < 0 || magic >= Q_NMAGIC)
                return "[bug]";
        return TYPEDEFS[magic].name;
}

const char *
q_nameof(struct qvar_t *v)
{
        return v->name ? v->name : "[unnamed]";
}

static void
initialize_keywords(void)
{
        static const struct kw_tbl_t {
                const char *name;
                int v;
        } KEYWORDS[] = {
                { "append",     KW_APPEND },
                { "function",   KW_FUNC },
                { "let",        KW_LET },
                { "return",     KW_RETURN },
                { "this",       KW_THIS },
                { "break",      KW_BREAK },
                { "import",     KW_IMPORT },
                { "if",         KW_IF },
                { "while",      KW_WHILE },
                { "else",       KW_ELSE },
                { "do",         KW_DO },
                { NULL, 0 }
        };
        const struct kw_tbl_t *tkw;

        q_.kw_htbl = hashtable_create(HTBL_COPY_KEY|HTBL_COPY_DATA, NULL);
        if (!q_.kw_htbl)
                fail("hashtable_create failed");

        for (tkw = KEYWORDS; tkw->name != NULL; tkw++) {
                int res = hashtable_put(q_.kw_htbl, tkw->name,
                                        (void *)&tkw->v, sizeof(tkw->v), 0);
                bug_on(res < 0);
        }
}

static void
init_lib(void)
{
        list_init(&q_.ns);

        initialize_keywords();

        q_.literals = hashtable_create(0, NULL);
        if (!q_.literals)
                fail("hashtable_create failed");

        q_.gbl = qobject_new(NULL, "__gbl__");

        initialize_lexer();

        /* Initialize PC (its initial location will be set later) */
        qvar_init(&q_.pc);
        q_.pc.magic = QPTRX_MAGIC;

        /* Set up the global object */
        q_builtin_initlib();

        /* Initialize stack regs */
        q_.sp = q_.stack;
        q_.fp = q_.stack;

        /* Initialize program counter */
        qvar_init(&q_.pc);
        q_.pc.magic = QPTRX_MAGIC;
        cur_ns = NULL;
        cur_oc = NULL;

        /* Initialize link register */
        qvar_init(&q_.lr);
        qop_mov(&q_.lr, &q_.pc);

        /* point initial fp to "__gbl__" */
        qstack_push(q_.gbl);
}

/**
 * script_read - Read and execute a script
 */
int
main(int argc, char **argv)
{
        init_lib();

        if (argc < 2)
                fprintf(stderr, "Expected: file name\n");

        load_file(argv[1]);
        return 0;
}


