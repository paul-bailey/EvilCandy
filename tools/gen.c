#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

static char buf[1024];

static int
istokchar(int c)
{
        /* isdigit in case we add things like PUSH_3 */
        return isupper(c) || isdigit(c) || c == '_';
}

static int
iseol(int c)
{
        return c == '#' || c == '\n' || c == '\0';
}

static char *
skipws(char *s)
{
        while (isspace((int)(*s)))
                s++;
        return s;
}

/* return: 1 for 'buf has 1 token', 0 for 'end of file' */
static int
next_instruction(void)
{
        static char *line = NULL;
        static size_t len = 0;

        char *src, *dst;
        ssize_t res;

        do {
                res = getline(&line, &len, stdin);
                if (res < 0) {
                        if (errno) {
                                perror("getline failed");
                                exit(1);
                        }
                        return 0;
                }

                src = skipws(line);
        } while (iseol(*src));

        dst = buf;
        while (istokchar(*src)) {
                if (dst >= &buf[sizeof(buf)-1]) {
                        fprintf(stderr, "buffer would overflow\n");
                        exit(1);
                }
                *dst++ = *src++;
        }
        if (dst == buf) {
                fprintf(stderr, "Expected: alphanumeric character\n");
                exit(1);
        }

        *dst++ = '\0';

        if (isdigit((int)(buf[0]))) {
                fprintf(stderr, "first character of name cannot be number\n");
                exit(1);
        }

        /* We should be getting just one non-comment token per line */
        src = skipws(src);

        if (!iseol(*src)) {
                fprintf(stderr, "Unexpected tokens in input");
                exit(1);
        }
        return 1;
}

static void
prlower(void)
{
        int c;
        char *s = buf;
        while ((c = *s++) != '\0')
                putchar(tolower(c));
}

static void
prupper(void)
{
        int c;
        char *s = buf;
        while ((c = *s++) != '\0')
                putchar(toupper(c));
}

static int
dis(void)
{
        int res;
        printf("/*\n"
               " * Auto-generated code, do not edit\n"
               " * used by disassemble.c\n"
               " * (see tools/gen.c, tools/instructions)\n"
               " */\n");
        while ((res = next_instruction()) == 1) {
                printf("        \"");
                prupper();
                printf("\",\n");
        }
        if (errno || !feof(stdin)) {
                perror("Input error");
                return 1;
        }
        return 0;
}

static int
def(void)
{
        int res;
        static const char *excl = "EGQ_INSTRUCTION_DEFS_H";
        bool first = true;
        printf("/* Auto-generated code, do not edit */\n");
        printf("/* (see tools/gen.c) */\n");
        printf("#ifndef %s\n", excl);
        printf("#define %s\n", excl);
        printf("enum {\n");
        while ((res = next_instruction()) == 1) {
                printf("        INSTR_");
                prupper();
                if (first) {
                        printf(" = 0,\n");
                        first = false;
                } else {
                        printf(",\n");
                }
        }
        if (errno || !feof(stdin)) {
                perror("Input error");
                return 1;
        }
        printf("        N_INSTR,\n");
        printf("};\n");
        printf("#endif /* %s */\n", excl);
        return 0;
}

static int
jump(void)
{
        int res;
        printf("/*\n"
               " * Auto-generated code, do not edit\n"
               " * used by vm.c\n"
               " * (see tools/gen.c, tools/instructions)\n"
               " */\n");
        while ((res = next_instruction()) == 1) {
                printf("        do_");
                prlower();
                putchar(',');
                putchar('\n');
        }
        if (errno || !feof(stdin)) {
                perror("Input error");
                return 1;
        }
        return 0;
}

int
main(int argc, char **argv)
{
        if (argc != 2)
                goto er;

        if (!strcmp(argv[1], "jump"))
                return jump();
        else if (!strcmp(argv[1], "def"))
                return def();
        else if (!strcmp(argv[1], "dis"))
                return dis();

er:
        fprintf(stderr, "Expected: %s jump|def|dis\n", argv[0]);
        return 1;
}
