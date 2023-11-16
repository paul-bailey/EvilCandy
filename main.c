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
        const char *typetbl[] = { "object", "function", "float", "int",
                                  "string", "empty", "pointer", "built_in_function" };
        magic -= QOBJECT_MAGIC;
        if (magic < 0 || magic >= arraylen(typetbl))
                return "[bug]";
        return typetbl[magic];
}

const char *
q_nameof(struct qvar_t *v)
{
        return v->name ? v->name : "[unnamed]";
}

static void
init_lib(void)
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
                { NULL, 0 }
        };
        /*
         * IMPORTANT!! These two strings must be in same order as
         *             their QD_* enums
         */
        static const char *const DELIMS = "+-<>=&|.!;,/*%^()[]{} \t\n";
        static const char *const DELIMDBL = "+-<>=&|";

        const char *s;
        const struct kw_tbl_t *tkw;
        int i;

        memset(&q_, 0, sizeof(q_));

        /* Initialize hash tables */
        q_.kw_htbl = hashtable_create(HTBL_COPY_KEY|HTBL_COPY_DATA, NULL);
        if (!q_.kw_htbl)
                fail("hashtable_create failed");

        for (tkw = KEYWORDS; tkw->name != NULL; tkw++) {
                int res = hashtable_put(q_.kw_htbl, tkw->name,
                                        (void *)&tkw->v, sizeof(tkw->v), 0);
                bug_on(res < 0);
        }

        q_.literals = hashtable_create(0, NULL);
        if (!q_.literals)
                fail("hashtable_create failed");

        q_.gbl = qobject_new(NULL, "__gbl__");

        /* Set up q_.charmap */
        /* delimiter */
        for (s = DELIMS; *s != '\0'; s++)
                q_.charmap[(int)*s] |= QDELIM;
        /* double-delimeters */
        for (s = DELIMDBL; *s != '\0'; s++)
                q_.charmap[(int)*s] |= QDDELIM;
        /* permitted identifier chars */
        for (i = 'a'; i < 'z'; i++)
                q_.charmap[i] |= QIDENT | QIDENT1;
        for (i = 'A'; i < 'Z'; i++)
                q_.charmap[i] |= QIDENT | QIDENT1;
        for (i = '0'; i < '9'; i++)
                q_.charmap[i] |= QIDENT;
        q_.charmap['_'] |= QIDENT | QIDENT1;

        /* Set up q_.char_x*tbl */
        for (s = DELIMS, i = QD_PLUS; !isspace((int)*s); s++, i++)
                q_.char_xtbl[(int)*s] = i;
        for (s = DELIMDBL, i = QD_PLUSPLUS; *s != '\0'; s++, i++)
                q_.char_x2tbl[(int)*s] = i;

        token_init(&q_.tok);
        /* Hack to ensure q_.tok.s can always be de-referenced */
        token_putc(&q_.tok, 'a');
        token_reset(&q_.tok);

        /* Initialize PC (its initial location will be set later) */
        qvar_init(&q_.pc);
        qvar_init(&q_.pclast);
        q_.pc.magic = q_.pclast.magic = QPTRX_MAGIC;

        q_builtin_initlib();

        /*
         * Some other modules automatically initialize on first
         * call to some of their functions.
         */

        q_.lib_init = true;
}

/**
 * script_read - Read and execute a script
 */
int
main(int argc, char **argv)
{
        struct ns_t *ns;

        init_lib();

        q_.lineno = 0;
        q_.infile = NULL;
        q_.infilename = NULL;

        if (argc < 2)
                fprintf(stderr, "Expected: file name\n");

        file_push(argv[1]);


        /* create & init new namespace */
        ns = ecalloc(sizeof(*ns));
        ns->fname = estrdup(q_.infilename);
        ns->lineno = q_.lineno + 1;
        token_init(&ns->pgm);

        for (;;) {
                char *line = next_line(0);
                if (!line)
                        break;
                token_puts(&ns->pgm, line);
        }

        if (!ns->pgm.s || ns->pgm.s[0] == '\0')
                return 0;

        if (!q_.ns_top) {
                q_.ns_top = ns;
        } else {
                struct ns_t *p;
                for (p = q_.ns_top; p->next != NULL; p = p->next)
                        ;
                p->next = ns;
        }
        exec_script(ns);
}


