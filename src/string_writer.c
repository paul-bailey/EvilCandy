/*
 * string_writer.c - Similar to buffer.c, but specifically for Unicode.
 *
 * Using buffer.c's binary API would be too verbose for writing strings,
 * so this exists independently.
 */
#include <evilcandy.h>

static long
width2maxchr(size_t width)
{
        if (width == 1)
                return 0xffu;
        if (width == 2)
                return 0xffffu;
        bug_on(width != 4);
        return 0x10ffffu;
}

/*
 * string_writer_init - Initialize a string writer.
 * @wr: Writer to initialize
 * @width: Expected width of the Unicode array.  If you don't know
 *         correct width, pass 1 for @width.  Absolutely do NOT pass a
 *         larger value than you will need, or you could cause
 *         unpredictable behavior in things like search algorithms.
 */
void
string_writer_init(struct string_writer_t *wr, size_t width)
{
        wr->width = width;
        wr->maxchr = width2maxchr(width);
        wr->p.p = NULL;
        wr->pos = wr->pos_i = wr->n_alloc = 0;
}

/**
 * string_writer_append - Append a Unicode point
 * @wr: Writer to append to
 * @c:  Unicode point, from 0 <= c <= 0x10ffff
 */
void
string_writer_append(struct string_writer_t *wr, unsigned long c)
{
        enum { STR_REALLOC_SIZE = 64 };

        if (c > wr->maxchr) {
                /*
                 * Ugh, need to resize.  This should only occur from
                 * string_parse, when we're loading a source file.
                 */
                struct string_writer_t wr2;
                size_t width;
                size_t i;

                bug_on(c > 0x10fffful);

                if (c > 0xfffful) {
                        width = 4;
                } else if (c > 0xfful) {
                        width = 2;
                } else{
                        bug();
                        return;
                }

                string_writer_init(&wr2, width);
                for (i = 0; i < wr->pos_i; i++) {
                        unsigned long oldchar;
                        switch (wr->width) {
                        case 1:
                                oldchar = wr->p.u8[i];
                                break;
                        case 2:
                                oldchar = wr->p.u16[i];
                                break;
                        default:
                                bug();
                                return;
                        }
                        string_writer_append(&wr2, oldchar);
                }
                if (wr->p.p)
                        efree(wr->p.p);
                bug_on(wr->pos_i != wr2.pos_i);
                memcpy(wr, &wr2, sizeof(wr2));
                /* fall through, we still have to write c */
        }

        bug_on(c > wr->maxchr);

        if (wr->pos + wr->width > wr->n_alloc) {
                wr->n_alloc += STR_REALLOC_SIZE * wr->width;
                wr->p.p = erealloc(wr->p.p, wr->n_alloc);
        }

        switch (wr->width) {
        case 1:
                wr->p.u8[wr->pos_i] = c;
                break;
        case 2:
                wr->p.u16[wr->pos_i] = c;
                break;
        case 4:
                wr->p.u32[wr->pos_i] = c;
                break;
        default:
                bug();
        }
        wr->pos_i++;
        wr->pos += wr->width;
}

/**
 * string_writer_appends - Append an ASCII C string.
 * @wr:   Writer to append to
 * @cstr: An ASCII C string.  DO NOT use UTF-8 or Latin1 strings.
 */
void
string_writer_appends(struct string_writer_t *wr, const char *cstr)
{
        while (*cstr != '\0') {
                string_writer_append(wr, (unsigned char)*cstr);
                cstr++;
        }
}

static long
string_writer_getidx(struct string_writer_t *wr, size_t idx)
{
        bug_on(idx >= string_writer_size(wr));
        switch (wr->width) {
        case 1:
                return wr->p.u8[idx];
        case 2:
                return wr->p.u16[idx];
        case 4:
                return wr->p.u32[idx];
        default:
                bug();
                return 0;
        }
}

/*
 * helper to string_writer_swap, only called if @idx is known
 * to be in range
 */
static void
string_writer_setidx(struct string_writer_t *wr,
                     size_t idx, unsigned long point)
{
        bug_on(idx >= string_writer_size(wr));
        switch (wr->width) {
        case 1:
                wr->p.u8[idx] = point;
                break;
        case 2:
                wr->p.u16[idx] = point;
                break;
        case 4:
                wr->p.u32[idx] = point;
                break;
        default:
                bug();
        }
}

/**
 * string_writer_appendb - Append a Unicode array
 * @wr: Writer to append to
 * @buf: Array of Unicode points
 * @width: Width of @buf
 * @len: Array size of @buf
 */
void
string_writer_appendb(struct string_writer_t *wr,
                      const void *buf, size_t width, size_t len)
{
        struct string_writer_t wr2;
        size_t i;

        /* Fill in just what we need for string_writer_getidx */
        wr2.width  = width;
        wr2.p.p    = (void *)buf;
#ifndef NDEBUG
        wr2.pos_i  = len; /* Avoid unnecessary bug trap */
#endif

        for (i = 0; i < len; i++) {
                long point = string_writer_getidx(&wr2, i);
                string_writer_append(wr, point);
        }
}

/**
 * string_writer_swapchars - Swap two characters in string writer.
 * @wr: Writer to swap character in.
 * @apos: position of first character
 * @bpos: position of second character
 *
 * Return: RES_OK if swap occurred, RES_ERROR if apos or bpos are
 *         out of @wr's range.  Use string_writer_size() to check.
 */
enum result_t
string_writer_swapchars(struct string_writer_t *wr, size_t apos, size_t bpos)
{
        long apoint, bpoint;
        size_t size = string_writer_size(wr);
        if (apos >= size || bpos >= size)
                return RES_ERROR;

        bug_on(apos >= string_writer_size(wr) ||
               bpos >= string_writer_size(wr));

        apoint = string_writer_getidx(wr, apos);
        bpoint = string_writer_getidx(wr, bpos);

        string_writer_setidx(wr, apos, bpoint);
        string_writer_setidx(wr, bpos, apoint);
        return RES_OK;
}

/**
 * string_writer_finish - Get the string writer's Unicode array, its width,
 *                        and length.
 * @wr:    String writer to get data from
 * @width: Variable to store character width.
 * @len:   Variable to store array length.
 *
 * Return: Pointer to a buffer whose width is @width and array length is
 *         @len.  This is not zero-terminated.  Do not use @wr again unless
 *         you next call string_writer_init().
 */
void *
string_writer_finish(struct string_writer_t *wr, size_t *width, size_t *len)
{
        *len = wr->pos_i;
        *width = wr->width;
        if (wr->p.p == NULL) {
                bug_on(*len != 0);
                return NULL;
        }

        /*
         * XXX: Maybe avoid realloc
         * if wr->n_alloc - wr->pos < some_threshold
         */
        return erealloc(wr->p.p, wr->pos);
}

/**
 * string_writer_destroy - Alternative to string_writer_finish.
 *
 * This frees the Unicode array if anything had been added to it.  It's for
 * use by error handlers when the Unicode array is not to be used after all.
 */
void
string_writer_destroy(struct string_writer_t *wr)
{
        if (wr->p.p != NULL)
                efree(wr->p.p);
        wr->pos = wr->pos_i = wr->n_alloc = 0;
}


