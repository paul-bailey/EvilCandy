/* Included by evilcandy.h.  Do not include this file directly. */
#ifndef EVC_STRING_READER_H
#define EVC_STRING_READER_H

struct string_reader_t {
        const void *dat;
        size_t wid;
        size_t len;
        size_t pos;
};

static inline long
string_reader_getc__(size_t wid, const void *dat, size_t pos)
{
        if (wid == 1)
                return (long)((uint8_t *)dat)[pos];
        if (wid == 2)
                return (long)((uint16_t *)dat)[pos];
        bug_on(wid != 4);
        return (long)((uint32_t *)dat)[pos];
}

/* types/string.c */
extern void string_reader_init(struct string_reader_t *rd,
                               Object *str, size_t startpos);

static inline void
string_reader_init_cstring(struct string_reader_t *rd, const char *s)
{
        rd->dat = s;
        rd->wid = 1;
        rd->len = strlen(s);
        rd->pos = 0;
}

static inline long
string_reader_getc(struct string_reader_t *rd)
{
        if (rd->pos >= rd->len)
                return -1L;
        return string_reader_getc__(rd->wid, rd->dat, rd->pos++);
}

static inline void
string_reader_backup(struct string_reader_t *rd, size_t amt)
{
        bug_on(amt > rd->pos);
        rd->pos -= amt;
}

static inline void
string_reader_ungetc(struct string_reader_t *rd, long c)
{
        if (c >= 0)
                rd->pos--;
}

static inline void
string_reader_setpos(struct string_reader_t *rd, size_t pos)
{
        if (pos >= rd->len)
                pos = rd->len;
        rd->pos = pos;
}

static inline size_t
string_reader_getpos(struct string_reader_t *rd)
{
        return rd->pos;
}

/* c is the last-read character */
static inline size_t
string_reader_getpos_lastread(struct string_reader_t *rd, long c)
{
        return c >= 0 ? rd->pos - 1 : rd->pos;
}



#endif /* EVC_STRING_READER_H */
