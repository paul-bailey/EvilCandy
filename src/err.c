#include <evilcandy.h>
#include <token.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define CSI "\033["
#define COLOR_RED CSI "31m"
#define COLOR_GRN CSI "32m"
#define COLOR_YEL CSI "33m"
#define COLOR_DEF CSI "39m"

/*
 * TODO: set some file-scope variables to COLOR(...) or "",
 * depending on whether stderr is a TTY or not.
 */
#define COLOR(what, str)      COLOR_##what str COLOR_DEF

static Object *exception_last = NULL;

#define exception_validate(X) tuple_validate(X, "*s", 0)

/* this consume references for args if they are Objects */
static Object *
mkexception(Object *exc_arg, Object *msg_arg)
{
        Object *args[2] = { exc_arg, msg_arg };
        return tuplevar_from_stack(args, 2, false);
}

static void
replace_exception(Object *newexc)
{
        if (exception_last)
                VAR_DECR_REF(exception_last);

        if (newexc)
                VAR_INCR_REF(newexc);

        exception_last = newexc;
}

/* helper to bug__ and breakpoint__ */
static void
trap(const char *what, const char *file, int line)
{
        fprintf(stderr, "%s trapped in %s line %d\n",
                what, file, line);
        _Exit(EXIT_FAILURE);
}

void
bug__(const char *file, int line)
{
        trap(COLOR(RED, "BUG"), file, line);
}

void
breakpoint__(const char *file, int line)
{
        trap(COLOR(GRN, "BREAKPOINT"), file, line);
}

/**
 * fail - Print an error message to stderr and die
 *
 * Not for user errors.  This is for fatal system errors
 * like being out of memory.
 * @msg: Formatted message to print before dying
 */
void
fail(const char *msg, ...)
{
        va_list ap;

        /* FIXME: if isatty(fileno(stderr))... */
        fprintf(stderr,
                "[EvilCandy] System " COLOR(RED, "ERROR") ": ");

        va_start(ap, msg);
        vfprintf(stderr, msg, ap);
        va_end(ap);

        if (errno) {
                fprintf(stderr, " (%s)\n", strerror(errno));
        } else {
                fputc('\n', stderr);
        }

        _Exit(EXIT_FAILURE);
}

/* returns updated @msg pointer */
static const char *
errmsg_format_arg(struct string_writer_t *wr, const char *msg, va_list ap)
{
        /*
         * Error messages aren't supposed to be cute and fancy.
         * They should just have %d %s, not "%+ 8.20zd" or whatever.
         * It's fancy enough that we deal with string-object values.
         * Nor do we yet support adding floating-point here.
         *
         * We'll skip all but the integer-width modifier.
         */
        char intsize = 'i';
        /* skip flags */
        msg += strspn(msg, "#-+ 0");
        /* skip pad width */
        while (isdigit(*msg))
                msg++;
        /* skip precision */
        if (*msg == '.')
                msg++;
        while (isdigit(*msg))
                msg++;
        /* save int width */
        if (*msg == 'h') {
                while (*msg == 'h')
                        msg++;
        } else if (*msg == 'l') {
                msg++;
                if (*msg == 'l') {
                        msg++;
                        intsize = 'L';
                } else {
                        intsize = 'l';
                }
        }

        bug_on(!strchr("diouxXscN", *msg) || *msg == '\0');
        switch (*msg) {
        default:
                bug();
        case 'd': case 'i': case 'u':
        case 'o': case 'x': case 'X':
        case 'p':
            {
                /* max size needs to fit a 64-bit number in octal
                 * (22 characters), plus a nulchar terminator.
                 * and a few additional bytes to round it up to be
                 * safe.
                 */
                char buf[64];
                switch (*msg) {
                default:
                case 'i':
                case 'd':
                        if (intsize == 'L')
                                sprintf(buf, "%lld", va_arg(ap, long long));
                        else if (intsize == 'l')
                                sprintf(buf, "%ld", va_arg(ap, long));
                        else
                                sprintf(buf, "%d", va_arg(ap, int));
                        break;
                case 'o':
                        if (intsize == 'L')
                                sprintf(buf, "%llo", va_arg(ap, long long));
                        else if (intsize == 'l')
                                sprintf(buf, "%lo", va_arg(ap, long));
                        else
                                sprintf(buf, "%o", va_arg(ap, int));
                        break;
                case 'u':
                        if (intsize == 'L')
                                sprintf(buf, "%llu", va_arg(ap, unsigned long long));
                        else if (intsize == 'l')
                                sprintf(buf, "%lu", va_arg(ap, unsigned long));
                        else
                                sprintf(buf, "%u", va_arg(ap, unsigned int));
                        break;
                case 'x':
                case 'X':
                        if (intsize == 'L')
                                sprintf(buf, "%llx", va_arg(ap, unsigned long long));
                        else if (intsize == 'l')
                                sprintf(buf, "%lx", va_arg(ap, unsigned long));
                        else
                                sprintf(buf, "%x", va_arg(ap, unsigned int));
                        if (*msg == 'X') {
                                char *ts = buf;
                                while (*ts) {
                                        *ts = toupper(*ts);
                                        ts++;
                                }
                        }
                        break;
                case 'p':
                        sprintf(buf, "%p", va_arg(ap, void *));
                        break;
                }
                string_writer_appends(wr, buf);
                break;
            }
        case 'c':
            {
                int c = va_arg(ap, int);
                c &= 0xff;
                if (c > 127 || (c != ' ' && !isgraph(c)))
                        c = '?';
                string_writer_append(wr, c);
                break;
            }
        case 's':
            {
                /* To hell with qualifiers, just print it */
                char *s = va_arg(ap, char *);
                string_writer_appends(wr, s);
                break;
            }
        case 'N':
            {
                /*
                 * The reason we aren't a wrapper to regular vsprintf()
                 * for the whole thing... Get a C-string from an object.
                 * Do not use simple string_cstring(), it may contain
                 * unprintable characters or an embedded nulchar.  Instead
                 * escape it with var_str(), then get cstring.
                 */
                Object *obj, *str;
                const char *cstr;

                obj = va_arg(ap, Object *);
                bug_on(!isvar_string(obj));
                str = var_str(obj);
                cstr = string_cstring(str);
                while (*cstr) {
                        string_writer_append(wr, *cstr);
                        cstr++;
                }
                VAR_DECR_REF(str);
                break;
            }
        }

        return msg + 1;
}

static Object *
errmsg_from_format(const char *msg, va_list ap)
{
        struct string_writer_t wr;
        string_writer_init(&wr, 1);
        while (*msg) {
                if (*msg == '%') {
                        msg++;
                        /* first try some easy ones */
                        if (*msg == '%') {
                                string_writer_append(&wr, '%');
                                msg++;
                        } else {
                                msg = errmsg_format_arg(&wr, msg, ap);
                        }
                } else {
                        string_writer_append(&wr, *msg);
                        msg++;
                }
        }
        return stringvar_from_writer(&wr);
}

static void
err_vsetstr(Object *exc, const char *msg, va_list ap)
{
        Object *new_exc, *msgstr;

        msgstr = errmsg_from_format(msg, ap);

        /*
         * @exc is not from the user stack.  It's a meant-to-be-immortal
         * 'XxxxError' exception value, provided by internal code.  For
         * convenience, the caller did not produce a reference.  So we
         * produce a reference here to keep it from getting destroyed when
         * err_get consumes it next.
         */
        VAR_INCR_REF(exc);

        new_exc = mkexception(exc, msgstr);
        replace_exception(new_exc);
        VAR_DECR_REF(msgstr);
        VAR_DECR_REF(new_exc);
}

/**
 * err_setstr - Set an exception by value and string
 * @exc: Value, a XxxxError object, like RuntimeError
 * @msg: Formatted text to write to the exception message
 *       Do not get fancy, this has very limited printf-like support.
 *       Floating-point format % + gGfF is unsupported.
 *       A special format, %N, takes an Object *arg, which is converted
 *       into a printable string.  You should only use it for strings
 *       and numbers.
 */
void
err_setstr(Object *exc, const char *msg, ...)
{
        va_list ap;
        /*
         * XXX REVISIT: @exc could be NULL if a function is called and
         * encounters an error during early initialization, before the
         * XxxError pointers have been set.  But fatal system errors do
         * not call err_setstr, and users can't cause an error so early,
         * only bad C code can.  So it's a bug, right?
         */
        bug_on(exc == NULL);
        va_start(ap, msg);
        err_vsetstr(exc, msg, ap);
        va_end(ap);
}

/*
 * err_set_from_user - Like err_setstr, except pass a tuple
 * @exc: Exception to set.  (The reference for this will be consumed.)
 *       This must be either a tuple containing two printable objects
 *       ('value' and 'message', in that order), an integer (which will
 *       be regarded as the value), or a string (the message, in which
 *       RuntimeError is assumed to be the value).
 */
void
err_set_from_user(Object *exc)
{
        if (!isvar_tuple(exc)) {
                Object *tmp;
                tmp = mkexception(exc, STRCONST_ID(nomsg));
                /*
                 * Do not consume @exc's reference here.
                 * The mkexception calls above did that for us.
                 */
                exc = tmp;
        } else if (exception_validate(exc) != RES_OK) {
                goto invalid;
        }

        replace_exception(exc);
        VAR_DECR_REF(exc);
        return;

invalid:
        err_setstr(TypeError, "Throwing invalid exception");
        VAR_DECR_REF(exc);
}

/*
 * Calling code is responsible for consuming reference on return value
 * when their done with it.
 */
Object *
err_get(void)
{
        Object *ret = exception_last;
        VAR_INCR_REF(exception_last);
        replace_exception(NULL);
        return ret;
}

void
err_print(FILE *fp, Object *exc)
{
        const char *errval, *errmsg;
        bool tty;
        Object *v, *msg;

        if (!exc)
                return;

        bug_on(exception_validate(exc) != RES_OK);

        v = var_str_swap(tuple_getitem(exc, 0));
        errval = string_cstring(v);

        msg = var_str_swap(tuple_getitem(exc, 1));
        errmsg = string_cstring(msg);

        tty = !!isatty(fileno(fp));

        fprintf(fp, "[EvilCandy] %s%s%s %s\n",
                tty ? COLOR_RED : "", errval,
                tty ? COLOR_DEF : "", errmsg);

        VAR_DECR_REF(v);
        VAR_DECR_REF(msg);
}

/* get last error, print it, then clear it */
void
err_print_last(FILE *fp)
{
        Object *exc;

        if(!err_occurred())
                return;

        exc = err_get();
        err_print(fp, exc);
        VAR_DECR_REF(exc);
}

bool
err_occurred(void)
{
        return exception_last != NULL;
}

void
err_clear(void)
{
        replace_exception(NULL);
}

