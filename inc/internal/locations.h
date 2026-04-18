#ifndef EVC_EVILCANDY_LOCATIONS_H
#define EVC_EVILCANDY_LOCATIONS_H

#include <stddef.h>
#include <evilcandy/enums.h>
#include <sys/types.h>

struct location_t {
        unsigned int loc_startline;
        unsigned short loc_instruction;
};

extern enum result_t location_unpack(const void *buf, size_t size,
                                     size_t instruction_offset,
                                     struct location_t *loc);

extern ssize_t location_pack(void *buf, size_t size,
                             const struct location_t *loc);

/* these made public so I can unit test them */
extern long unpack_value(const unsigned char *u8,
                         size_t size, unsigned char **endptr);

extern ssize_t pack_value(unsigned char *u8, size_t size,
                          unsigned long value);

#endif /* EVC_EVILCANDY_LOCATIONS_H */
