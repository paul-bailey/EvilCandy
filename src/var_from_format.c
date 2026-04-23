/*
 * var_from_format.c - An idea half cribbed from Cpython and half cribbed
 *                     from Kernighan and Pike's pack/unpack functions in
 *                     _The Practice of Programming_, p. 218-219.
 *                     This is my own non-derived implementation, so no
 *                     special license is in order.
 */
#include <evilcandy/debug.h>
#include <evilcandy/types/array.h>
#include <evilcandy/types/function.h>
#include <evilcandy/types/dict.h>
#include <evilcandy/types/string.h>
#include <evilcandy/types/number_types.h>
#include <evilcandy/types/tuple.h>
#include <internal/types/sequential_types.h>
#include <internal/type_registry.h>

static int
count_items(const char *s, int endchar)
{
        int count = 0;
        int depth = 0;
        while (*s != '\0' && (depth > 0 || *s != endchar)) {
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
                default:
                        if (!depth)
                                count++;
                };
                s++;
        }
        bug_on(depth != 0);

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
        if (count > 0) {
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
        }
        bug_on(*fmt != '}');
        *endptr = (char *)fmt + 1;
        return dict;
}

static Object *
var_make_tuple(const char *fmt, va_list ap, char **endptr)
{
        int i, count = count_items(fmt, ')');
        Object *tuple = tuplevar_new(count);
        if (count > 0) {
                Object **data = tuple_get_data(tuple);
                for (i = 0; i < count; i++) {
                        data[i] = var_vmake(fmt, ap, endptr);
                        fmt = *endptr;
                }
        }

        bug_on(*fmt != ')');
        *endptr = (char *)fmt + 1;
        return tuple;
}

static Object *
var_make_array(const char *fmt, va_list ap, char **endptr)
{
        int i, count = count_items(fmt, ']');
        Object *array = arrayvar_new(count);
        for (i = 0; i < count; i++) {
                Object *item = var_vmake(fmt, ap, endptr);
                fmt = *endptr;
                array_setitem(array, i, item);
                VAR_DECR_REF(item);
        }

        bug_on(*fmt != ']');
        *endptr = (char *)fmt + 1;
        return array;
}


static Object *
var_make_builtin(const char *fmt, va_list ap, char **endptr)
{
        Object *func;
        Object *(*cb)(Frame *) = NULL;
        int bind = 0;

        /*
         * Expect <x> or <xb>.  x if for the function handle, b is an
         * integer, true to bind on de-reference, false to not bind.
         * If b is not supplied, false is assumed.
         */
        while (*fmt != '>') {
                switch (*fmt) {
                case 'x':
                        bug_on(!!cb);
                        cb = va_arg(ap, Object *(*)(Frame *));
                        break;
                case 'b':
                        bind = va_arg(ap, int);
                        break;
                default:
                        bug();
                }
                fmt++;
        }

        bug_on(!cb);

        func = funcvar_new_intl(cb, bind);

        bug_on(*fmt != '>');
        *endptr = (char *)fmt+1;
        return func;
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

