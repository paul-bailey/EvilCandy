#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

/* command-line options */
static bool css_only = false;
static bool standalone = true;

static const char *header_start =
"<!DOCTYPE html>\n"
"<html>\n"
"  <head>\n"
"    <style>\n";

static const char *header_end =
"    </style>\n"
"  </head>\n";

static const char *body_start = "  <body>\n";

static const char *css_str =
".evilcandy-keyword {\n"
"        color: #000088;\n"
"        font-weight: bold;\n"
"}\n"
".evilcandy-identifier {\n"
"        color: black;\n"
"}\n"
".evilcandy-builtin {\n"
"        color: #002222;\n"
"        font-weight: bold;\n"
"}\n"
".evilcandy-lambda {\n"
"        color: #884444;\n"
"        font-weight: bold;\n"
"}\n"
".evilcandy-number {\n"
"        color: #880000;\n"
"}\n"
".evilcandy-string {\n"
"        color: #004444;\n"
"}\n"
".evilcandy-comment {\n"
"        color: #008800;\n"
"}\n";

static const char *body_end =
"  </body>\n"
"</html>\n";

static int lineno = 0;

static void
myputchar(int c)
{
        switch (c) {
        case '<':
                printf("&lt;");
                break;
        case '>':
                printf("&gt;");
                break;
        case '&':
                printf("&amp;");
                break;
        default:
                putchar(c);
        }
}

static char *
next_line(void)
{
        static char *line = NULL;
        static size_t size = 0;
        ssize_t res;

        res = getline(&line, &size, stdin);
        if (res == -1)
                return NULL;
        lineno++;
        return line;
}

static bool getting_comment = false;
static int getting_string = '\0';

static bool
isidentchar(int c)
{
        return isalnum(c) || c == '_';
}

static bool
isidentchar1(int c)
{
        return isalpha(c) || c == '_';
}

static bool
kw_bi_common(char **pps, const char **kwbi, char *cls)
{
        char *s = *pps;
        while (*kwbi) {
                size_t len = strlen(*kwbi);
                if (!strncmp(s, *kwbi, len) && !isidentchar(s[len])) {
                        printf("<span class='%s'>%s</span>", cls, *kwbi);
                        *pps = s + strlen(*kwbi);
                        return true;
                }
                kwbi++;
        }
        return false;
}

static bool
do_keyword(char **pps)
{
        static const char *keywords[] = {
                "function",
                "let",
                "return",
                "this",
                "break",
                "if",
                "while",
                "else",
                "do",
                "for",
                "load",
                "const",
                "private",
                "true",
                "false",
                "null",
                NULL,
        };
        return kw_bi_common(pps, keywords, "evilcandy-keyword");
}

static bool
do_common_identifier(char **pps)
{
        static const char *common[] =  {
                "print",
                "hasattr",
                "len",
                "foreach",
                "__gbl__",
                "format",
                NULL,
        };
        return kw_bi_common(pps, common, "evilcandy-builtin");
}

static bool
do_identifier(char **pps)
{
        char *s = *pps;
        if (!isidentchar1(*s))
                return false;

        printf("<span class='evilcandy-identifier'>");
        while (isidentchar(*s))
                myputchar(*s++);
        printf("</span>");
        *pps = s;
        return true;
}

static bool
do_delimiter(char **pps)
{
        static const char *const DELIMS = "+-<>=&|.!;,/*%^()[]{}:~";
        char *s = *pps;

        /* do the special cass, lambda */
        if (*s == '`' && s[1] == '`') {
                printf("<span class='evilcandy-lambda'>``</span>");
                *pps = s + 2;
                return true;
        }


        /* all others, no special syntax */
        while (*s && strchr(DELIMS, *s) != NULL) {
                /* chars that need HTML escape */
                if (*s == '&')
                        printf("&amp;");
                else if (*s == '<')
                        printf("&lt;");
                else if (*s == '>')
                        printf("&gt;");
                else
                        myputchar(*s);
                s++;
        }
        if (s == *pps)
                return false;
        *pps = s;
        return true;
}

static bool
do_number_hex(char **pps)
{
        int c;
        char *endptr;
        char *s = *pps;
        if (s[0] != '0')
                return false;

        c = toupper((int)s[1]);
        if (c != 'X' && c != 'B')
                return false;

        errno = 0;
        {
                long long dummy = strtoull(s, &endptr, 0);
                (void)dummy;
        }
        if (errno || endptr == s) {
                errno = 0;
                return false;
        }
        printf("<span class='evilcandy-number'>");
        while (s < endptr)
                myputchar(*s++);
        printf("</span>");
        *pps = s;
        return true;
}

static bool
do_number(char **pps)
{
        size_t idx;
        char *s = *pps;
        char *endptr;

        if (do_number_hex(pps))
                return true;

        if (s[0] == '.') {
                if (!isdigit((int)s[1]))
                        return false; /* just a dot */
                goto dofloat;
        }

        idx = strspn(s, "0123456789");
        if (idx == 0 && s[0] != '.')
                return false;
        if (s[idx] == 'E' || s[idx] == 'e' || s[idx] == '.')
                goto dofloat;

        /* base 10 integer */
        {
                long long dummy = strtoll(s, &endptr, 10);
                (void)dummy;
        }
        goto print;

dofloat:
        /* floating-point expression */
        {
                double dummy = strtod(s, &endptr);
                (void)dummy;
        }
        /* fall through to print */

print:
        assert(endptr > s);
        printf("<span class='evilcandy-number'>");
        while (s < endptr)
                myputchar(*s++);
        printf("</span>");
        *pps = s;
        return true;
}

static void
highlight_line(char *line)
{
        char *s = line;
        while (*s != '\0') {
                if (isspace(*s)) {
                        myputchar(*s++);
                } else if (getting_comment) {
                        if (s[0] == '*' && s[1] == '/') {
                                myputchar (*s++);
                                myputchar (*s++);
                                printf("</span>");
                                getting_comment = false;
                        } else {
                                myputchar (*s++);
                        }
                } else if (getting_string) {
                        if (*s == '\\' && s[1] == getting_string) {
                                myputchar(*s++);
                                myputchar(*s++);
                        } else if (*s == getting_string) {
                                myputchar(*s++);
                                printf("</span>");
                                getting_string = '\0';
                        } else {
                                myputchar(*s++);
                        }
                } else if (*s == '"' || *s == '\'') {
                        printf("<span class='evilcandy-string'>");
                        getting_string = *s;
                        myputchar(*s++);
                } else if ((s[0] == '/' && s[1] == '/') || s[0] == '#') {
                        printf("<span class='evilcandy-comment'>");
                        while (*s != '\0')
                                myputchar(*s++);
                        printf("</span>");
                        return;
                } else if (s[0] == '/' && s[1] == '*') {
                        printf("<span class='evilcandy-comment'>");
                        getting_comment = true;
                        myputchar(*s++);
                        myputchar(*s++);
                } else {
                        if (do_keyword(&s))
                                continue;
                        if (do_common_identifier(&s))
                                continue;
                        if (do_identifier(&s))
                                continue;
                        if (do_number(&s))
                                continue;
                        if (do_delimiter(&s))
                                continue;

                        /* \_(!)_/ */
                        myputchar(*s++);
                }
        }
}

static void
evilcandy_highlight(void)
{
        char *line;
        while ((line = next_line()) != NULL) {
                highlight_line(line);
        }
}

int
main(int argc, char **argv)
{
        int argi;
        for (argi = 1; argi < argc; argi++) {
                if (!strcmp(argv[argi], "--block-only")) {
                        standalone = false;
                        continue;
                }
                if (!strcmp(argv[argi], "--css-only")) {
                        standalone = true;
                        css_only = true;
                        continue;
                }
                fprintf(stderr, "Unrecognized option '%s'\n",
                        argv[argi]);
                return 1;
        }

        assert(standalone || !css_only);
        if (standalone) {
                if (!css_only)
                        printf("%s", header_start);
                printf("%s", css_str);
                if (!css_only) {
                        printf("%s", header_end);
                        printf("%s", body_start);
                } else {
                        return 0;
                }
        }

        printf("    <pre class=\"evilcandy\">\n");

        evilcandy_highlight();
        /* prolly should throw a warning too */
        if (getting_comment || getting_string)
                printf("</span>");

        printf("    </pre>\n");


        if (standalone && !css_only)
                printf("%s", body_end);
}

