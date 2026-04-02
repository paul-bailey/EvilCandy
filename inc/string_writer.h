#ifndef EVC_STRING_WRITER_H
#define EVC_STRING_WRITER_H

#include <stdint.h>
#include <evcenums.h>

struct utf8_state_t {
        enum {
                /* Keep this order. Implementation depends on it */
                UTF8_STATE_ASCII = 0,
                UTF8_STATE_GET1,
                UTF8_STATE_GET2,
                UTF8_STATE_GET3,
                UTF8_STATE_ERR,
                UTF8_STATE_ERR_ORD,
        } state;
        char buf[5];
        unsigned long point;
        size_t idx;
};

/*
 * struct string_writer_t - Because wrappers for struct buffer_t would be
 *                          too cumbersome, we'll just do it manually.
 */
struct string_writer_t {
        size_t width;
        union {
                uint8_t *u8;
                uint16_t *u16;
                uint32_t *u32;
                void *p;
        } p;
        unsigned long maxchr;
        size_t pos;
        size_t pos_i;
        size_t n_alloc;
};

/* string_writer.c */
extern void string_writer_init(struct string_writer_t *wr, size_t width);
extern void string_writer_append(struct string_writer_t *wr,
                                 unsigned long c);
extern void string_writer_appends(struct string_writer_t *wr,
                                  const char *cstr);
extern void string_writer_appendb(struct string_writer_t *wr,
                              const void *buf, size_t width, size_t len);
extern size_t string_writer_get_ascii(struct string_writer_t *wr,
                                      const void *buf, size_t max);
extern enum result_t string_writer_swapchars(struct string_writer_t *wr,
                                             size_t apos, size_t bpos);
extern void *string_writer_finish(struct string_writer_t *wr,
                                  size_t *width, size_t *len);
extern void string_writer_destroy(struct string_writer_t *wr);

static inline size_t
string_writer_size(struct string_writer_t *wr)
{
        return wr->pos_i;
}

/* types/string.c */
extern void string_writer_append_strobj(struct string_writer_t *wr,
                                        Object *str);
extern ssize_t string_writer_decode(struct string_writer_t *wr,
                                const void *data, size_t n,
                                int codec, struct utf8_state_t *state);
extern Object *stringvar_from_writer(struct string_writer_t *wr);


#endif /* EVC_STRING_WRITER_H */
