/*
 * XXX "ewrappers" is no longer a clear description of what this code
 * does.  "memwrappers" or "mallocwrappers" is a better choice.
 */
#ifndef EVC_INC_EVILCANDY_EWRAPPERS_H
#define EVC_INC_EVILCANDY_EWRAPPERS_H

/* ewrappers.c */
extern char *estrdup(const char *s);
extern void *emalloc(size_t size);
extern void *ecalloc(size_t size);
extern void *erealloc(void *buf, size_t size);
extern void *ememdup(const void *buf, size_t size);
extern ssize_t egetline(char **line, size_t *linecap, FILE *fp);
extern void efree(void *ptr);


#endif /* EVC_INC_EVILCANDY_EWRAPPERS_H */
