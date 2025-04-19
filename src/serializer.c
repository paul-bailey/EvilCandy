/*
 * serializer.c - Code that loads or writes out a serialized byte code file.
 *
 * The format of an EvilCandy byte code file is:
 *
 *         header       variable length
 *         exec0        variable length
 *         exec1
 *         ...
 *         execN
 *         footer       32 bits
 *
 * ...all in network byte order.  execX is the serialized version of a
 * struct xptrvar_t.  exec0 is the entry point, and exec1..N are
 * functions, if the script has any.
 *
 * Header format
 * -------------
 *
 *         magic number         32 bits
 *         number of execs      32 bits
 *         version              16 bits <- for ABI backwards-compatibility
 *         file name            string, variable length
 *
 * Serialized struct xptrvar_t format
 * -------------------------------------
 *
 *      Header:
 *          magic               8 bits
 *          .file_line          32 bits
 *          .uuid               string, variable length
 *      .instr array:
 *          header:
 *              magic           8 bits
 *              .n_instr        32 bits
 *          array:
 *              .instr[0]       32 bits
 *                 ...
 *              .instr[n]
 *      .rodata array:
 *          header:
 *              magic           8 bits
 *              .n_rodata       32 bits
 *          .rodata[0]:
 *              magic           8 bits, a TYPE_XXX enum
 *              data            variable length depending on magic, could be 0
 *            ...               (*64-bit values are network-endian all the way)
 *          .rodata[n]
 *      .label array:
 *          header:
 *              magic           8 bits
 *              .n_label        32 bits
 *          .label[0]           16 bits
 *             ...
 *          .label[n]
 *
 * Footer format
 * -------------
 *
 *         magic number         8 bits
 *         checksum             16 bits
 *
 * where checksum is the same as the checksum field in network packets,
 * which RFC 793 explains as "the 16-bit ones' complement of the ones'
 * complement sum of all 16-bit words in the header and text."
 * Note that there are no pad bytes to guarantee that the file size is
 * an even number.  If necessary, a fictitious zero byte is used with
 * the last real byte (the footer's magic number) to complete the
 * checksum algorithm, but it is not written to the file.
 *
 * Serialized version of a string
 * ------------------------------
 *
 * This is used for .rodata, uuid, and the file name in the header.
 *
 *      length (incl. null term.)       32 bits (wow!)
 *      text + nulchar termination      variable length
 */

#include <evilcandy.h>
#include <xptr.h>

#include <errno.h>
#include <sys/stat.h>

/* Early debug version, this is kind of meaningless right now */
#define EVILCANDY_SERIAL_VERSION        1

enum {
        /* Magic numbers for top-level structs */
        HEADER_MAGIC    =       0x45564300, /* big-endian "EVC\0" */
        FOOTER_MAGIC    =       'F',
        EXEC_MAGIC      =       'X',

        /* magic numbers for field in struct xptrvar_t */
        INSTR_MAGIC     = 'I',
        RODATA_MAGIC    = 'R',
        LABEL_MAGIC     = 'L'
};


/* **********************************************************************
 *                      Checksum portion
 ***********************************************************************/

/*
 * One's-compliment add for checksum
 */
static inline uint32_t
ocadd32(uint32_t a, uint32_t b)
{
        uint32_t ret = a + b;
        if (ret < a)
                ret++;
        return ret;
}


/*
 * One's-compliment wrap, for when a bunch of 16-bit numbers were
 * added without wrapping the carry bit each add, to save time.
 */
static inline uint32_t
ocwrap32(uint32_t x)
{
        x = (x & 0xffffu) + (x >> 16);
        x = (x & 0xffffu) + (x >> 16);
        return x;
}

/*
 * TODO: If len > some_threshold, use a faster version of this algo.
 * I've written one for a different project, but it requires things
 * like macros that check machine's endianness, and those are
 * compiler-specific.
 */
static uint32_t
csum_basic(void *buf, size_t len)
{
        uint8_t *p = buf;
        uint8_t *end;
        uint32_t sum = 0;

        end = p + (len & ~1);
        while (p < end) {
                uint16_t v = p[0] | p[1] << 8;
                sum += v;
                p += 2;
        }
        if (!!(len & 1))
                sum += *p;
        return sum;
}

/*
 * Continue checksum on some contiguous data, starting from @sum.
 *
 * We have two problems to address, having to do with this being only a
 * part of a full checksum for non-contiguous data that could point to
 * who-knows-where:
 *
 *      (1) A 16-bit checksum word might straddle the end of the previous
 *          call and the start of this call.
 *      (2) @buf might not be aligned in RAM
 *
 * Problem 1 is addressed by the functions below that wrap this call,
 * using @odd to tell us whether or not Problem 1 exists.
 *
 * Problem 2 is not a problem at all, because we're doing things the
 * slow way, one byte at a time.
 *
 * Return value's upper 16 bits may have a small straggling sum of
 * carries that still need to be added to the LSBs before being turned
 * into the final checksum.
 */
static uint32_t
csum_continue(void *buf, size_t len, uint32_t sum, bool odd)
{
        uint8_t *p = (uint8_t *)buf;
        uint32_t tsum = 0;
        bug_on(!len);
        if (odd) {
                tsum += *p++ << 8;
                len--;
        }
        if (len)
                tsum += csum_basic(p, len);
        return ocadd32(sum, tsum);
}

static uint16_t
csum_finish(uint32_t sum)
{
        return (uint16_t)(~ocwrap32(sum));
}


/* **********************************************************************
 *                      Read portion
 ***********************************************************************/

struct serial_rstate_t {
        FILE *fp;
        const char *file_name;
        unsigned char *buf;
        unsigned char *head;     /* produce pointer */
        unsigned char *tail;     /* consume pointer */
        unsigned char *end;      /* top of allocated buf */
        uint32_t csum;
        bool ran_csum;
        bool odd;
};

static void
bad_checksum(void)
{
        err_setstr(RuntimeError, "byte code file bad checksum");
}

/* return file size, or zero if we can't determine it */
static off_t
rfile_size(struct serial_rstate_t *state)
{
        struct stat st;
        int res = fstat(fileno(state->fp), &st);
        if (res != 0) {
                /* can't get file size */
                errno = 0;
                return (off_t)0;
        }
        return (off_t)st.st_size;
}

static unsigned char *
rbuf(struct serial_rstate_t *state, size_t nbytes)
{
        size_t alloc_bytes;
        size_t have_bytes = state->head - state->tail;
        size_t rcount;
        unsigned char *ret;

        bug_on((int)have_bytes < 0);

        if (nbytes < have_bytes)  /* we had read entire file at once */
                goto fastpath;

        if (have_bytes != 0) {
                /*
                 * shenanigans - if we had read the whole file in at once,
                 * then this means the file was malformed.  All other
                 * occasions, it means we have a bug.
                 */
                bug_on(state->ran_csum);
                err_setstr(RuntimeError, "malformed byte-code file");
                return NULL;
        }

        /* need to read from file */
        alloc_bytes = state->end - state->buf;
        bug_on((int)alloc_bytes < 0);
        if (alloc_bytes < nbytes) {
                /* XXX: may trigger an unnecessary memcpy in realloc */
                state->buf = erealloc(state->buf, nbytes);
                state->end = state->buf + nbytes;
        }
        state->tail = state->head = state->buf;

        rcount = fread(state->buf, 1, nbytes, state->fp);
        if (rcount != nbytes) {
                err_errno(notdir(state->file_name));
                return NULL;
        }
        state->head += nbytes;

fastpath:
        ret = state->tail;
        if (!state->ran_csum) {
                state->csum = csum_continue(state->buf, nbytes,
                                            state->csum, state->odd);
                state->odd = !state->odd && !!(nbytes & 1);
        }
        state->tail += nbytes;
        return ret;
}

static unsigned long
rlong(struct serial_rstate_t *state)
{
        unsigned long x;
        unsigned char *buf = rbuf(state, 4);
        if (!buf)
                return 0;

        x = *buf++ << 24;
        x |= *buf++ << 16;
        x |= *buf++ << 8;
        x |= *buf++;
        return x;
}

static unsigned long long
rllong(struct serial_rstate_t *state)
{
        unsigned long long x;
        x = rlong(state) << 32;
        x |= rlong(state);
        return x;
}

static unsigned int
rshort(struct serial_rstate_t *state)
{
        unsigned int x;
        unsigned char *buf = rbuf(state, 2);
        if (!buf)
                return 0;

        x = *buf++ << 8;
        x |= *buf++;
        return x;
}

static unsigned char
rbyte(struct serial_rstate_t *state)
{
        unsigned char *buf = rbuf(state, 1);
        return buf ? *buf : 0;
}

static double
rdouble(struct serial_rstate_t *state)
{
        /*
         * XXX REVISIT: This is the in-a-perfect-world way to do things,
         * where every computing platform makes sense, but the proper,
         * truly platform-independent way to do this is with a
         * painstaking bit-by-bit binary reconstruction of the exponent,
         * mantissa, and sign.
         */
        union {
                long long i;
                double f;
        } x;
        x.i = rllong(state);
        return x.f;
}

static char *
rstring(struct serial_rstate_t *state, size_t *n)
{
        char *str, *ret;
        unsigned long len = rlong(state);
#warning "fix this up when finished debugging"
        if (err_occurred()) /* || (long)len < 0) */
                return NULL;
#ifndef NDEBUG
        if (len > 0xffffu) {
                DBUG("suspicious string length %lu", len);
                bug();
        }
#endif /* NDEBUG */
        /* note: 'len' includes the nulchar termination */
        str = (char *)rbuf(state, len);
        *n = len;
        if (!str)
                return NULL;

        if (str[len-1] != '\0' || strlen(str) != len-1) {
                err_setstr(RuntimeError, "malformed string");
                return NULL;
        }
        ret = emalloc(len);
        memcpy(ret, str, len);
        return ret;
}

struct serial_header_t {
        char *file_name;
        int nexec;
        int version;
};

static int
read_header(struct serial_rstate_t *state, struct serial_header_t *hdr)
{
        size_t dummylen;
        unsigned long magic = rlong(state);
        if (err_occurred() || magic != HEADER_MAGIC) {
                DBUG("Expected magic %lX but got magic %lX",
                     (unsigned long)HEADER_MAGIC, magic);
                return RES_ERROR;
        }
        hdr->nexec = rlong(state);
        hdr->version = rshort(state);
        hdr->file_name = rstring(state, &dummylen);
        if (!hdr->file_name)
                return RES_ERROR;

        /* currently only support the version I'm on */
        if (hdr->version != EVILCANDY_SERIAL_VERSION) {
                err_setstr(RuntimeError,
                           "Cannot parse byte code version %u",
                           hdr->version);
                return RES_ERROR;
        }
#warning "fix this up when finished debugging."
        if (hdr->nexec > 0xffffu) {
                DBUG("suspicious number of executables %u", hdr->nexec);
                return RES_ERROR;
        }
        if (err_occurred())
                return RES_ERROR;


        return RES_OK;
}

static int
read_footer(struct serial_rstate_t *state)
{
        unsigned char *csum, magic;

        magic = rbyte(state);
        if (err_occurred() || magic != FOOTER_MAGIC)
                return RES_ERROR;
        if ((csum = rbuf(state, 2)) == NULL)
                return RES_ERROR;

        if (!state->ran_csum) {
                /* finish up checksum */
                state->csum = csum_continue(csum, 2,
                                        state->csum, state->odd);
                state->csum = csum_finish(state->csum);
                if (state->csum != 0) {
                        bad_checksum();
                        return RES_ERROR;
                }
                /*
                 * TODO: ftell or something to make sure we're
                 * at the end of the file.
                 */
        } else {
                if (state->tail != state->head) {
                        err_setstr(RuntimeError,
                                   "Excess elements in byte code file");
                }
        }

        return RES_OK;
}

static int
read_xinstructions(struct serial_rstate_t *state, struct xptrvar_t *ex)
{
        int i;
        bug_on(sizeof(instruction_t) != 4);
        ex->instr = emalloc(sizeof(instruction_t) * ex->n_instr);

        for (i = 0; i< ex->n_instr; i++) {
                bug_on(sizeof(instruction_t) != 4);
                union {
                        instruction_t ii;
                        uint32_t ul;
                } x;
                x.ul = rlong(state);
                ex->instr[i] = x.ii;
                if (ex->instr[i].code >= N_INSTR) {
                        err_setstr(RuntimeError,
                                   "byte code error: malformed instruction %u",
                                   ex->instr[i].code);
                        goto err;
                }
                /*
                 * TODO: Also need to also check .arg1 and .arg2
                 * and make sure they're valid.
                 * Best to make a function called
                 * validate_instruction().
                 */
        }
        if (err_occurred())
                goto err;

        return RES_OK;
err:
        efree(ex->instr);
        return RES_ERROR;
}

static int
read_labels(struct serial_rstate_t *state, struct xptrvar_t *ex)
{
        int i;
        bug_on(sizeof(*ex->label) != 2);
        ex->label = emalloc(2 * ex->n_label);
        for (i = 0; i < ex->n_label; i++)
                ex->label[i] = rshort(state);
        if (err_occurred())
                goto err;
        return RES_OK;

err:
        efree(ex->label);
        return RES_ERROR;
}

static int
read_rodata(struct serial_rstate_t *state, struct xptrvar_t *ex)
{
        int i;
        ex->rodata = emalloc(sizeof(void *) * ex->n_rodata);
        for (i = 0; i < ex->n_rodata; i++) {
                size_t len; /* dummy for now */
                char *s;
                unsigned char magic = rbyte(state);
                struct var_t *v = NULL;
                switch (magic) {
                case TYPE_EMPTY:
                        VAR_INCR_REF(NullVar);
                        v = NullVar;
                        break;
                case TYPE_FLOAT: {
                        v = floatvar_new(rdouble(state));
                        break;
                }
                case TYPE_INT:
                        v = intvar_new(rllong(state));
                        break;
                case TYPE_STRPTR:
                        s = rstring(state, &len);
                        if (!s)
                                break;
                        /*
                         * XXX literal_put() adds this string permanently
                         * to the running program.  Is there no way to
                         * put this off as long as possible, so we aren't
                         * stuck with it in case this fails later?
                         */
                        v = stringvar_nocopy(s);
                        break;
                case TYPE_XPTR:
                        s = rstring(state, &len);
                        if (!s)
                                break;
                        v = uuidptrvar_new(s);
                        /* don't free s, it's now in @v */
                        break;
                default:
                        break;
                }

                if (v == NULL || err_occurred())
                        break;
                ex->rodata[i] = v;
        }

        if (err_occurred() || i < ex->n_rodata) {
                /* had an error, need to unwind and free */
                for (i = 0; i < ex->n_rodata; i++) {
                        struct var_t *v = ex->rodata[i];
                        if (!v)
                                break;

                        VAR_DECR_REF(ex->rodata[i]);
                }
                efree(ex->rodata);
                return RES_ERROR;
        }
        return RES_OK;
}

static int
read_executable(struct serial_rstate_t *state, struct xptrvar_t *ex)
{
        size_t len; /* dummy */
        unsigned char v8;
        int res;

        /* Get header */
        v8 = rbyte(state);
        ex->file_line = rlong(state);
        ex->uuid = rstring(state, &len);
        if (err_occurred() || v8 != EXEC_MAGIC || !ex->uuid)
                return RES_ERROR;

        /* read subsection for instructions */
        v8 = rbyte(state);
        ex->n_instr = rlong(state);
        if (err_occurred() || v8 != INSTR_MAGIC || ex->n_instr < 0)
                return RES_ERROR;
        res = read_xinstructions(state, ex);
        if (err_occurred() || res != RES_OK)
                return res;

        /* read subsection for rodata */
        v8 = rbyte(state);
        ex->n_rodata = rlong(state);
        if (err_occurred() || v8 != RODATA_MAGIC || ex->n_rodata < 0)
                goto err_have_instr;
        res = read_rodata(state, ex);
        if (err_occurred() || res != RES_OK)
                goto err_have_instr;

        /* read subsection for labels */
        v8 = rbyte(state);
        ex->n_label = rlong(state);
        if (err_occurred() || v8 != LABEL_MAGIC || ex->n_label < 0)
                goto err_have_rodata;
        res = read_labels(state, ex);
        if (err_occurred() || res != RES_OK)
                goto err_have_rodata;

        /* finish up */
        return RES_OK;

err_have_rodata:
        efree(ex->rodata);
        ex->n_rodata = 0;

err_have_instr:
        efree(ex->instr);
        ex->n_instr = 0;
        return RES_ERROR;
}

static struct xptrvar_t *
seek_uuid(const char *uuid, struct xptrvar_t **xa, int n)
{
        int i;
        for (i = 0; i < n; i++) {
                if (!strcmp(xa[i]->uuid, uuid))
                        return xa[i];
        }
        return NULL;
}

/*
 * In serial bitstream, TYPE_XPTR .rodata vars are strings containing
 * a UUID.  This points them instead to the struct executable containing
 * that UUID.
 */
static int
resolve_uuid(struct xptrvar_t *ex, struct xptrvar_t **xa, int n)
{
        int i;
        for (i = 0; i < ex->n_rodata; i++) {
                struct var_t *v = ex->rodata[i];
                struct xptrvar_t *ref;

                if (!isvar_uuidptr(v))
                        continue;
                ref = seek_uuid(uuidptr_get_cstring(v), xa, n);
                if (ref == ex) {
                        /*
                         * probably a bug, but hypothetically it could be
                         * a maliciously malformed file.
                         */
                        DBUG("byte code executable may not reference itself");
                        return RES_ERROR;
                }
                if (ref == NULL) {
                        err_setstr(RuntimeError,
                                "Byte code references executable not in script");
                        return RES_ERROR;
                }

                VAR_DECR_REF(v);
                ex->rodata[i] = (struct var_t *)ref;

                /* do recursively for each child found */
                if (resolve_uuid(ref, xa, n) != RES_OK)
                        return RES_ERROR;
        }
        return RES_OK;
}

/**
 * serialize_read - Import a byte code file
 * @fp: handle to the file, open with in "r" mode.
 * @file_name: Name of the file, needed only for error messages etc.
 *
 * Return: entry-point struct executable, which is ready to run,
 *         or ErrorVar if there was an error.
 */
struct var_t *
serialize_read(FILE *fp, const char *file_name)
{
        enum {
                /*
                 * XXX: arbitrarily chosen, what's a good file
                 * size limit to store in RAM?
                 */
                FILE_BUFFER_MAX = 128 * 1024
        };
        struct serial_rstate_t state;
        struct serial_header_t hdr;
        struct xptrvar_t **exarray;
        struct xptrvar_t *ret;
        off_t size;
        int i, res;

        memset(&state, 0, sizeof(state));

        state.file_name = file_name;
        state.fp = fp;

        size = rfile_size(&state);

        /* size==0 means "we don't know the size" */
        if ((int)size > 0 && (int)size < FILE_BUFFER_MAX) {
                size_t nread;
                size_t len = (size_t)size;
                state.buf = emalloc(len);
                nread = fread(state.buf, 1, len, state.fp);
                if (nread != len) {
                        /* ok, failed the whole shebang after all */
                        err_errno("fread %s", file_name);
                        goto err_have_buffer;
                }
                state.tail = state.buf;
                state.end  = state.head = state.buf + len;
                state.csum = csum_continue(state.tail, len, 0, false);
                state.csum = csum_finish(state.csum);
                if (state.csum != 0) {
                        bad_checksum();
                        goto err_have_buffer;
                }
                state.ran_csum = true;
                /*
                 * If still here, everything set up to do
                 * things the fast way.  The entire file
                 * was read into a buffer.
                 */
        }
        /*
         * else, gotta do it the hard way.  The above memset to zero set
         * everything where it needs to be.
         */

        res = read_header(&state, &hdr);
        if (res != RES_OK || err_occurred()) {
                goto err_have_buffer;
        }

        /* after this point, we need ex for stuff */
        exarray = emalloc(sizeof(void *) * hdr.nexec);
        memset(exarray, 0, sizeof(void *) * hdr.nexec);

        for (i = 0; i < hdr.nexec; i++) {
                struct xptrvar_t *ex;

                /*
                 * We don't know line number yet, we'll fill that in later
                 *
                 * XXX: we also have to clobber an unnecessary uuid call,
                 * so maybe re-think args to xptrvar_new()
                 */
                ex = (struct xptrvar_t *)xptrvar_new(notdir(file_name), 0);
                exarray[i] = ex;

                res = read_executable(&state, ex);
                if (err_occurred() || res != RES_OK)
                        goto err_have_ex;
        }

        res = read_footer(&state);
        if (err_occurred() || res != RES_OK)
                goto err_have_ex;

        res = resolve_uuid(exarray[0], exarray, hdr.nexec);
        if (err_occurred() || res != RES_OK)
                goto err_have_ex;

        ret = exarray[0];

        /* no longer need array, ret's .rodata can reference the rest */
        efree(exarray);
        efree(state.buf);
        return (struct var_t *)ret;

err_have_ex:
        for (i = 0; i < hdr.nexec; i++) {
                if (exarray[i] == NULL)
                        break;
                VAR_DECR_REF((struct var_t *)exarray[i]);
        }
        efree(exarray);

err_have_buffer:
        efree(state.buf);
        if (!err_occurred()) {
                DBUG("Ghost error @%s, line %d", __FILE__, __LINE__);
                err_setstr(RuntimeError,
                           "Failed to read byte code file %s", file_name);
        }
        return ErrorVar;
}



/* **********************************************************************
 *                      Write portion
 ***********************************************************************/

struct serial_wstate_t {
        FILE *fp;
        struct buffer_t b;
        uint32_t csum;
        bool odd;
};

static int
wflush(struct serial_wstate_t *state)
{
        size_t wsize = buffer_size(&state->b);
        if (wsize != 0) {
                size_t n = fwrite(state->b.s, 1, wsize, state->fp);

                /* Update state->csum here */
                state->csum = csum_continue(state->b.s, wsize,
                                        state->csum, state->odd);
                state->odd = !state->odd && !!(wsize & 1);

                buffer_reset(&state->b);
                if (n != wsize)
                        return RES_ERROR;
        }
        return RES_OK;
}

/* doesn't flush like the others */
static void
wbyte(struct serial_wstate_t *state, unsigned char v)
{
        buffer_putd(&state->b, &v, 1);
}

static void
wbytes(struct serial_wstate_t *state, unsigned const char *buf, size_t n)
{
        buffer_putd(&state->b, buf, n);
}

static void
wshort(struct serial_wstate_t *state, unsigned int v)
{
        wbyte(state, v >> 8);
        wbyte(state, v);
}

static void
wlong(struct serial_wstate_t *state, unsigned long v)
{
        wbyte(state, v >> 24);
        wbyte(state, v >> 16);
        wbyte(state, v >> 8);
        wbyte(state, v);
}

static void
wllong(struct serial_wstate_t *state, unsigned long long v)
{
        /* XXX: portable? */
        unsigned long upper = v >> 32;
        wlong(state, upper);
        wlong(state, v);
}

static void
wdouble(struct serial_wstate_t *state, double d)
{
        union {
                unsigned long long i;
                double f;
        } x = { .f = d };
        wllong(state, x.i);
}

static void
wstring(struct serial_wstate_t *state, const char *s)
{
        size_t len = strlen(s) + 1;
        wlong(state, len);
        wbytes(state, (const unsigned char *)s, len);
}

static int
write_header(struct serial_wstate_t *state,
             unsigned int nexec, const char *file_name)
{
        wlong(state, HEADER_MAGIC);
        wlong(state, nexec);
        wshort(state, EVILCANDY_SERIAL_VERSION);
        wstring(state, notdir(file_name));
        return wflush(state);
}

static int
write_footer(struct serial_wstate_t *state)
{
        wlong(state, FOOTER_MAGIC);
        /* need to flush before finalizing checksum */
        if (wflush(state) != RES_OK)
                return -1;
        state->csum = csum_finish(state->csum);
        wshort(state, state->csum);
        return wflush(state);
}

static int
write_exec(struct serial_wstate_t *state, struct xptrvar_t *ex)
{
        int i;
        wbyte(state, EXEC_MAGIC);
        wlong(state, ex->file_line);
        wstring(state, ex->uuid);

        /* Write .instr array */
        wbyte(state, INSTR_MAGIC);
        wlong(state, ex->n_instr);
        i = 0;
        while (i < ex->n_instr) {
                /* try not to let our buffer blow up in size */
                int j;
                for (j = 0; j < 40 && i < ex->n_instr; j++, i++) {
                        /*
                         * ABI, not just API, so if we change this in
                         * instruction.h, get a friendly reminder to
                         * change it here too.
                         */
                        bug_on(sizeof(instruction_t) != 4);
                        union {
                                uint32_t u;
                                instruction_t x;
                        } x = { .x = ex->instr[i] };
                        wlong(state, x.u);
                }
                if (wflush(state) != RES_OK)
                        return RES_ERROR;
        }
        if (wflush(state) != RES_OK)
                return RES_ERROR;

        /* Write .rodata array */
        wbyte(state, RODATA_MAGIC);
        wlong(state, ex->n_rodata);
        for (i = 0; i < ex->n_rodata; i++) {
                struct var_t *v = ex->rodata[i];

                if (isvar_empty(v))
                        wbyte(state, TYPE_EMPTY);
                        continue;
                if (isvar_float(v)) {
                        wbyte(state, TYPE_FLOAT);
                        wdouble(state, floatvar_tod(v));
                } else if (isvar_int(v)) {
                        wbyte(state, TYPE_INT);
                        wllong(state, intvar_toll(v));
                } else if (isvar_string(v)) {
                        wbyte(state, TYPE_STRPTR);
                        wstring(state, string_get_cstring(v));
                } else if (isvar_xptr(v)) {
                        /*
                         * Of course we don't serialize an internal
                         * pointer.  Instead we use the executable's UUID
                         */
                        wbyte(state, TYPE_XPTR);
                        wstring(state, ((struct xptrvar_t *)v)->uuid);
                } else {
                        /* note StringType falls here, because all strings
                         * in .rodata are StrptrType
                         */
                        bug();
                }

                if (wflush(state) != RES_OK)
                        return RES_ERROR;
        }
        if (wflush(state) != RES_OK)
                return RES_ERROR;

        /* Write .label array */
        wbyte(state, LABEL_MAGIC);
        wlong(state, ex->n_label);
        for (i = 0; i < ex->n_label; i++) {
                /* not enough of these to need intermed. wflush calls */
                wshort(state, ex->label[i]);
        }
        if (wflush(state) != RES_OK)
                return RES_ERROR;

        /*
         * Now that we've written this one out, recursively write out any
         * others that are referenced in .rodata.  Remember, there is
         * globally at most one .rodata pointer for any unique struct
         * xptrvar_t, so we're not duplicating anything or doubling
         * back on ourselves.  A struct xptrvar_t can technically be
         * thought of as a node in a tree structure, even though that's
         * not at all how it is used.
         */
        for (i = 0; i < ex->n_rodata; i++) {
                struct var_t *v = ex->rodata[i];
                if (isvar_xptr(v)) {
                        int res = write_exec(state, (struct xptrvar_t *)v);
                        if (res != RES_OK)
                                return res;
                }
        }
        return RES_OK;
}

/* Count the number of execs, including @node */
static int
n_exec(struct xptrvar_t *node)
{
        int i;
        int count = 1; /* start with me */

        for (i = 0; i < node->n_rodata; i++) {
                struct var_t *v = node->rodata[i];
                if (isvar_xptr(v))
                        count += n_exec((struct xptrvar_t *)(v));
        }
        return count;
}

/**
 * serialize_write - Serialize a program to a byte code file
 * @fp: Open file to write to in binary mode, at position 0
 * @ex: Executable code to write.  As a general rule, this
 *      should be for the top level of a script, not a function.
 *
 * Return RES_OK if successful, RES_ERROR if not.
 */
int
serialize_write(FILE *fp, struct var_t *v)
{
        struct xptrvar_t *ex = (struct xptrvar_t *)v;
        struct serial_wstate_t state;
        int n;
        int res = 0;

        bug_on(!isvar_xptr(v));

        n = n_exec(ex);

        state.fp = fp;
        state.csum = 0;
        state.odd = false;
        buffer_init(&state.b);

        res = write_header(&state, n, ex->file_name);
        if (res != RES_OK)
                goto done;
        res = write_exec(&state, ex);
        if (res != RES_OK)
                goto done;
        res = write_footer(&state);

done:
        buffer_free(&state.b);
        return res;
}
