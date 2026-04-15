#ifndef EVILCANDY_TYPES_BYTES_H
#define EVILCANDY_TYPES_BYTES_H

#include <evilcandy/typedefs.h>
#include <stddef.h>
#include <sys/types.h>

/* types/bytes.c */
extern Object *bytesvar_new(const unsigned char *buf, size_t len);
extern Object *bytesvar_from_source(char *src);
extern const unsigned char *bytes_getbuf(Object *v);
extern Object *bytesvar_nocopy(const unsigned char *buf, size_t len);
extern Object *bytes_getslice(Object *bytes, ssize_t start,
                              ssize_t stop, ssize_t step);
extern Object *bytesvar_new_sg(size_t size, ...);


#endif /* EVILCANDY_TYPES_BYTES_H */
