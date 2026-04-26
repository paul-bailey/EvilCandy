#ifndef EVC_INC_INTERNAL_PATH_H
#define EVC_INC_INTERNAL_PATH_H

#include <stdio.h>

/* path.c */
extern void pop_path(FILE *fp);
extern FILE *push_path(const char *filename);
extern void reduce_pathname_in_place(char *path);
extern enum result_t path_insert(const char *path);

#endif /* EVC_INC_INTERNAL_PATH_H */
