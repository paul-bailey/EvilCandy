/* builtin/io.c - Implementation of the __gbl__.Io built-in object */
#include <evilcandy.h>

/*
 * Io.open(name, mode)          name and mode are strings
 *
 * Return: a FileType handle
 */
static Object *
do_open(Frame *fr)
{
        const char *name, *mode, *ps;
        unsigned int modeflags;
        Object *vname;
        FILE *fp;

        if (vm_getargs(fr, "<s>s", &vname, &mode) == RES_ERROR)
                return ErrorVar;
        name = string_cstring(vname);

        modeflags = 0;
        for (ps = mode; *ps != '\0'; ps++) {
                switch (*ps) {
                case 'r':
                        modeflags |= FMODE_READ;
                        continue;
                case 'w':
                        modeflags |= FMODE_WRITE;
                        continue;
                case 'b':
                        modeflags |= FMODE_BINARY;
                        continue;
                default:
                        err_setstr(ValueError, "Invalid mode '%c'", *ps);
                        return ErrorVar;
                }
        }

        if (!(modeflags & (FMODE_READ | FMODE_WRITE))) {
                err_setstr(ValueError, "Mode must have at least 'r' or 'w' set");
                return ErrorVar;
        }

        bug_on(!name || !mode);
        bug_on(name[0] == '\0' || mode[0] == '\0');
        fp = fopen(name, mode);
        if (!fp) {
                err_errno("open failed");
                return ErrorVar;
        }

        /* filevar_new will produce ref for name */
        return filevar_new(fp, vname, modeflags);
}

static const struct type_inittbl_t io_inittbl[] = {
        V_INITTBL("open", do_open, 2, 2, -1, -1),
        TBLEND,
};

static Object *
create_io_instance(Frame *fr)
{
        return dictvar_from_methods(NULL, io_inittbl);
}

void
moduleinit_io(void)
{
        Object *k = stringvar_new("_io");
        Object *o = var_from_format("<xmM>",
                                    create_io_instance, 0, 0);
        dict_setitem(GlobalObject, k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);
}


