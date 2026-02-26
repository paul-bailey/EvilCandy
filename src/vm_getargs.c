/*
 * Inspired by, but not derivative of, cpython's generic argument parser.
 * see cpython source tree, 'Python/getargs.c'.
 *
 * XXX REVISIT: Only the top-level function (which is a very small part
 * of this file) actually deals with vm.h-specific stuff.  The
 * implementation (the remainder of this file) should be pulled into
 * its own more independent C file, and also unpack tuples, arrays,
 * and dictionaries.
 *
 * TODO: Add 'e' for enum... expect two va_arg() values, one for a table
 *       of non-negative numbers, the other to store the value.
 *
 * FORMAT STRINGS
 * --------------
 *
 * A format string contains a sequence of characters describing the data
 * to be C-ified from the object stack, in their expected order (called
 * "uarg" in this file), and the variadic C arguments which collect them
 * (which I'll call "varg").  All vargs are pointers.
 *
 * 'b'  Get integer that can fit into a byte. varg is a pointer to
 *      "char".  uarg is either a bytes object of size 1 or an integer
 *      object whose value is from -128 to 255.  Calling code should deal
 *      with the sign bit its own way.
 * 'h'  Get integer that cna fit into a short.  varg is a pointer to
 *      "short".  uarg is an integer object whose value is from -32768 to
 *      65535.  Calling code should deal with the sign bit its own way.
 * 'i'  Get a long long integer. varg is a pointer to "int".  uarg is an
 *      integer object whose positive value may not exceed the range of
 *      INT_MIN...UINT_MAX.
 * 'l'  Get a long long integer. varg is a pointer to 'long long'.
 *      uarg is an integer object.
 * 's'  Get a C string.  varg is a pointer to "char *".  uarg is a string
 *      object.  Warning!! This is a pointer into the Object's data, so
 *      if it needs to be saved for later, it should be copied.
 * 'c'  Get a single-character string's Unicode point. varg is a pointer
 *      to "long".  uarg is a string object whose size must be 1.
 * 'f'  Get a floating-point value. varg is a pointer to "double".
 *      uarg is a floating-point object.
 * '.'  Skip this argument or numerical index.
 *
 * '|'  End of mandatory arguments; remaining characters are for optional
 *      args.  If these optional arguments do not exist in the arguments,
 *      the pointers in vargs will not be modified.
 *
 *      If vargs in the 'optional' range are for c variables, they may be
 *      initialized before calling vm_getargs(); they will not be
 *      modified if the actual args do not exist.  If they are for
 *      objects (the <...> format, see below), initialize these to NULL;
 *      DO NOT initialize them to a default object, because you will not
 *      be able to tell if a reference was consumed or not.  Instead,
 *      wait until vm_getargs() returns, and set the still-NULL args to
 *      defaults.
 *
 *      If this exists within a dictionary, array, or tuple (see below),
 *      then only the remaining items until the closing brace are
 *      considered optional.
 *
 * "<...>"
 *      Get an object.  varg is a pointer to "Object *" (ie. a
 *      double-pointer).  Types of objects within the angle brackets
 *      are as follows:
 *
 *      <s>     string
 *      <c>     string whose size must be 1
 *      <i>     integer
 *      <f>     float
 *      <b>     bytes
 *      <z>     complex
 *      <x>     function
 *      <r>     range
 *      <{}>    dictionary
 *      <()>    tuple
 *      <[]>    array
 *      </>     file
 *      <*>     wild-card (type of object is not checked)
 *
 *      More than one type can be listed within the angle brackets,
 *      and if the object matches any one of those types, it will
 *      be permitted.  For example...
 *
 *      <()[]>  either a list or a tuple
 *      <sb>    either a string or a bytes
 *
 *      A special exception is <c>, which may not have additional types
 *      specified along with the 'c'.
 *
 *      Warning! As with vm_get_arg(), vm_getargs() does not produce any
 *      reference for '<...>' objects passed to callers.  If the calling
 *      code needs to store these objects, it should produce the
 *      reference itself.
 *
 *  '{...}'
 *  '(...)'
 *  '[...]'
 *      Next arg is a sequential object, list for [], tuple for (),
 *      or dictionary for {}.  Next varg does not store this object,
 *      but rather it gets the next item out of the object from within
 *      the braces.
 *
 *      In the case of vm_getargs_sv(), the format string MUST begin
 *      and end with these characters.
 *
 *      For ARRAYS and TUPLES, each varg is for each successive
 *      index number (except where '.' skips an index).  If the array
 *      or tuple is longer than required by the format string, this is
 *      not regarded as an error.
 *
 *      For DICTIONARIES, there are two vargs for each entry.  The
 *      first varg is a pointer to (Object *), which contains the key
 *      of the item, usually a STRCONST_ID() object.  The second varg
 *      depends on the character in fmt, as described above.
 *
 * Please do not abuse the nesting of tuples and dictionaries.  This
 * was merely inteded for argument unpacking.
 */
#include <evilcandy.h>
#include <stdarg.h>

/* flags arg...there used to be more */
enum {
        GAF_MANDO = 0x1, /* arg is mandatory */
};

static enum result_t convert_arg(int typec, Object *uarg, const char **fmt,
                                 va_list ap, unsigned int flags,
                                 const char *fname, int argno);
static enum result_t vm_getargs_1(Object **items, size_t n, const char **fmt,
                                  va_list ap, unsigned int flags, char endchr,
                                  const char *fname, int argno);

static void
vmerr_type_mismatch(int argno, const char *fname, Object *arg, struct type_t *type)
{
        char buf[512];
        char *p = buf;
        char *end = &buf[sizeof(buf)];
        if (fname)
                p += snprintf(p, end - p, "%s() ", fname);
        if (argno >= 0)
                p += snprintf(p, end - p, "argument %d ", argno + 1);
        p += snprintf(p, end - p, "expected %s but got %s",
                      type->name, typestr(arg));
        if (p == end)
                p--;
        *p = '\0';
        err_setstr(TypeError, "%s", buf);
}

static void
vmerr_generic(const char *msg, int argno, const char *fname)
{
        char buf[512];
        char *p = buf;
        char *end = &buf[sizeof(buf)];

        /*
         * Bug checks instead of non-fatal checks, because no code in
         * this file should be writing a message that will overrun @buf.
         */

        if (fname)
                p += snprintf(p, end-p, "%s() ", fname);
        if (argno >= 0)
                p += snprintf(p, end-p, "argument %d ", argno + 1);
        if (msg)
                p += snprintf(p, end-p, "%s", msg);
        if (p == end)
                p--;
        *p = '\0';
        err_setstr(TypeError, "%s", buf);
}

static enum result_t
get_dict_args(Object *dict, const char **fmt, va_list ap,
              unsigned int flags, const char *fname, int argno)
{
        Object *k, *v;
        const char *s = *fmt;

        for (;;) {
                enum result_t res;
                int c = *s++;
                bug_on(c == '\0');
                if (c == '}')
                        break;

                if (c == '|') {
                        bug_on(!(flags & GAF_MANDO));
                        flags &= ~GAF_MANDO;
                        continue;
                }

                /*
                 * Getting dictionary items from built-ins means there's
                 * no reason for it to not have a STRCONST_ID() pointer,
                 * so require k to always be an Object, never a C string.
                 */
                k = va_arg(ap, Object *);
                bug_on(!k || !isvar_string(k));

                if (dict) {
                        v = dict_getitem(dict, k);
                        if (!v) {
                                if (!!(flags & GAF_MANDO)) {
                                        err_setstr(TypeError,
                                                "%s%smissing %s item in dict",
                                                fname ? fname : "",
                                                fname ? "() " : "",
                                                string_cstring(k));
                                        return RES_ERROR;
                                }
                        }
                } else {
                        v = NULL;
                }
                res = convert_arg(c, v, &s, ap, flags, fname, argno);
                if (v)
                        VAR_DECR_REF(v);
                if (res == RES_ERROR)
                        return res;
        }
        *fmt = s;
        return RES_OK;
}

static enum result_t
get_array_args(Object *uarg, const char **fmt, va_list ap,
                unsigned int flags, const char *fname, int endchr, int argno)
{
        bool match;
        Object **items;
        size_t n;

        if (uarg) {
                switch (endchr) {
                case ']':
                        match = isvar_array(uarg);
                        break;
                case ')':
                        match = isvar_tuple(uarg);
                        break;
                case '}':
                        match = isvar_dict(uarg);
                        break;
                default:
                        match = false;
                        break;
                }
                if (!match) {
                        struct type_t *type = (endchr == '('
                                               ? &TupleType
                                               : (endchr == '['
                                                  ? &ArrayType
                                                  : &DictType));
                        vmerr_type_mismatch(-1, fname, uarg, type);
                        return RES_ERROR;
                }
        }
        if (endchr == '}') {
                return get_dict_args(uarg, fmt, ap,
                                     flags, fname, argno);
        }

        /* still here, process tuple or array */
        if (!uarg) {
                n = 0;
                items = NULL;
        } else {
                n = seqvar_size(uarg);
                switch (endchr) {
                case ']':
                        items = array_get_data(uarg);
                        break;
                case ')':
                        items = tuple_get_data(uarg);
                        break;
                default:
                        items = NULL;
                        bug();
                }
        }

        flags |= GAF_MANDO;
        return vm_getargs_1(items, n, fmt, ap,
                            flags | GAF_MANDO,
                            endchr, fname, argno);
}

static enum result_t
convert_arg(int typec, Object *uarg, const char **fmt, va_list ap,
            unsigned int flags, const char *fname, int argno)
{
        const char *s = *fmt;
        int c;
        void *pv;

        s = *fmt;
        c = typec;
        bug_on(c == '\0');

        if (typec == '<') {
                Object **ppo;
                const char *right = s;
                bool match;
                while (*right != '>') {
                        bug_on(*right == '\0');
                        right++;
                }

                *fmt = right + 1;
                ppo = va_arg(ap, Object **);

                if (!uarg)
                        return RES_OK;

                /*
                 * Special case: <C> expects a string of length 1.
                 * It makes no sense for something like <CS>, so
                 * treat <C...anything> like a bug
                 */
                if (s[0] == 'c') {
                        bug_on(s[1] != '>');
                        if (isvar_string(uarg)
                            && seqvar_size(uarg) == 1) {
                                *ppo = uarg;
                                return RES_OK;
                        }
                        vmerr_generic("string must have a size of 1",
                                      argno, fname);
                        return RES_ERROR;
                }

                /*
                 * Note: this will throw error in case of "<>".
                 * Wildcard objects must be expressed as "<*>".
                 */
                match = false;

                while (s < right && !match) {
                        int c = *s++;
                        struct type_t *type = NULL;
                        switch (c) {
                        case 's':
                                type = &StringType;
                                break;
                        case 'i':
                                type = &IntType;
                                break;
                        case 'f':
                                type = &FloatType;
                                break;
                        case '*':
                                match = true;
                                break;

                                /*
                                 * {[( require complements, just because
                                 * "<{}>{I}" is easier to parse with the
                                 * eyes than "<{>{I}"
                                 */
                        case '{':
                                type = &DictType;
                                bug_on(*s != '}');
                                s++;
                                break;
                        case '(':
                                type = &TupleType;
                                bug_on(*s != ')');
                                s++;
                                break;
                        case '[':
                                type = &ArrayType;
                                bug_on(*s != ']');
                                s++;
                                break;

                        case 'b':
                                type = &BytesType;
                                break;
                        case 'z':
                                type = &ComplexType;
                                break;
                        case '/':
                                type = &FileType;
                                break;
                        case 'x':
                                type = &FunctionType;
                                break;
                        case 'r':
                                type = &RangeType;
                                break;
                        default:
                                bug();
                        }
                        if (type && uarg->v_type == type)
                                match = true;
                }

                if (!match) {
                        char argnobuf[100];
                        sprintf(argnobuf, " %d", argno + 1);
                        err_setstr(TypeError, "%s%sargument%s invalid type %s",
                                   fname ? fname : "", fname ? "() " : "",
                                   argno >= 0 ? argnobuf : "");
                        return RES_ERROR;
                }
                *ppo = uarg;
                return RES_OK;

        }

        if (strchr("({[", typec) != NULL) {
                /* In ASCII, ')' = '(' + 1, the others are +2 */
                char endchr = c + 1;
                if (typec != '(')
                        endchr++;

                /*
                 * To get one of these rather than one of their children,
                 * use <{}>, <[]>, <()>, NOT {}, [], ().
                 */
                bug_on(*s == endchr);

                if (uarg)
                        flags |= GAF_MANDO;
                *fmt = s;
                return get_array_args(uarg, fmt, ap, flags, fname, endchr, argno);
        }

        /* Every item in ap is a pointer of some sort */
        pv = va_arg(ap, void *);
        if (!uarg)
                return RES_OK;

        switch (typec) {
        case 'b':
                /* Integer that can fit into a byte */
                if (isvar_bytes(uarg)) {
                        int ival = 0;
                        if (seqvar_size(uarg) == 1) {
                                ival = bytes_get_data(uarg)[0];
                        } else if (seqvar_size(uarg) != 0) {
                                vmerr_generic("expected value from -128...255",
                                              argno, fname);
                                return RES_ERROR;
                        }
                        *((unsigned char *)pv) = ival & 0xffu;
                } else if (isvar_int(uarg)) {
                        long long ival = intvar_toll(uarg);
                        if (ival < -128 || ival > 255) {
                                vmerr_generic("expected value from -128...255",
                                              argno, fname);
                                return RES_ERROR;
                        }
                        *((unsigned char *)pv) = ival & 0xffu;
                } else {
                        vmerr_type_mismatch(argno, fname, uarg, &IntType);
                        return RES_ERROR;
                }
                break;

        case 'h':
            {
                long long ival;
                if (!isvar_int(uarg)) {
                        vmerr_type_mismatch(argno, fname, uarg, &IntType);
                        return RES_ERROR;
                }
                ival = intvar_toll(uarg);
                if (ival < -32768 || ival > 65535) {
                        vmerr_generic("expected value from -32768...65535",
                                      argno, fname);
                        return RES_ERROR;
                }
                *((short *)pv) = (int)ival & 0xffffu;
                break;
            }

        case 'i':
            {
                /* integer */
                long long ival;
                if (!isvar_int(uarg)) {
                        vmerr_type_mismatch(argno, fname, uarg, &IntType);
                        return RES_ERROR;
                }
                ival = intvar_toll(uarg);
                if (ival < INT_MIN || ival > UINT_MAX) {
                        vmerr_generic("value must fit within a C int",
                                      argno, fname);
                        return RES_ERROR;
                }
                *((int *)pv) = (int)ival;
                break;
            }

        case 'l':
                if (!isvar_int(uarg)) {
                        vmerr_type_mismatch(argno, fname, uarg, &IntType);
                        return RES_ERROR;
                }
                *((long long *)pv) = intvar_toll(uarg);
                break;

        case 's':
                if (!isvar_string(uarg)) {
                        vmerr_type_mismatch(argno, fname, uarg, &StringType);
                        return RES_ERROR;
                }
                *((const char **)pv) = string_cstring(uarg);
                break;
        case 'c':
                if (!isvar_string(uarg)) {
                        vmerr_type_mismatch(argno, fname, uarg, &StringType);
                        return RES_ERROR;
                }
                if (seqvar_size(uarg) != 1) {
                        vmerr_generic("string must have a size of 1",
                                      argno, fname);
                        return RES_ERROR;
                }
                *((long *)pv) = string_ord(uarg, 0);
                break;

        case 'f':
                if (isvar_float(uarg)) {
                        *((double *)pv) = floatvar_tod(uarg);
                } else if (isvar_int(uarg)) {
                        *((double *)pv) = (double)intvar_toll(uarg);
                } else {
                        vmerr_type_mismatch(argno, fname, uarg, &FloatType);
                        return RES_ERROR;
                }
                break;
        /*
         * TODO: 'z' -> complex numbers
         */
        default:
                bug();
        }
        return RES_OK;
}

static enum result_t
vm_getargs_1(Object **items, size_t n, const char **fmt, va_list ap,
             unsigned int flags, char endchr, const char *fname, int argno)
{
        size_t i;
        const char *s = *fmt;

        for (i = 0; ; i++) {
                int c = *s++;

                /* If top level, keep argno up to date */
                if (endchr == ':')
                        argno = i;

                if (c == '\0') {
                        bug_on(endchr != ':');
                        --s;
                        break;
                }
                if (c == endchr)
                        break;

                switch (c) {
                case '.':
                        continue;
                case '|':
                        bug_on(!(flags & GAF_MANDO));
                        flags &= ~GAF_MANDO;
                        break;
                default:
                    {
                        Object *uarg;
                        if (items) {
                                uarg = i >= n ? NULL : items[i];
                                if (!uarg) {
                                        if (!!(flags & GAF_MANDO)) {
                                                vmerr_generic("missing",
                                                              argno, fname);
                                                return RES_ERROR;
                                        }
                                }
                        } else {
                                uarg = NULL;
                        }

                        /*
                         * unlike with get_dict_args(), we're borrowing
                         * refs, not producing them, so do not consume
                         * them here.
                         */
                        if (convert_arg(c, uarg, &s, ap, flags, fname, argno)
                            == RES_ERROR) {
                                return RES_ERROR;
                        }
                        break;
                    }
                }
        }
        *fmt = s;
        return RES_OK;
}

static enum result_t
vm_vgetargs(Frame *fr, const char *fmt, va_list ap, unsigned int flags)
{
        const char *fname;

        /* Check from end; earlier ':'s may exist in case of dict items */
        fname = strrchr(fmt, ':');
        if (fname)
                fname++;

        return vm_getargs_1(fr->stack, fr->ap, &fmt, ap,
                            flags | GAF_MANDO, ':', fname, 0);
}

/**
 * vm_getargs - Unpack arguments to (usually) C-ified variables.
 * @fr:  Stack frame used by the calling function
 * @fmt: Format string describing the arguments to unpack.
 *       see big comment atop vm_getargs.c describing this string.
 *
 * Return: RES_OK or RES_ERROR
 */
enum result_t
vm_getargs(Frame *fr, const char *fmt, ...)
{
        va_list ap;
        enum result_t res;

        va_start(ap, fmt);
        res = vm_vgetargs(fr, fmt, ap, 0);
        va_end(ap);

        return res;
}

/**
 * vm_getargs_sv - Like vm_getargs, but instead of using a stack frame,
 *                 use a dictionary, a list, or a tuple
 * @sv:  A dictionary, a list, or a tuple
 * @fmt: A format string whose first and final characters describes what
 *       type @sv is: "(...)" for tuple, etc.
 *
 * Return: RES_OK or RES_ERROR
 */
enum result_t
vm_getargs_sv(Object *sv, const char *fmt, ...)
{
        va_list ap;
        enum result_t res;
        int c;
        char endchr;
        const char *fname;

        fname = strrchr(fmt, ':');
        if (fname)
                fname++;

        va_start(ap, fmt);

        c = *fmt++;
        bug_on(!strchr("({[", c) || c == '\0');
        endchr = c + 1;
        if (c != '(')
                endchr++;

        res = get_array_args(sv, &fmt, ap, GAF_MANDO, fname, endchr, -1);

        va_end(ap);
        return res;
}

