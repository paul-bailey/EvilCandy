/* Included by evilcandy.h.  Do not include this file directly. */
#ifndef EVC_STRING_READER_H
#define EVC_STRING_READER_H

struct string_reader_t {
        Object *str;
        void *dat;
        size_t wid;
        size_t len;
        size_t pos;
};

/* types/string.c */
extern void string_reader_init(struct string_reader_t *rd,
                               Object *str, size_t startpos);
extern long string_reader_getc__(size_t wid, void *dat, size_t pos);

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


#endif /* EVC_STRING_READER_H */
