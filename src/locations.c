#include <evilcandy.h>
#include <internal/locations.h>

enum {
        NR_VALUES_PER_LOCATION = 2,
        /* keep aligned with max jump length */
        LOCATION_INSTR_MAX = 32768,
};

static ssize_t
pack_value(unsigned char *u8, size_t size, unsigned long value)
{
        unsigned char *start = u8;

        if (!value || value < 128) {
                if (!size)
                        return -1;
                *u8 = value;
                return 1;
        }

        while (value) {
                unsigned char x = value & 0x7fu;
                if (value >= 128)
                        x |= 128;
                if (!size)
                        return -1;
                *u8++ = x;
                value >>= 7;
                size--;
        }
        return u8 - start;
}

static long
unpack_value(const unsigned char *u8, size_t size, unsigned char **endptr)
{
        unsigned long ret = 0;
        const unsigned char *end = &u8[size];

        while (!!(*u8 & 0x80)) {
                if (u8 >= end)
                        return -1L;
                ret <<= 7;
                ret += (*u8 & 0x7fu);
                u8++;
        }
        if (u8 >= end)
                return -1L;

        ret <<= 7;
        ret += *u8 & 0x7fu;
        u8++;

        *endptr = (unsigned char *)u8;
        return ret;
}

/*
 * location_pack - Pack location information @loc into buffer @buf.
 *
 * Return: Number of bytes stuffed, or -1 if not enough room allocated.
 *
 * This does not raise an exception.
 */
ssize_t
location_pack(void *buf, size_t size, const struct location_t *loc)
{
        unsigned char *u8 = (unsigned char *)buf;
        ssize_t res;

        res = pack_value(u8, size, loc->loc_startline);
        if (res < 0)
                return -1;
        u8 += res;
        size -= res;
        res = pack_value(u8, size, loc->loc_instruction);
        if (res < 0)
                return -1;
        return (size_t)(&u8[res] - (unsigned char *)buf);
}

static enum result_t
location_unpack_one(const unsigned char *buf, size_t size,
                    unsigned char **endptr, struct location_t *loc)
{
        long value;
        unsigned char *ep2;
        const unsigned char *end = &buf[size];

        value = unpack_value(buf, end - buf, &ep2);
        if (value < 0 || value > INT_MAX)
                return RES_ERROR;
        loc->loc_startline = value;
        buf = ep2;

        value = unpack_value(buf, end - buf, &ep2);
        if (value < 0 || value > LOCATION_INSTR_MAX)
                return RES_ERROR;
        loc->loc_instruction = value;
        *endptr = ep2;
        return RES_OK;
}

/**
 * location_unpack - Given @instruction_offset, find the location in @buf
 *                   containing the location info and put it in @loc
 *
 * Return: RES_OK or (likely due to a bug) RES_ERROR.
 *
 * This does not raise an exception.
 */
enum result_t
location_unpack(const void *buf, size_t size, size_t instruction_offset,
                struct location_t *loc)
{
        const unsigned char *u8 = (unsigned char *)buf;
        const unsigned char *end = &u8[size];
        struct location_t backup = { -1, -1 };

        bug_on(instruction_offset > LOCATION_INSTR_MAX);

        for (;;) {
                unsigned char *endptr;
                enum result_t res;
                res = location_unpack_one(u8, end - u8, &endptr, loc);
                if (res == RES_ERROR) {
                        if ((short)backup.loc_instruction < 0)
                                return RES_ERROR;
                        memcpy(loc, &backup, sizeof(*loc));
                        return RES_OK;
                }
                u8 = endptr;

                if (loc->loc_instruction == instruction_offset) {
                        /* best-case scenario */
                        return RES_OK;
                } else if (loc->loc_instruction < instruction_offset) {
                        memcpy(&backup, loc, sizeof(backup));
                } else {
                        /* > instruction_offset */
                        if ((short)backup.loc_instruction >= 0)
                                memcpy(loc, &backup, sizeof(*loc));
                        /*
                         * backup < 0 means this is 1st pass, so 1st
                         * location is before this instruction.  There's
                         * nothing we could do about that.
                         */
                        return RES_OK;
                }
        }
}
