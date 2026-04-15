#ifndef EVILCANDY_MYREADLINE_H
#define EVILCANDY_MYREADLINE_H

#include <stdio.h>
#include <sys/types.h>

/* readline.c */
extern ssize_t myreadline(char **linep, size_t *size,
                          FILE *fp, const char *prompt);

#endif /* EVILCANDY_MYREADLINE_H */

