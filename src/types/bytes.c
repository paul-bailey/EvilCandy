/* bytes.c - Built-in methods for bytes data types */

#include <evilcandy.h>

#define V2B(v_) ((struct bytesvar_t *)(v_))

static Object *
bytes_len(Frame *fr)
{
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &BytesType) == RES_ERROR)
                return ErrorVar;

        return intvar_new(seqvar_size(self));
}

/*
 * TODO REVISIT: Policy decision.  Return datum as an integer,
 * or as another bytes array of size 1?
 */
static Object *
bytes_getitem(Object *a, int idx)
{
        unsigned char *ba = V2B(a)->b_buf;
        unsigned int u;

        /* var.c should have trapped this */
        bug_on(idx >= seqvar_size(a));

        /*
         * Shouldn't need this, but who knows what compilers
         * still wander the wastes...
         */
        u = ((unsigned int)ba[idx]) & 0xffu;
        return intvar_new(u);
}

static bool
bytes_hasitem(Object *bytes, Object *ival)
{
        long long x;
        size_t i, n;
        unsigned char *data;
        bug_on(!isvar_bytes(bytes));
        if (!isvar_int(ival)) {
                /* TODO: Should this be an error? */
                return false;
        }
        x = intvar_toll(ival);
        if (x < 0 || x > 255)
                return false;

        n = seqvar_size(bytes);
        data = V2B(bytes)->b_buf;
        for (i = 0; i < n; i++) {
                if (data[i] == (unsigned char)x)
                        return true;
        }
        return false;
}

static Object *
bytes_cat(Object *a, Object *b)
{
        unsigned char *ba, *bb, *bc;
        size_t a_len, b_len, c_len;

        if (!b)
                return bytesvar_new((unsigned char *)"", 0);

        ba = V2B(a)->b_buf;
        bb = V2B(b)->b_buf;
        a_len = seqvar_size(a);
        b_len = seqvar_size(b);
        c_len = a_len + b_len;

        bc = emalloc(c_len);
        memcpy(bc, ba, a_len);
        memcpy(bc + a_len, bb, b_len);
        return bytesvar_new(bc, c_len);
}

static Object *
bytes_str(Object *v)
{
        Object *ret;
        struct buffer_t b;
        unsigned char *s = V2B(v)->b_buf;
        size_t i, n = seqvar_size(v);
        enum { SQ = '\'', BKSL = '\\'};

        buffer_init(&b);
        buffer_putc(&b, 'b');
        buffer_putc(&b, SQ);

        for (i = 0; i < n; i++) {
                unsigned int c = s[i] & 0xffu;
                if (c == SQ) {
                        buffer_putc(&b, BKSL);
                        buffer_putc(&b, SQ);
                } else if (c == BKSL) {
                        buffer_putc(&b, BKSL);
                        buffer_putc(&b, BKSL);
                } else if (isspace(c)) {
                        switch (c) {
                        case ' ':
                                buffer_putc(&b, c);
                                continue;
                        case '\n':
                                c = 'n';
                                break;
                        case '\t':
                                c = 't';
                                break;
                        case '\v':
                                c = 'v';
                                break;
                        case '\f':
                                c = 'f';
                                break;
                        case '\r':
                                c = 'r';
                                break;
                        }
                        buffer_putc(&b, BKSL);
                        buffer_putc(&b, c);
                } else if (c > 127 || !isgraph(c)) {
                        buffer_putc(&b, BKSL);
                        buffer_putc(&b, ((c >> 6) & 0x07) + '0');
                        buffer_putc(&b, ((c >> 3) & 0x07) + '0');
                        buffer_putc(&b, (c & 0x07) + '0');
                } else {
                        buffer_putc(&b, c);
                }
        }
        buffer_putc(&b, SQ);
        ret = stringvar_new(b.s);
        buffer_free(&b);
        return ret;
}

static int
bytes_cmp(Object *a, Object *b)
{
        bug_on(a->v_type != b->v_type);
        int ret;
        size_t len_a, len_b, minlen;
        unsigned char *ba = V2B(a)->b_buf;
        unsigned char *bb = V2B(b)->b_buf;

        if (ba == bb)
                return 0;
        len_a = seqvar_size(a);
        len_b = seqvar_size(b);
        minlen = len_a > len_b ? len_b : len_a;
        ret = memcmp(ba, bb, minlen);
        if (!ret && len_a != len_b)
                return len_a > len_b ? 1 : -1;
        return ret > 0 ? 1 : (ret < 0 ? -1 : 0);
}

static bool
bytes_cmpz(Object *v)
{
        return seqvar_size(v) == 0;
}

static void
bytes_reset(Object *v)
{
        unsigned char *b = V2B(v)->b_buf;
        if (b)
                efree(b);
}

enum {
        BF_COPY = 1,
};

static Object *
bytesvar_newf(unsigned char *buf, size_t len, unsigned int flags)
{
        Object *v = var_new(&BytesType);
        /* REVISIT: allow immortal bytes vars? */
        if (!!(flags & BF_COPY))
                V2B(v)->b_buf = ememdup(buf, len);
        else
                V2B(v)->b_buf = buf;
        seqvar_set_size(v, len);
        return v;
}

/**
 * bytes_getbuf - Get raw data array from bytes object.
 * seqvar_size(v) will return its length in bytes
 */
const unsigned char *
bytes_getbuf(Object *v)
{
        bug_on(!isvar_bytes(v));
        return V2B(v)->b_buf;
}

/**
 * bytesvar_new - Get a new bytes var
 * @buf: Array of bytes to copy into new var
 * @len: Length of @buf
 *
 * Return: New immutable bytes var
 */
Object *
bytesvar_new(unsigned char *buf, size_t len)
{
        return bytesvar_newf(buf, len, BF_COPY);
}

/**
 * bytesvar_nocopy - Same relation to bytesvar_new that stringvar_nocopy
 *                   has to stringvar_new
 */
Object *
bytesvar_nocopy(unsigned char *buf, size_t len)
{
        return bytesvar_newf(buf, len, 0);
}

/**
 * bytesvar_from_source - Like bytesvar_new, except input has not been
 *                        parsed yet
 * @src: C string as written from source.  Contains leading 'b' or 'B'
 *       as well as the quote character.  Concatentated tokens may
 *       exist, eg. "b'\x12x34'b'\x56\x78'".  No characters may exist
 *       between these concatenated tokens.
 */
Object *
bytesvar_from_source(char *src)
{
        unsigned char c;
        size_t size;
        int q;
        struct buffer_t b;
        enum { BKSL = '\\' };

        buffer_init(&b);
        /* calling code should have trapped this */
        bug_on(src[0] != 'b' && src[0] != 'B');
        q = src[1];
        bug_on(!isquote(q));

        src += 2;

again:
        while ((c = *src++) != q && c != '\0') {
                if (c == BKSL) {
                        c = *src++;
                        if (c == q) {
                                buffer_putd(&b, &c, 1);
                                continue;
                        }
                        if (c == 'x' || c == 'X') {
                                if (!isxdigit(src[0]) || !isxdigit(src[1]))
                                        goto err;
                                c = x2bin(src[0]) * 16 + x2bin(src[1]);

                                src += 2;
                                buffer_putd(&b, &c, 1);
                                continue;
                        }
                        if (isodigit(c)) {
                                int i, v;
                                --src;
                                for (i = 0, v = 0; i < 3; i++, src++) {
                                        if (!isodigit(*src))
                                                break;
                                        /* '0' & 7 happens to be 0 */
                                        v = (v << 3) + (*src & 7);
                                }
                                if (v >= 256)
                                        goto err;
                                c = v;
                                buffer_putd(&b, &c, 1);
                                continue;
                        }
                        switch (c) {
                        case '0':
                                c = 0;
                                break;
                        case 'a':
                                c = '\a';
                                break;
                        case 'b':
                                c = '\b';
                                break;
                        case 'e':
                                c = '\033';
                                break;
                        case 'f':
                                c = '\f';
                                break;
                        case 'n':
                                c = '\n';
                                break;
                        case 'r':
                                c = '\r';
                                break;
                        case 't':
                                c = '\t';
                                break;
                        case 'v':
                                c = '\v';
                                break;
                        case BKSL:
                                break;
                        default:
                                goto err;
                        }
                        buffer_putd(&b, &c, 1);
                } else {
                        buffer_putd(&b, &c, 1);
                }
        }

        bug_on(c != q);
        c = *src++;

        if (c != '\0') {
                /* wrapping code should have caught this earlier */
                bug_on(c != 'b' && c != 'B');
                q = *src++;
                bug_on(!isquote(q));
                goto again;
        }

        size = buffer_size(&b);
        if (size == 0) {
                /* empty buffer, best not to accidentally de-reference NULL */
                unsigned char dummy = 0;
                buffer_putd(&b, &dummy, 1);
        }

        /* TODO: Any need to make immortal? */
        return bytesvar_newf((unsigned char *)b.s, size, 0);

err:
        buffer_free(&b);
        return ErrorVar;
}

static const struct type_inittbl_t bytes_cb_methods[] = {
        V_INITTBL("len",        bytes_len, 0, 0, -1, -1),
        /* TODO: lstrip, just, etc. */
        TBLEND,
};

static const struct seq_methods_t bytes_seq_methods = {
        .getitem        = bytes_getitem,
        .setitem        = NULL, /* like string, immutable */
        .hasitem        = bytes_hasitem,
        .cat            = bytes_cat,
        .sort           = NULL,
};

struct type_t BytesType = {
        .name   = "bytes",
        .opm    = NULL,
        .cbm    = bytes_cb_methods,
        .mpm    = NULL,
        .sqm    = &bytes_seq_methods,
        .size   = sizeof(struct bytesvar_t),
        .str    = bytes_str,
        .cmp    = bytes_cmp,
        .cmpz   = bytes_cmpz,
        .reset  = bytes_reset,
};

