#ifndef EVC_EVILCANDY_LOCATIONS_H
#define EVC_EVILCANDY_LOCATIONS_H

#include <evilcandy.h>

struct location_t {
        unsigned int loc_startline;
        unsigned short loc_instruction;
};

extern enum result_t location_unpack(const void *buf, size_t size,
                                     size_t instruction_offset,
                                     struct location_t *loc);

extern ssize_t location_pack(void *buf, size_t size,
                             const struct location_t *loc);

#endif /* EVC_EVILCANDY_LOCATIONS_H */
