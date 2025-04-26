#include "gen.h"
#include <stdio.h>
#include <stdlib.h>

#define MAX_TOK_PER_LINE 20

void
oom(void)
{
        perror("out of memory!");
        abort();
}

char **
tokenize_next_line(FILE *fp, int *ntok)
{
        char *tokarray[MAX_TOK_PER_LINE];
        char **ret;
        char *line = NULL;
        char *s;
        size_t len = 0;
        ssize_t count;
        int n;

        do {
                count = getline(&line, &len, fp);
                if (count < 0) {
                        if (errno == ENOMEM)
                                oom();
                        if (!feof(fp)) {
                                perror("getline failed");
                                abort();
                        }
                        /* else, normal EOF */
                        return NULL;
                }

                for (s = line; *s != '\0'; s++) {
                        if (!isspace((int)*s))
                                break;
                }
        } while (iseol(*s));

        n = 0;
        while (true) {
                char *start, *stop;
                size_t tlen;
                start = s;
                while (isgraph((int)*s))
                        s++;

                stop = s;
                tlen = stop - start;
                tokarray[n] = malloc(tlen + 1);
                if (!tokarray[n])
                        oom();
                memcpy(tokarray[n], start, tlen);
                tokarray[n][tlen] = '\0';

                n++;

                while (isspace((int)*s) && !iseol(*s))
                        s++;

                if (iseol(*s))
                        break;
        }

        ret = malloc(n * sizeof(char *));
        if (!ret)
                oom();

        memcpy(ret, tokarray, n * sizeof(char *));
        if (ntok)
                *ntok = n;

        free(line);
        return ret;
}

