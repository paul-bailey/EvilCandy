/* bytes.c - Built-in methods for bytes data types */

#include "types_priv.h"
#include <string.h>
#include <stdlib.h>

struct bytesvar_t {
        struct seqvar_t base;
        unsigned char *b_buf; /* the actual byte array */
};

#define V2B(v_) ((struct bytesvar_t *)(v_))

static struct var_t *
bytes_len(struct vmframe_t *fr)
{
        struct var_t *self = vm_get_this(fr);
        bug_on(!isvar_bytes(self));
        return intvar_new(seqvar_size(self));
}

/*
 * TODO REVISIT: Policy decision.  Return datum as an integer,
 * or as another bytes array of size 1?
 */
static struct var_t *
bytes_getitem(struct var_t *a, int idx)
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

static struct var_t *
bytes_cat(struct var_t *a, struct var_t *b)
{
        unsigned char *ba, *bb, *bc;
        size_t a_len, b_len, c_len;
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

static struct var_t *
bytes_str(struct var_t *v)
{
        /*
         * XXX REVISIT: temporary, until we decide on a syntax for
         * expressing this as a literal.
         */
        char buf[32];
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "<bytes at %p>", V2B(v)->b_buf);
        return stringvar_new(buf);
}

static int
bytes_cmp(struct var_t *a, struct var_t *b)
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
bytes_cmpz(struct var_t *v)
{
        return seqvar_size(v) == 0;
}

static void
bytes_reset(struct var_t *v)
{
        unsigned char *b = V2B(v)->b_buf;
        if (b)
                free(b);
}

static struct var_t *
bytes_cp(struct var_t *v)
{
        /* immutable, so merely produce a reference */
        VAR_INCR_REF(v);
        return v;
}

/**
 * bytesvar_new - Get a new bytes var
 * @buf: Array of bytes to copy into new var
 * @len: Length of @buf
 *
 * Return: New immutable bytes var
 */
struct var_t *
bytesvar_new(unsigned char *buf, size_t len)
{
        struct var_t *v = var_new(&BytesType);
        /* REVISIT: allow immortal bytes vars? */
        V2B(v)->b_buf = ememdup(buf, len);
        seqvar_set_size(v, len);
        return v;
}

static const struct type_inittbl_t bytes_cb_methods[] = {
        V_INITTBL("len",        bytes_len, 0, 0),
        /* TODO: lstrip, just, etc. */
        TBLEND,
};

static const struct seq_methods_t bytes_seq_methods = {
        .getitem        = bytes_getitem,
        .setitem        = NULL, /* like string, immutable */
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
        .cp     = bytes_cp,
};

