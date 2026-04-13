#ifndef EVC_INC_INTERNAL_BUILTIN_IO_H
#define EVC_INC_INTERNAL_BUILTIN_IO_H

/* builtin/builtin.c */
extern ssize_t evc_file_write(Object *fo, Object *data);
extern Object *evc_file_open(int fd, const char *name, bool binary,
                             bool closefd, int codec, size_t buffering);
extern Object *evc_file_read(Object *fo, ssize_t size);


#endif /* EVC_INC_INTERNAL_BUILTIN_IO_H */

