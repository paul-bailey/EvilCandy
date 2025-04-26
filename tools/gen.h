#ifndef TOOLS_GEN_H
#define TOOLS_GEN_H

/* all the standard headers here, why not */
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int
iseol(int c)
{
        return c == '#' || c == '\n' || c == '\0';
}

extern void oom(void);
extern char **tokenize_next_line(FILE *fp, int *ntok);

#endif /* TOOLS_GEN_H */
