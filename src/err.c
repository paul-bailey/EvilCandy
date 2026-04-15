#include <evilcandy/string_writer.h>
#include <evilcandy/vm.h>
#include <evilcandy/global.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/err.h>
#include <evilcandy/types/array.h>
#include <evilcandy/types/class.h>
#include <evilcandy/types/string.h>
#include <internal/err.h>
#include <internal/token.h>
#include <internal/types/string.h>
#include <lib/helpers.h>

#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>

/* FIXME: Replace this with gbl accessor functions */
#include <internal/global.h>

#define CSI "\033["
#define COLOR_RED CSI "31m"
#define COLOR_GRN CSI "32m"
#define COLOR_YEL CSI "33m"
#define COLOR_DEF CSI "39m"

/* err runtime state */
struct gbl_err_subsys_t {
        Object *exception_last;
};

/*
 * TODO: set some file-scope variables to COLOR(...) or "",
 * depending on whether stderr is a TTY or not.
 */
#define COLOR(what, str)      COLOR_##what str COLOR_DEF

#define exception_instance_validate(exc) \
        (isvar_instance(exc) && instance_instanceof(exc, ErrorVar))
#define exception_class_validate(exc) \
        (isvar_class(exc) && class_issubclass(exc, ErrorVar))

/* this consume references for args if they are Objects */
static Object *
mkexception(Object *exc_class, Object *msg)
{
        Object *stack[1] = { msg };
        Object *args = arrayvar_from_stack(stack, 1, false);
        Object *exc_instance = vm_exec_func(NULL, exc_class, args, NULL);
        VAR_DECR_REF(args);
        return exc_instance;
}

static void
err_clear_subsys(struct gbl_err_subsys_t *err)
{
        if (err->exception_last)
                VAR_DECR_REF(err->exception_last);
        err->exception_last = NULL;
}

static void
replace_exception(Object *newexc)
{
        struct gbl_err_subsys_t *err = gbl_get_err_subsys();
        err_clear_subsys(err);
        err->exception_last = VAR_NEW_REF(newexc);
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
                return NULL;
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
                                evc_sprintf(buf, sizeof(buf), "%lld", va_arg(ap, long long));
                        else if (intsize == 'l')
                                evc_sprintf(buf, sizeof(buf), "%ld", va_arg(ap, long));
                        else
                                evc_sprintf(buf, sizeof(buf), "%d", va_arg(ap, int));
                        break;
                case 'o':
                        if (intsize == 'L')
                                evc_sprintf(buf, sizeof(buf), "%llo", va_arg(ap, long long));
                        else if (intsize == 'l')
                                evc_sprintf(buf, sizeof(buf), "%lo", va_arg(ap, long));
                        else
                                evc_sprintf(buf, sizeof(buf), "%o", va_arg(ap, int));
                        break;
                case 'u':
                        if (intsize == 'L')
                                evc_sprintf(buf, sizeof(buf), "%llu", va_arg(ap, unsigned long long));
                        else if (intsize == 'l')
                                evc_sprintf(buf, sizeof(buf), "%lu", va_arg(ap, unsigned long));
                        else
                                evc_sprintf(buf, sizeof(buf), "%u", va_arg(ap, unsigned int));
                        break;
                case 'x':
                case 'X':
                        if (intsize == 'L')
                                evc_sprintf(buf, sizeof(buf), "%llx", va_arg(ap, unsigned long long));
                        else if (intsize == 'l')
                                evc_sprintf(buf, sizeof(buf), "%lx", va_arg(ap, unsigned long));
                        else
                                evc_sprintf(buf, sizeof(buf), "%x", va_arg(ap, unsigned int));
                        if (*msg == 'X') {
                                char *ts = buf;
                                while (*ts) {
                                        *ts = toupper(*ts);
                                        ts++;
                                }
                        }
                        break;
                case 'p':
                        evc_sprintf(buf, sizeof(buf), "%p", va_arg(ap, void *));
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

        new_exc = mkexception(exc, msgstr);
        replace_exception(new_exc);
        VAR_DECR_REF(msgstr);
        VAR_DECR_REF(new_exc);
        /*
         * Do not consume @exc.  It does not come from the user stack.
         * It's a meant-to-be-immortal 'XxxxError' exception value,
         * provided by internal code.  For convenience, the caller did
         * not produce a reference.  So we produce a reference here to
         * keep it from getting destroyed when err_get consumes it next.
         *
         * (Compare w/ err_set_from_user, which does consume @exc).
         */
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
         * only bad C code can.
         */
        bug_on(exc == NULL);
        bug_on(!exception_class_validate(exc));
        va_start(ap, msg);
        err_vsetstr(exc, msg, ap);
        va_end(ap);
}

/*
 * err_set_from_user - Like err_setstr, but from user.
 * @exc: Exception to set.
 */
void
err_set_from_user(Object *exc)
{
        if (!exception_instance_validate(exc)) {
                err_setstr(TypeError, "Throwing invalid exception");
                return;
        }

        replace_exception(exc);
}

/*
 * Calling code is responsible for consuming reference on return value
 * when their done with it.
 */
Object *
err_get(void)
{
        struct gbl_err_subsys_t *err = gbl_get_err_subsys();
        Object *ret = err->exception_last;
        VAR_INCR_REF(err->exception_last);
        err_clear_subsys(err);
        return ret;
}

static void
err_print(FILE *fp, Object *exc)
{
        bool tty;
        Object *exception_name, *exception_class, *message;
        Object *message_key;
        const char *msg;

        if (!exc)
                return;

        bug_on(!exception_instance_validate(exc));

        exception_class = instance_get_class(exc);
        bug_on(!exception_class || !isvar_class(exception_class));

        exception_name = class_get_name(exception_class);
        bug_on(!isvar_string(exception_name));

        message_key = stringvar_new("message");
        message = instance_getattr(NULL, exc, message_key);
        if (!message || !isvar_string(message))
                msg = "malformed exception";
        else
                msg = string_cstring(message);

        tty = !!isatty(fileno(fp));

        fprintf(fp, "[EvilCandy] %s%s%s %s\n",
                tty ? COLOR_RED : "", string_cstring(exception_name),
                tty ? COLOR_DEF : "", msg);

        VAR_DECR_REF(exception_class);
        VAR_DECR_REF(exception_name);
        VAR_DECR_REF(message_key);
        if (message)
                VAR_DECR_REF(message);
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
        return gbl_get_err_subsys()->exception_last != NULL;
}

void
err_clear(void)
{
        err_clear_subsys(gbl_get_err_subsys());
}

void
err_deinit_gbl(struct gbl_err_subsys_t *err)
{
        err_clear_subsys(err);
        efree(err);
}

struct gbl_err_subsys_t *
err_init_gbl(void)
{
        struct gbl_err_subsys_t *err = emalloc(sizeof(*err));
        memset(err, 0, sizeof(*err));
        err->exception_last = NULL;
        return err;
}

