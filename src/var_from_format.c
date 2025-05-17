/*
 * var_from_format.c - An idea half cribbed from Cpython and half cribbed
 *                     from Kernighan and Pike's pack/unpack functions in
 *                     _The Practice of Programming_, p. 218-219.
 *                     This is my own non-derived implementation, so no
 *                     special license is in order.
 */
#include <evilcandy.h>

static int
count_items(const char *s, int endchar)
{
        int count = 0;
        int depth = 0;
        while (*s != '\0' && *s != endchar) {
                switch (*s) {
                case '<':
                case '(':
                case '{':
                case '[':
                        if (!depth)
                                count++;
                        depth++;
                        break;
                case '>':
                case ')':
                case '}':
                case ']':
                        bug_on(!depth);
                        depth--;
                        break;
                case '/':
                        s++;
                        while (*s != '/' && *s != '\0')
                                s++;
                        bug_on(*s != '/');
                        /* fall through */
                default:
                        if (!depth)
                                count++;
                };
                s++;
        }

        bug_on(*s != endchar);
        return count;
}

/* forward-declaration because recursion is needed */
static Object *var_vmake(const char *fmt, va_list ap, char **endptr);

static Object *
var_make_dict(const char *fmt, va_list ap, char **endptr)
{
        Object *dict = dictvar_new();
        int count = count_items(fmt, '}');
        if (count == 0) {
                fmt++;
                goto done;
        }
        bug_on(!!(count & 1));

        for (; count > 0; count -= 2) {
                Object *k, *v;
                enum result_t res;
                k = var_vmake(fmt, ap, endptr);
                fmt = *endptr;
                v = var_vmake(fmt, ap, endptr);
                fmt = *endptr;
                bug_on(!isvar_string(k));
                res = dict_setitem(dict, k, v);
                bug_on(res != RES_OK);
                (void)res;
                VAR_DECR_REF(v);
                VAR_DECR_REF(k);
        }
done:
        bug_on(*fmt != '}');
        *endptr = (char *)fmt + 1;
        return dict;
}

static Object *
var_make_tuple(const char *fmt, va_list ap, char **endptr)
{
        int i, count = count_items(fmt, ')');
        Object *tuple = tuplevar_new(count);
        if (count == 0) {
                fmt++;
                goto done;
        }

        Object **data = tuple_get_data(tuple);
        for (i = 0; i < count; i++) {
                data[i] = var_vmake(fmt, ap, endptr);
                fmt = *endptr;
        }

done:
        bug_on(*fmt != ')');
        *endptr = (char *)fmt + 1;
        return tuple;
}

static Object *
var_make_array(const char *fmt, va_list ap, char **endptr)
{
        int i, count = count_items(fmt, ']');
        Object *array = arrayvar_new(count);
        if (count == 0) {
                fmt++;
                goto done;
        }

        for (i = 0; i < count; i++) {
                Object *item = var_vmake(fmt, ap, endptr);
                fmt = *endptr;
                array_setitem(array, i, item);
                VAR_DECR_REF(item);
        }

done:
        bug_on(*fmt != ']');
        *endptr = (char *)fmt + 1;
        return array;
}


static Object *
var_make_builtin(const char *fmt, va_list ap, char **endptr)
{
        Object *func;
        Object *(*cb)(Frame *) = NULL;
        int min = -1;
        int max = -1;
        int opt = -1;
        int kw  = -1;

        /*
         * Expect something like <xmMok>.  x is not optional.  If any of
         * mMok are not supplied, defaults will be used in their place.
         */
        while (*fmt != '>') {
                switch (*fmt) {
                case 'x':
                        bug_on(!!cb);
                        cb = va_arg(ap, Object *(*)(Frame *));
                        break;
                case 'm':
                        bug_on(min >= 0);
                        min = va_arg(ap, int);
                        break;
                case 'M':
                        bug_on(max >= 0);
                        max = va_arg(ap, int);
                        break;
                case 'o':
                        bug_on(opt >= 0);
                        opt = va_arg(ap, int);
                        break;
                case 'k':
                        bug_on(kw >= 0);
                        kw = va_arg(ap, int);
                        break;
                default:
                        bug();
                }
                fmt++;
        }

        bug_on(!cb);

        if (min < 0)
                min = 0;
        func = funcvar_new_intl(cb, min, max);
        if (kw >= 0)
                function_setattr(func, IARG_FUNC_KWIND, kw);
        if (opt >= 0)
                function_setattr(func, IARG_FUNC_OPTIND, opt);

        bug_on(*fmt != '>');
        *endptr = (char *)fmt+1;
        return func;
}

static Object *
var_make_file(const char *fmt, va_list ap, char **endptr)
{
        Object *res;
        Object *name = NULL;
        unsigned int mode = (unsigned int)(-1);
        FILE *fp = NULL;

        /*
         * Expect exactly 5 chars, something like "/nsmif/"
         * n is filename, m is mode, f is file pointer
         * n must be followed by what will build as a string.
         * m must be followed by what will build as an integer.
         */
        while (*fmt != '/') {
                switch (*fmt) {
                case 'f':
                        bug_on(!!fp);
                        fp = va_arg(ap, FILE *);
                        fmt++;
                        continue;
                case 'n':
                        bug_on(!!name);
                        name = var_vmake(fmt + 1, ap, endptr);
                        bug_on(!isvar_string(name));
                        fmt = *endptr;
                        continue;
                case 'm':
                    {
                        Object *tmode;
                        bug_on((int)mode >= 0);
                        tmode = var_vmake(fmt + 1, ap, endptr);
                        bug_on(!isvar_int(tmode));
                        mode = intvar_toi(tmode);
                        VAR_DECR_REF(tmode);
                        fmt = *endptr;
                        continue;
                    }
                default:
                        bug();
                }
        }
        bug_on((int)mode < 0);
        bug_on(name == NULL);
        bug_on(fp == NULL);

        res = filevar_new(fp, name, mode);
        VAR_DECR_REF(name);
        bug_on(res == ErrorVar);
        bug_on(*fmt != '/');
        *endptr = (char *)fmt+1;
        return res;
}

static Object *
var_vmake(const char *fmt, va_list ap, char **endptr)
{
        Object *o;
        switch (*fmt++) {
        case '{':
                return var_make_dict(fmt, ap, endptr);
        case '[':
                return var_make_array(fmt, ap, endptr);
        case '(':
                return var_make_tuple(fmt, ap, endptr);
        case '<':
                return var_make_builtin(fmt, ap, endptr);
        case '/':
                return var_make_file(fmt, ap, endptr);
        case 'O':
            {
                o = va_arg(ap, Object *);
                VAR_INCR_REF(o);
                break;
            }
        case 's':
            {
                const char *s = va_arg(ap, const char *);
                o = stringvar_new(s);
                break;
            }
        case 'S':
            {
                char *s = va_arg(ap, char *);
                o = stringvar_nocopy(s);
                break;
            }
        case 'B':
            {
                struct buffer_t *b = va_arg(ap, struct buffer_t *);
                o = stringvar_from_buffer(b);
                break;
            }
        case 'i':
            {
                int ival = va_arg(ap, int);
                o = intvar_new(ival);
                break;
            }
        case 'l':
            {
                long ival = va_arg(ap, long);
                o = intvar_new(ival);
                break;
            }
        case 'L':
            {
                long long ival = va_arg(ap, long long);
                o = intvar_new(ival);
                break;
            }
        case 'd':
            {
                double d = va_arg(ap, double);
                o = floatvar_new(d);
                break;
            }
        default:
                bug();
                o = NULL;
        }
        *endptr = (char *)fmt;
        return o;
}

Object *
var_from_format(const char *fmt, ...)
{
        Object *res;
        va_list ap;
        char *endptr;
        bug_on(count_items(fmt, '\0') != 1);

        va_start(ap, fmt);
        res = var_vmake(fmt, ap, &endptr);
        va_end(ap);
        return res;
}

