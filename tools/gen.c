#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

static char buf[1024];

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
        while ((res = scanf("%s", buf)) == 1) {
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
        while ((res = scanf("%s", buf)) == 1) {
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
        while ((res = scanf("%s", buf)) == 1) {
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
