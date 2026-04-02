#ifndef EVILCANDY_LIB_UTF8_H
#define EVILCANDY_LIB_UTF8_H

#include <stdbool.h>

extern long utf8_decode_one(const unsigned char *src,
                            unsigned char **endptr);
static inline bool
utf8_valid_unicode(unsigned long point)
{
        /* Check out of range or invalid surrogate pairs */
        return point < 0x10fffful &&
               !(point >= 0xd800ul && point <= 0xdffful);
}

#endif /* EVILCANDY_LIB_UTF8_H */
