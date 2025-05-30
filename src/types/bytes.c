/* bytes.c - Built-in methods for bytes data types */

#include <evilcandy.h>

#define V2B(v_) ((struct bytesvar_t *)(v_))

enum {
        /* common 'flags' bit-field args */
        BF_COPY     = 0x01,
        BF_RIGHT    = 0x02,
        BF_CENTER   = 0x04,
        BF_SUPPRESS = 0x08,
};

static Object *
bytesvar_newf(const unsigned char *buf, size_t len, unsigned int flags)
{
        Object *v = var_new(&BytesType);
        /* REVISIT: allow immortal bytes vars? */
        if (!!(flags & BF_COPY))
                V2B(v)->b_buf = ememdup(buf, len);
        else
                V2B(v)->b_buf = (unsigned char *)buf;
        seqvar_set_size(v, len);
        return v;
}

/* XXX: DRY violation with string.c */
static unsigned long
to_swapcase(unsigned long c)
{
        return evc_islower(c)
                ? toupper(c)
                : (evc_isupper(c) ? tolower(c) : c);
}

/*
 * iobj already known to be int
 * ret<0 means ValueError set because out of range
 */
static int
intvar_to_byte(Object *iobj, bool suppress)
{
        long long ival = intvar_toll(iobj);
        if (ival < 0LL || ival > 255LL) {
                if (!suppress) {
                        err_setstr(ValueError,
                                   "Expected: value between 0 and 255");
                }
                return -1;
        }
        return (int)ival;
}

static enum result_t
bytes_unpack_self(Frame *fr, const unsigned char **self, size_t *selflen)
{
        Object *selfobj = vm_get_this(fr);
        if (arg_type_check(selfobj, &BytesType) == RES_ERROR)
                return RES_ERROR;
        *self = bytes_get_data(selfobj);
        *selflen = seqvar_size(selfobj);
        return RES_OK;
}

/* Used when arg[argno] is expected to be bytes */
static enum result_t
bytes_unpack_argno(Frame *fr, int argno,
                   const unsigned char **arg, size_t *arglen)
{
        Object *argobj = vm_get_arg(fr, argno);
        if (arg_type_check(argobj, &BytesType) == RES_ERROR)
                return RES_ERROR;
        *arg = bytes_get_data(argobj);
        *arglen = seqvar_size(argobj);
        return RES_OK;
}

/*
 * Used when arg is bytes and self, arg data and length are needed.
 */
static enum result_t
bytes_unpack_1arg(Frame *fr, const unsigned char **self, size_t *selflen,
                  const unsigned char **arg, size_t *arglen)
{
        if (bytes_unpack_self(fr, self, selflen) == RES_ERROR)
                return RES_ERROR;
        return bytes_unpack_argno(fr, 0, arg, arglen);
}

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

/* comparisons, helpers to array_getslice */
static bool slice_cmp_lt(int a, int b) { return a < b; }
static bool slice_cmp_gt(int a, int b) { return a > b; }

static Object *
bytes_getslice(Object *bytes, int start, int stop, int step)
{
        unsigned char *src, *dst;
        bool (*cmp)(int, int);
        struct buffer_t b;
        size_t count;

        bug_on(!isvar_bytes(bytes));
        if (start == stop)
                return VAR_NEW_REF(gbl.empty_bytes);

        cmp = start < stop ? slice_cmp_lt : slice_cmp_gt;
        src = bytes_get_data(bytes);
        buffer_init(&b);

        count = 0;
        while (cmp(start, stop)) {
                buffer_putd(&b, &src[start], 1);
                start += step;
                count++;
        }
        bug_on(buffer_size(&b) != count);
        dst = (unsigned char *)buffer_trim(&b);
        return bytesvar_nocopy(dst, count);
}

static bool
bytes_hasitem(Object *bytes, Object *ival)
{
        bug_on(!isvar_bytes(bytes));
        if (isvar_int(ival)) {
                size_t n;
                unsigned char *data;
                int x = intvar_to_byte(ival, true);
                if (x < 0)
                        return false;
                n = seqvar_size(bytes);
                data = bytes_get_data(bytes);
                return memchr(data, x, n) != NULL;
        } else if (isvar_bytes(ival)) {
                const unsigned char *haystack, *needle;
                size_t hlen, nlen;

                haystack = bytes_get_data(bytes);
                hlen = seqvar_size(bytes);
                needle = bytes_get_data(ival);
                nlen = seqvar_size(ival);

                return memmem(haystack, hlen, needle, nlen) != NULL;
        } else {
                /* XXX: error instead? */
                return false;
        }
}

static Object *
bytes_cat(Object *a, Object *b)
{
        unsigned char *ba, *bb, *bc;
        size_t a_len, b_len, c_len;

        if (!b)
                return VAR_NEW_REF(gbl.empty_bytes);

        ba = V2B(a)->b_buf;
        bb = V2B(b)->b_buf;
        a_len = seqvar_size(a);
        b_len = seqvar_size(b);
        c_len = a_len + b_len;

        if (!c_len)
                return VAR_NEW_REF(gbl.empty_bytes);
        bc = emalloc(c_len);
        memcpy(bc, ba, a_len);
        memcpy(bc + a_len, bb, b_len);
        return bytesvar_newf(bc, c_len, 0);
}

static Object *
bytes_str(Object *v)
{
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
        return stringvar_nocopy(buffer_trim(&b));
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
bytesvar_new(const unsigned char *buf, size_t len)
{
        return bytesvar_newf(buf, len, BF_COPY);
}

/**
 * bytesvar_nocopy - Same relation to bytesvar_new that stringvar_nocopy
 *                   has to stringvar_new
 */
Object *
bytesvar_nocopy(const unsigned char *buf, size_t len)
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

static Object *
do_bytes_decode(Frame *fr)
{
        err_setstr(NotImplementedError,
                   ".decode() method not yet implemented");
        return ErrorVar;
}

static Object *
do_bytes_count(Frame *fr)
{
        Object *self = vm_get_this(fr);
        Object *arg = vm_get_arg(fr, 0);
        const unsigned char *haystack;
        size_t hlen;
        int count = 0;

        if (arg_type_check(self, &BytesType) == RES_ERROR)
                return ErrorVar;

        haystack = bytes_get_data(self);
        hlen = seqvar_size(self);

        bug_on(!arg);
        if (isvar_bytes(arg)) {
                /* sub-bytes */
                const unsigned char *needle = bytes_get_data(arg);
                size_t nlen = seqvar_size(arg);
                count = memcount(haystack, hlen, needle, nlen);
        } else if (isvar_int(arg)) {
                /* single-char */
                unsigned char v;
                int ival = intvar_to_byte(arg, false);
                if (ival < 0)
                        return ErrorVar;
                v = (unsigned char)ival;
                count = memcount(haystack, hlen, &v, 1);
        } else {
                err_argtype(typestr(arg));
                return ErrorVar;
        }

        return count ? intvar_new(count) : VAR_NEW_REF(gbl.zero);
}

static Object *
bytes_index_or_find(Frame *fr, unsigned int flags)
{
        Object *self, *arg;
        const unsigned char *found, *haystack;
        size_t hlen;
        void *(*locfn)(const void *, size_t, const void *, size_t);
        int res;

        self = vm_get_this(fr);
        arg = vm_get_arg(fr, 0);
        if (arg_type_check(self, &BytesType) == RES_ERROR)
                return ErrorVar;
        bug_on(!arg);

        locfn = !!(flags & BF_RIGHT) ? memrmem : memmem;

        haystack = bytes_get_data(self);
        hlen = seqvar_size(self);
        if (isvar_bytes(arg)) {
                found = locfn(haystack, hlen,
                              bytes_get_data(arg), seqvar_size(arg));
        } else if (isvar_int(arg)) {
                unsigned char v8;
                int ival = intvar_to_byte(arg, false);
                if (ival < 0)
                        return ErrorVar;
                v8 = ival;
                found = locfn(haystack, hlen, &v8, 1);
        } else {
                err_argtype(typestr(arg));
                return ErrorVar;
        }
        if (!found && !(flags & BF_SUPPRESS)) {
                err_setstr(ValueError, "subbytes not found");
                return ErrorVar;
        }
        if (!found)
                return VAR_NEW_REF(gbl.neg_one);
        res = (int)(found - haystack);
        return res ? intvar_new(res) : VAR_NEW_REF(gbl.zero);
}

static Object *
do_bytes_find(Frame *fr)
{
        return bytes_index_or_find(fr, BF_SUPPRESS);
}

static Object *
do_bytes_index(Frame *fr)
{
        return bytes_index_or_find(fr, 0);
}

static Object *
do_bytes_rfind(Frame *fr)
{
        return bytes_index_or_find(fr, BF_RIGHT | BF_SUPPRESS);
}

static Object *
do_bytes_rindex(Frame *fr)
{
        return bytes_index_or_find(fr, BF_RIGHT);
}

static Object *
bytes_removelr(Frame *fr, unsigned int flags)
{
        const unsigned char *needle, *haystack;
        size_t idx, nlen, hlen;

        if (bytes_unpack_1arg(fr, &haystack, &hlen, &needle, &nlen)
            == RES_ERROR) {
                return ErrorVar;
        }

        if (nlen > hlen)
                return VAR_NEW_REF(vm_get_this(fr));

        idx = !!(flags & BF_RIGHT) ? hlen - nlen : 0;
        if (memcmp(&haystack[idx], needle, nlen) != 0)
                return VAR_NEW_REF(vm_get_this(fr));

        if (!(flags & BF_RIGHT))
                haystack += nlen;
        return bytesvar_newf(haystack, hlen - nlen, BF_COPY);
}

static Object *
do_bytes_removeprefix(Frame *fr)
{
        return bytes_removelr(fr, 0);
}

static Object *
do_bytes_removesuffix(Frame *fr)
{
        return bytes_removelr(fr, BF_RIGHT);
}

static Object *
bytes_starts_or_ends_with(Frame *fr, unsigned int flags)
{
        Object *ret;
        const unsigned char *needle, *haystack;
        size_t nlen, hlen, idx;

        if (bytes_unpack_1arg(fr, &haystack, &hlen, &needle, &nlen)
            == RES_ERROR) {
                return ErrorVar;
        }

        if (nlen > hlen)
                return VAR_NEW_REF(gbl.zero);

        idx = !!(flags & BF_RIGHT) ? hlen - nlen : 0;
        ret = memcmp(&haystack[idx], needle, nlen) ? gbl.zero : gbl.one;
        return VAR_NEW_REF(ret);
}

static Object *
do_bytes_endswith(Frame *fr)
{
        return bytes_starts_or_ends_with(fr, BF_RIGHT);
}

static Object *
do_bytes_startswith(Frame *fr)
{
        return bytes_starts_or_ends_with(fr, 0);
}

static Object *
do_bytes_join(Frame *fr)
{
        Object *self, *arg, **data;
        unsigned char *newbuf, *dst;
        const unsigned char *joinbuf;
        size_t i, total_size, joinlen, arglen;

        self = vm_get_this(fr);
        arg = vm_get_arg(fr, 0);

        if (arg_type_check(self, &BytesType) == RES_ERROR)
                return ErrorVar;
        joinbuf = bytes_get_data(self);
        joinlen = seqvar_size(self);

        /*
         * The only sequence types that could possibly return bytes types
         * are arrays or lists.  (Dictionaries cannot, because EvilCandy
         * does not support bytes-type keys).  So we can simplify this
         * by using direct access to a raw array.
         */
        if (isvar_array(arg)) {
                data = array_get_data(arg);
        } else if (isvar_tuple(arg)) {
                data = tuple_get_data(arg);
        } else {
                err_argtype(typestr(arg));
                return ErrorVar;
        }
        total_size = 0;
        arglen = seqvar_size(arg);
        for (i = 0; i < arglen; i++) {
                Object *d = data[i];
                if (i > 0)
                        total_size += joinlen;
                if (!isvar_bytes(d)) {
                        err_setstr(TypeError,
                                   "Expected bytes type in sequence but found %s",
                                   typestr(d));
                        return ErrorVar;
                }
                total_size += seqvar_size(d);
        }

        if (!total_size)
                return VAR_NEW_REF(gbl.empty_bytes);

        newbuf = dst = emalloc(total_size);
        for (i = 0; i < arglen; i++) {
                Object *d = data[i];
                size_t dlen = seqvar_size(d);
                if (i > 0) {
                        memcpy(dst, joinbuf, joinlen);
                        dst += joinlen;
                }
                memcpy(dst, bytes_get_data(d), dlen);
                dst += dlen;
        }
        return bytesvar_newf(newbuf, total_size, 0);
}

static Object *
bytes_lrpartition(Frame *fr, unsigned int flags)
{
        Object *tup, **td;
        const unsigned char *haystack, *needle, *found;
        size_t hlen, nlen;

        if (bytes_unpack_1arg(fr, &haystack, &hlen, &needle, &nlen)
            == RES_ERROR) {
                return ErrorVar;
        }

        if (nlen == 0) {
                err_setstr(ValueError, "Separator may not be empty");
                return ErrorVar;
        }

        if (!!(flags & BF_RIGHT))
                found = memrmem(haystack, hlen, needle, nlen);
        else
                found = memmem(haystack, hlen, needle, nlen);

        tup = tuplevar_new(3);
        td = tuple_get_data(tup);
        VAR_DECR_REF(td[0]);
        VAR_DECR_REF(td[1]);
        VAR_DECR_REF(td[2]);
        if (!found) {
                td[0] = VAR_NEW_REF(vm_get_this(fr));
                td[1] = VAR_NEW_REF(gbl.empty_bytes);
                td[2] = VAR_NEW_REF(gbl.empty_bytes);
        } else {
                int idx = (int)(found - haystack);
                if (idx == 0) {
                        td[0] = VAR_NEW_REF(gbl.empty_bytes);
                } else {
                        td[0] = bytesvar_new(haystack, idx);
                }
                td[1] = VAR_NEW_REF(vm_get_arg(fr, 1));
                idx += nlen;
                if (idx == hlen) {
                        td[2] = VAR_NEW_REF(gbl.empty_bytes);
                } else {
                        td[2] = bytesvar_new(&haystack[idx], hlen - idx);
                }
        }
        return tup;
}

static Object *
do_bytes_partition(Frame *fr)
{
        return bytes_lrpartition(fr, 0);
}

static Object *
do_bytes_rpartition(Frame *fr)
{
        return bytes_lrpartition(fr, BF_RIGHT);
}

static Object *
do_bytes_replace(Frame *fr)
{
        const unsigned char *self, *old, *new;
        size_t selflen, oldlen, newlen, finallen;
        struct buffer_t b;
        /* TODO: count arg */

        if (bytes_unpack_self(fr, &self, &selflen) == RES_ERROR)
                return ErrorVar;
        if (bytes_unpack_argno(fr, 0, &old, &oldlen) == RES_ERROR)
                return ErrorVar;
        if (bytes_unpack_argno(fr, 1, &new, &newlen) == RES_ERROR)
                return ErrorVar;

        buffer_init(&b);
        while (selflen > 0) {
                size_t tlen;
                const unsigned char *next = memmem(self, selflen,
                                                   old, oldlen);
                if (!next) {
                        buffer_putd(&b, self, selflen);
                        break;
                }
                tlen = (size_t)(next - self);
                buffer_putd(&b, self, tlen);
                buffer_putd(&b, new, newlen);

                self += tlen + oldlen;
                selflen -= (tlen + oldlen);
                bug_on((ssize_t)selflen < 0);
        }
        finallen = buffer_size(&b);
        return bytesvar_newf(buffer_trim(&b), finallen, 0);
}

static Object *
do_bytes_lrjust(Frame *fr, unsigned int flags)
{
        Object *arg;
        const unsigned char *self;
        unsigned char *newbuf, *dst, *end;
        size_t selflen, newlen, padlen;

        bug_on((flags & (BF_CENTER|BF_RIGHT)) == (BF_CENTER|BF_RIGHT));
        if (bytes_unpack_self(fr, &self, &selflen) == RES_ERROR)
                return ErrorVar;

        arg = vm_get_arg(fr, 0);
        if (arg_type_check(arg, &IntType) == RES_ERROR)
                return ErrorVar;

        newlen = intvar_toi(arg);
        if (err_occurred() || (ssize_t)newlen < 0)
                return ErrorVar;

        if (newlen < selflen)
                newlen = selflen;
        padlen = newlen - selflen;
        if (!!(flags & BF_CENTER))
                padlen /= 2;

        if (!newlen)
                return VAR_NEW_REF(gbl.empty_bytes);

        newbuf = dst = emalloc(newlen);
        end = newbuf + newlen;
        if (!!(flags & (BF_CENTER | BF_RIGHT))) {
                while (padlen-- > 0)
                        *dst++ = ' ';
        }

        memcpy(dst, self, selflen);
        dst += selflen;

        bug_on(dst < end && !!(flags & BF_RIGHT));
        while (dst < end)
                *dst++ = ' ';
        return bytesvar_newf(newbuf, newlen, BF_COPY);
}

static Object *
do_bytes_center(Frame *fr)
{
        return do_bytes_lrjust(fr, BF_CENTER);
}

static Object *
do_bytes_ljust(Frame *fr)
{
        return do_bytes_lrjust(fr, 0);
}

static Object *
do_bytes_rjust(Frame *fr)
{
        return do_bytes_lrjust(fr, BF_RIGHT);
}

static Object *
bytes_lrsplit(Frame *fr, unsigned int flags)
{
        enum { LRSPLIT_STACK_SIZE = 64 };

        Object *kw, *separg, *maxarg, *ret;
        const unsigned char *self, *sep;
        size_t selflen;
        ssize_t seplen;
        int maxsplit;
        bool combine = false;

        if (bytes_unpack_self(fr, &self, &selflen) == RES_ERROR)
                return ErrorVar;

        kw = vm_get_arg(fr, 0);
        bug_on(!kw || !isvar_dict(kw));
        dict_unpack(kw,
                    STRCONST_ID(sep), &separg, NullVar,
                    STRCONST_ID(maxsplit), &maxarg, gbl.neg_one,
                    NULL);
        if (separg == NullVar) {
                /*
                 * FIXME: program flow should hinge on this.
                 * We shouldn't do the below memmem() call, but should
                 * instead slide across whitespace.
                 */
                combine = true;
                VAR_DECR_REF(separg);
                separg = VAR_NEW_REF(gbl.spc_bytes);
        }

        /* error until proven success */
        ret = ErrorVar;
        if (arg_type_check(separg, &BytesType) == RES_ERROR)
                goto out;

        if (seqvar_size(separg) == 0) {
                err_setstr(ValueError, "Separator may not be empty");
                goto out;
        }
        maxsplit = intvar_toi(maxarg);
        if (err_occurred())
                goto out;

        sep = bytes_get_data(separg);
        seplen = seqvar_size(separg);
        ret = arrayvar_new(0);
        if (!!(flags & BF_RIGHT)) {
                while (maxsplit != 0 && selflen != 0) {
                        size_t tlen;
                        const unsigned char *psep, *pnext;

                        maxsplit--;
                        psep = memrmem(self, selflen, sep, seplen);
                        if (!psep) {
                                break;
                        }
                        pnext = psep + seplen;
                        tlen = selflen - (pnext - self);
                        array_append(ret, bytesvar_new(pnext, tlen));
                        while (combine && psep >= &self[seplen] &&
                                    !memcmp(psep - seplen, sep, seplen)) {
                                psep -= seplen;
                        }
                        selflen = psep - self;
                }
                if (selflen != 0)
                        array_append(ret, bytesvar_new(self, seplen));
                array_reverse(ret);
        } else {
                while (maxsplit != 0 && selflen != 0) {
                        Object *new;
                        size_t tlen;
                        const unsigned char *psep;

                        maxsplit--;

                        psep = memmem(self, selflen, sep, seplen);
                        if (!psep)
                                break;
                        tlen = (ssize_t)(psep - self);
                        new = tlen ? bytesvar_new(self, tlen)
                                   : VAR_NEW_REF(gbl.empty_bytes);
                        array_append(ret, new);
                        self = psep + seplen;
                        selflen -= (tlen + seplen);
                        while (combine && selflen > seplen &&
                                        !memcmp(self, sep, seplen)) {
                                self += seplen;
                                selflen -= seplen;
                        }
                }
                if (selflen != 0)
                        array_append(ret, bytesvar_new(self, selflen));
        }
out:
        VAR_DECR_REF(separg);
        VAR_DECR_REF(maxarg);
        return ret;
}

static Object *
do_bytes_split(Frame *fr)
{
        return bytes_lrsplit(fr, 0);
}

static Object *
do_bytes_rsplit(Frame *fr)
{
        return bytes_lrsplit(fr, BF_RIGHT);
}

static Object *
bytes_lrstrip(Frame *fr, unsigned int flags)
{
        const unsigned char *self, *chars;
        size_t selflen, charslen, selflen_save;

        if (bytes_unpack_self(fr, &self, &selflen) == RES_ERROR)
                return ErrorVar;

        if (vm_get_arg(fr, 0) != NULL) {
                if (bytes_unpack_argno(fr, 0, &chars, &charslen) == RES_ERROR)
                        return ErrorVar;
        } else {
                chars = (unsigned char *)ASCII_WS_CHARS;
                charslen = ASCII_NWS_CHARS;
        }

        selflen_save = selflen;

        if (!(flags & BF_RIGHT)) {
                while (selflen != 0 &&
                            !!memchr(chars, (int)(*self), charslen)) {
                        self++;
                        selflen--;
                }
        }

        if (!!(flags & (BF_RIGHT|BF_CENTER))) {
                const unsigned char *end = &self[selflen - 1];
                while (end > self &&
                            !!memchr(chars, (int)(*end), charslen)) {
                        end--;
                        selflen--;
                }
        }

        if (selflen_save == selflen)
                return VAR_NEW_REF(vm_get_this(fr));

        if (!selflen)
                return VAR_NEW_REF(gbl.empty_bytes);

        return bytesvar_new(self, selflen);
}

static Object *
do_bytes_strip(Frame *fr)
{
        return bytes_lrstrip(fr, BF_CENTER);
}

static Object *
do_bytes_lstrip(Frame *fr)
{
        return bytes_lrstrip(fr, 0);
}

static Object *
do_bytes_rstrip(Frame *fr)
{
        return bytes_lrstrip(fr, BF_RIGHT);
}

static Object *
do_bytes_capitalize(Frame *fr)
{
        const unsigned char *self;
        unsigned char *newbuf, *dst;
        size_t i, selflen;

        if (bytes_unpack_self(fr, &self, &selflen) == RES_ERROR)
                return ErrorVar;

        if (!selflen)
                return VAR_NEW_REF(gbl.empty_bytes);

        newbuf = dst = emalloc(selflen);
        if (selflen)
                dst[0] = evc_toupper(self[0]);
        for (i = 1; i < selflen; i++)
                dst[i] = evc_tolower(self[i]);
        return bytesvar_newf(newbuf, selflen, 0);
}

static Object *
do_bytes_expandtabs(Frame *fr)
{
        static const char SPC = ' ';
        Object *kw, *tabarg;
        int tabsize, col, nextstop;
        const unsigned char *self;
        size_t i, selflen, newlen;
        struct buffer_t b;

        if (bytes_unpack_self(fr, &self, &selflen) == RES_ERROR)
                return ErrorVar;

        kw = vm_get_arg(fr, 0);
        bug_on(!kw || !isvar_dict(kw));
        dict_unpack(kw, STRCONST_ID(tabsize), &tabarg, gbl.eight, NULL);
        if (arg_type_check(tabarg, &IntType) == RES_ERROR) {
                VAR_DECR_REF(tabarg);
                return ErrorVar;
        }
        tabsize = intvar_toi(tabarg);
        VAR_DECR_REF(tabarg);

        if (err_occurred())
                return ErrorVar;

        if (tabsize < 0)
                tabsize = 0;

        buffer_init(&b);
        col = 0;
        nextstop = tabsize;
        for (i = 0; i < selflen; i++) {
                if (self[i] == '\n') {
                        col = 0;
                        nextstop = tabsize;
                        buffer_putd(&b, &self[i], 1);
                } else if (self[i] == '\t') {
                        if (col == nextstop)
                                nextstop += tabsize;
                        while (col < nextstop) {
                                buffer_putd(&b, &SPC, 1);
                                col++;
                        }
                        nextstop += tabsize;
                } else {
                        if (col == nextstop)
                                nextstop += tabsize;
                        buffer_putd(&b, &self[i], 1);
                        col++;
                }
        }
        newlen = buffer_size(&b);
        return bytesvar_newf(buffer_trim(&b), newlen, 0);
}

static Object *
bytes_is(Frame *fr, bool (*tst)(unsigned long))
{
        Object *self = vm_get_this(fr);
        const unsigned char *p8;
        size_t i, len;

        if (arg_type_check(self, &BytesType) == RES_ERROR)
                return ErrorVar;

        if (seqvar_size(self) == 0)
                return VAR_NEW_REF(gbl.zero);

        p8 = bytes_get_data(self);
        len = seqvar_size(self);
        for (i = 0; i < len; i++) {
                if (!tst(p8[i]))
                        return VAR_NEW_REF(gbl.zero);
        }
        return VAR_NEW_REF(gbl.one);
}

static Object *
do_bytes_isalnum(Frame *fr)
{
        return bytes_is(fr, evc_isalnum);
}

static Object *
do_bytes_isalpha(Frame *fr)
{
        return bytes_is(fr, evc_isalpha);
}

static Object *
do_bytes_isascii(Frame *fr)
{
        return bytes_is(fr, evc_isascii);
}

static Object *
do_bytes_isdigit(Frame *fr)
{
        return bytes_is(fr, evc_isdigit);
}

static Object *
do_bytes_islower(Frame *fr)
{
        return bytes_is(fr, evc_islower);
}

static Object *
do_bytes_isspace(Frame *fr)
{
        return bytes_is(fr, evc_isspace);
}

static Object *
do_bytes_isupper(Frame *fr)
{
        return bytes_is(fr, evc_isupper);
}

static Object *
do_bytes_istitle(Frame *fr)
{
        Object *self = vm_get_this(fr);
        const unsigned char *p8;
        size_t i, len;
        bool first = true;

        if (arg_type_check(self, &BytesType) == RES_ERROR)
                return ErrorVar;

        if (seqvar_size(self) == 0)
                return VAR_NEW_REF(gbl.zero);

        p8 = bytes_get_data(self);
        len = seqvar_size(self);
        for (i = 0; i < len; i++) {
                int c = p8[i];
                if (!evc_isalpha(c)) {
                        first = true;
                } else if (first) {
                        if (evc_islower(c))
                                return VAR_NEW_REF(gbl.zero);
                        first = false;
                }
        }
        return VAR_NEW_REF(gbl.one);
}

static Object *
bytes_convert_case(Frame *fr, unsigned long (*convert)(unsigned long))
{
        Object *self;
        unsigned char *dst;
        const unsigned char *src;
        size_t i, len;

        self = vm_get_this(fr);

        if (arg_type_check(self, &BytesType) == RES_ERROR)
                return ErrorVar;

        src = bytes_get_data(self);
        len = seqvar_size(self);
        if (!len)
                return VAR_NEW_REF(gbl.empty_bytes);
        dst = emalloc(len);

        for (i = 0; i < len; i++)
                dst[i] = convert(src[i]);
        return bytesvar_new(dst, len);
}

static Object *
do_bytes_lower(Frame *fr)
{
        return bytes_convert_case(fr, evc_tolower);
}

static Object *
do_bytes_swapcase(Frame *fr)
{
        return bytes_convert_case(fr, to_swapcase);
}

static Object *
do_bytes_upper(Frame *fr)
{
        return bytes_convert_case(fr, evc_toupper);
}

static Object *
do_bytes_splitlines(Frame *fr)
{
        const unsigned char *src;
        size_t srclen;
        int keepends;
        Object *kw, *keeparg, *ret;

        if (bytes_unpack_self(fr, &src, &srclen) == RES_ERROR)
                return ErrorVar;

        kw = vm_get_arg(fr, 0);
        bug_on(!kw || !isvar_dict(kw));
        dict_unpack(kw, STRCONST_ID(keepends), &keeparg, gbl.zero, NULL);
        if (arg_type_check(keeparg, &IntType) == RES_ERROR) {
                VAR_DECR_REF(keeparg);
                return ErrorVar;
        }
        keepends = intvar_toll(keeparg);
        VAR_DECR_REF(keeparg);
        ret = arrayvar_new(0);
        while (srclen) {
                size_t next, i;
                for (i = 0; i < srclen; i++) {
                        if (src[i] == '\r' || src[i] == '\n')
                                break;
                }
                if (i == srclen) {
                        array_append(ret,
                                     bytesvar_new(src, srclen));
                        break;
                }
                next = i;
                switch (src[i]) {
                case '\n':
                        break;
                case '\r':
                        if (i < srclen - 1 && src[i+1] == '\r')
                                next++;
                        break;
                default:
                        bug();
                }
                array_append(ret, bytesvar_new(src, keepends ? next : i));
                src += next;
                srclen -= next;
        }

        return ret;
}

static Object *
do_bytes_title(Frame *fr)
{
        const unsigned char *self;
        unsigned char *newbuf, *dst;
        size_t i, selflen;
        bool first;

        if (bytes_unpack_self(fr, &self, &selflen) == RES_ERROR)
                return ErrorVar;

        if (!selflen)
                return VAR_NEW_REF(gbl.empty_bytes);
        dst = newbuf = emalloc(selflen);
        first = true;
        for (i = 0; i < selflen; i++) {
                unsigned int c = self[i];
                if (evc_isalpha(c)) {
                        if (first) {
                                c = toupper(c);
                                first = false;
                        } else {
                                c = tolower(c);
                        }
                } else {
                        first = true;
                }
                *dst++ = c;
        }
        return bytesvar_newf(newbuf, selflen, 0);
}

static Object *
do_bytes_zfill(Frame *fr)
{
        Object *arg;
        const unsigned char *self, *src;
        unsigned char *newbuf, *dst;
        size_t i, selflen, newlen;
        ssize_t nz;

        if (bytes_unpack_self(fr, &self, &selflen) == RES_ERROR)
                return ErrorVar;

        arg = vm_get_arg(fr, 0);
        if (arg_type_check(arg, &IntType) == RES_ERROR)
                return ErrorVar;
        nz = intvar_toi(arg);
        if (err_occurred())
                return ErrorVar;
        newlen = nz < selflen ? selflen : nz;
        newbuf = emalloc(newlen);
        nz -= selflen;
        src = self;
        dst = newbuf;
        if (selflen) {
                if (*src == '-' || *src == '+') {
                        *dst++ = *src++;
                        selflen--;
                }
        }

        for (i = 0; i < nz; i++)
                *dst++ = '0';
        if (selflen)
                memcpy(dst, src, selflen);
        return bytesvar_newf(newbuf, newlen, 0);
}

static Object *
bytes_getprop_length(Object *self)
{
        bug_on(!isvar_bytes(self));
        return intvar_new(seqvar_size(self));
}

static const struct type_prop_t bytes_prop_getsets[] = {
        { .name = "length", .getprop = bytes_getprop_length, .setprop = NULL },
        { .name = NULL },
};

static const struct type_inittbl_t bytes_cb_methods[] = {
        V_INITTBL("capitalize",   do_bytes_capitalize,   0, 0, -1, -1),
        V_INITTBL("center",       do_bytes_center,       1, 1, -1, -1),
        V_INITTBL("count",        do_bytes_count,        1, 1, -1, -1),
        V_INITTBL("decode",       do_bytes_decode,       0, 0, -1, -1),
        V_INITTBL("endswith",     do_bytes_endswith,     1, 1, -1, -1),
        V_INITTBL("expandtabs",   do_bytes_expandtabs,   1, 1, -1,  0),
        V_INITTBL("find",         do_bytes_find,         1, 1, -1, -1),
        V_INITTBL("index",        do_bytes_index,        1, 1, -1, -1),
        V_INITTBL("isalnum",      do_bytes_isalnum,      0, 0, -1, -1),
        V_INITTBL("isalpha",      do_bytes_isalpha,      0, 0, -1, -1),
        V_INITTBL("isascii",      do_bytes_isascii,      0, 0, -1, -1),
        V_INITTBL("isdigit",      do_bytes_isdigit,      0, 0, -1, -1),
        V_INITTBL("islower",      do_bytes_islower,      0, 0, -1, -1),
        V_INITTBL("isspace",      do_bytes_isspace,      0, 0, -1, -1),
        V_INITTBL("istitle",      do_bytes_istitle,      0, 0, -1, -1),
        V_INITTBL("isupper",      do_bytes_isupper,      0, 0, -1, -1),
        V_INITTBL("join",         do_bytes_join,         1, 1, -1, -1),
        V_INITTBL("ljust",        do_bytes_ljust,        1, 1, -1, -1),
        V_INITTBL("lower",        do_bytes_lower,        0, 0, -1, -1),
        V_INITTBL("lstrip",       do_bytes_lstrip,       0, 1, -1, -1),
        V_INITTBL("partition",    do_bytes_partition,    1, 1, -1, -1),
        V_INITTBL("removeprefix", do_bytes_removeprefix, 1, 1, -1, -1),
        V_INITTBL("removesuffix", do_bytes_removesuffix, 1, 1, -1, -1),
        V_INITTBL("replace",      do_bytes_replace,      2, 2, -1, -1),
        V_INITTBL("rfind",        do_bytes_rfind,        1, 1, -1, -1),
        V_INITTBL("rindex",       do_bytes_rindex,       1, 1, -1, -1),
        V_INITTBL("rjust",        do_bytes_rjust,        1, 1, -1, -1),
        V_INITTBL("rpartition",   do_bytes_rpartition,   1, 1, -1, -1),
        V_INITTBL("rsplit",       do_bytes_rsplit,       1, 1, -1,  0),
        V_INITTBL("rstrip",       do_bytes_rstrip,       0, 1, -1, -1),
        V_INITTBL("split",        do_bytes_split,        1, 1, -1,  0),
        V_INITTBL("splitlines",   do_bytes_splitlines,   1, 1, -1,  0),
        V_INITTBL("startswith",   do_bytes_startswith,   1, 1, -1, -1),
        V_INITTBL("strip",        do_bytes_strip,        0, 1, -1, -1),
        V_INITTBL("swapcase",     do_bytes_swapcase,     0, 0, -1, -1),
        V_INITTBL("title",        do_bytes_title,        0, 0, -1, -1),
        V_INITTBL("upper",        do_bytes_upper,        0, 0, -1, -1),
        V_INITTBL("zfill",        do_bytes_zfill,        1, 1, -1, -1),

        /* TODO: lstrip, just, etc. */
        TBLEND,
};

static const struct seq_methods_t bytes_seq_methods = {
        .getitem        = bytes_getitem,
        .setitem        = NULL, /* like string, immutable */
        .hasitem        = bytes_hasitem,
        .getslice       = bytes_getslice,
        .cat            = bytes_cat,
        .sort           = NULL,
};

struct type_t BytesType = {
        .flags  = 0,
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
        .prop_getsets = bytes_prop_getsets,
};

