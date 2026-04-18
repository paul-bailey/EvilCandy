/* real-time fuzzer.  April 2026: This is a work in progress. */
#include <lib/buffer.h>
#include <lib/helpers.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/debug.h>

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

struct template_program_t {
        const char *name;
        const char *template_text;
};

static const struct template_program_t TEMPLATES[] = {
        {
                .name = "adder",
                .template_text =
                        "function add(a, b) {\n"
                        "  return a + b;\n"
                        "}\n"
                        "print(add(@EXPR1, @EXPR1));\n",
        }, {
                .name = "dict",
                .template_text =
                        "let @IDENTIFIER1 = { 'x': 1 };\n"
                        "print(@IDENTIFIER1.@IDENTIFIER2);\n",
        }

};

static const char *
substitute_expression(unsigned int which)
{
        static const char *EXPR_SUBST[8] = {
                "some_name", "1",
                "f'{some_other_name}'", "'a'",
                "{ 'a': 1 }", "b'x08'",
                "function(x) { return x + 1; }", "[]"
        };
        bug_on(which > 1);
        return EXPR_SUBST[2 * (rand() % 4) + which];
}

static const char *
substitute_identifier(unsigned int which)
{
        static const char *IDENTIFIER_SUBST[6] = {
                "x", "falstaff",
                "y", "hal",
                "z", "poins"
        };

        bug_on(which > 1);
        return IDENTIFIER_SUBST[2 * (rand() % 3) + which];
}

static char *
gen_program(const char *template)
{
        struct buffer_t b;
        const char *s;
        int c;

        buffer_init(&b);

        s = template;
        while ((c = *s++) != '\0') {
                int n;
                const char *start, *end;
                if (c != '@') {
                        buffer_putc(&b, c);
                        continue;
                }
                start = s;
                while (isupper((int)*s))
                        s++;
                end = s;
                bug_on(*s != '1' && *s != '2');
                n = *s - '1';
                s++;

                if (!strncmp(start, "IDENTIFIER", end - start)) {
                        buffer_puts(&b, substitute_identifier(n));
                } else if (!strncmp(start, "EXPR", end - start)) {
                        buffer_puts(&b, substitute_expression(n));
                } else {
                        bug();
                }
        }
        return buffer_trim(&b);
}

int
main(int argc, char **argv)
{
        int seed = 12345;
        int i;
        char *pgm;

        srand(seed);
        /* TODO: for now, just print to stdout to see what kinds of
         * programs are being generated.
         */
        for (i = 0; i < 1000; i++) {
                pgm = gen_program(TEMPLATES[rand() % ARRAY_SIZE(TEMPLATES)].template_text);
                printf("%s\n", pgm);
                efree(pgm);
        }
        return 0;
}

