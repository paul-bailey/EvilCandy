/*
 * builtin/io.c - Implementation of the __gbl__.Io built-in object
 *
 * f = Io.open(name, mode)
 * If fail,
 *      return a string describing the failure
 *
 * If success,
 *      return a file handle, an object with the following
 *      methods:
 *
 * f.readline()    Read a line from f to the next '\n' and return
 *                 it as a string, or as "" if f is at EOF.
 * f.writeline(txt)
 *                 Write txt to f.  Do not interpolate characters
 *                 or add a newline at the end.
 * f.eof()         Return 1 if f is at EOF, 0 if not
 * f.clearerr()    Clear error flags in f
 * f.errno()       Get the last error number pertaining to f
 * f.tell()        Return the current offset into f
 * f.rewind()      Return to the start of the file
 *
 * TODO: Binary file operations, need better array class than list.
 */
#include "builtin.h"


/*
 * Io.open(name, mode)          name and mode are strings
 *
 * Return:
 *      string          if error (errno descr. stored in string)
 *      object          if success
 *
 * Check with typeof to determine success or failure
 */
static Object *
do_open(Frame *fr)
{
        char *name, *mode, *ps;
        unsigned int modeflags;
        FILE *fp;
        Object *vname = frame_get_arg(fr, 0);
        Object *vmode = frame_get_arg(fr, 1);

        if (arg_type_check(vname, &StringType) != 0)
                return ErrorVar;
        if (arg_type_check(vmode, &StringType) != 0)
                return ErrorVar;
        name = string_get_cstring(vname);
        mode = string_get_cstring(vmode);
        if (name == NULL || mode == NULL) {
                err_setstr(ArgumentError, "Expected: NAME MODE");
                return ErrorVar;
        }

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

const struct inittbl_t bi_io_inittbl__[] = {
        TOFTBL("open", do_open, 2, 2),
        TBLEND,
};

