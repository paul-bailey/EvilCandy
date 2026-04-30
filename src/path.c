#include <evilcandy/global.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/types/array.h>
#include <evilcandy/types/string.h>
#include <internal/path.h>
#include <internal/builtin/sys.h>
#include <internal/types/string.h>
#include <internal/global.h>
#include <lib/helpers.h>

#define SEP '/'
#define DOT '.'
#define BACKDIR "../"
#define BACKDIR_LEN 3
#define THISDIR "./"
#define THISDIR_LEN 2

/* FIXME: Need a Windows version of this */
static bool
path_is_absolute(const char *path)
{
        return path[0] == SEP;
}

/*
 * Find the non-directory file name in path name.
 * @path must be absolute
 *
 * Return: Pointer to last SEP (or first character after it,
 * if it's in the top-level directory)
 */
static char *
find_notdir(const char *path)
{
        char *res = strrchr(path, SEP);
        bug_on(!res);
        /*
         * XXX: inconsistency:
         * If path is '/a', directory is '/'.
         * If path is '/a/b', directory is '/a', not '/a/'.
         * Should we treat '/' like an empty string?
         */
        if (res == path)
                res++;
        return res;
}

static char *
pathcat(const char *requested_file, const char *refpath)
{
        if (path_is_absolute(requested_file)) {
                bug_on(refpath != NULL);
                return estrdup(requested_file);
        }

        char *newpath;
        size_t pathlen;

        bug_on(!refpath);
        bug_on(!path_is_absolute(refpath));
        pathlen = strlen(requested_file) + strlen(refpath) + 2;
        newpath = emalloc(pathlen);
        evc_sprintf(newpath, pathlen, "%s%c%s",
                    refpath, SEP, requested_file);
        return newpath;
}

/*
 * If @refpath is NULL, use absolute path
 * @script is true if @refpath is the path of the current script
 */
static FILE *
push_path_from(const char *requested_file, const char *refpath, bool script)
{
        /*
         * Using heap instead of stack for temporary path name because:
         * 1. requested_file + path lengths could add up to more than
         *    PATH_MAX if @requested_file has lots of redundancies in it.
         * 2. PATH_MAX is more bytes to push onto the C stack than I am
         *    comfortable with, since this could be called from the VM,
         *    which itself may be nested arbitrarily deep.
         * 3. This occurs during "import", which is a non-trivial step,
         *    anyway.  import should not be an iterative step if speed
         *    is a concern.
         */
        char *newpath = pathcat(requested_file, refpath);
        FILE *fp = fopen(newpath, "r");
        if (fp) {
                /* TODO: Check that @fp is not a directory. */
                Object *bc, *bcnew;
                char *notdir;

                bc = sys_getitem(STRCONST_ID(breadcrumbs));
                bug_on(!bc || bc == ErrorVar || !isvar_array(bc));
                bcnew = stringvar_new(newpath);
                array_append(bc, bcnew);
                VAR_DECR_REF(bcnew);
                VAR_DECR_REF(bc);

                notdir = find_notdir(newpath);
                *notdir = '\0';
                if (script) {
                        Object *po = sys_getitem(STRCONST_ID(import_path));
                        Object *np = stringvar_new(newpath);
                        array_setitem(po, 0, np);
                        VAR_DECR_REF(np);
                        VAR_DECR_REF(po);
                }
        }
        efree(newpath);
        return fp;
}

/* Currently always returns RES_OK */
enum result_t
path_insert(const char *path)
{
        Object *pathobj, *import_paths;
        char *fullpath;
        if (!path_is_absolute(path)) {
                Object *cwd;
                const char *cwds;

                cwd = gbl_cwd();
                bug_on(!isvar_string(cwd));
                cwds = string_cstring(cwd);
                fullpath = pathcat(path, cwds);
        } else {
                fullpath = estrdup(path);
        }

        pathobj = stringvar_new(fullpath);
        efree(fullpath);

        /*
         * keep the following order:
         *  0         '.' or directory of current import
         *  1...n-2   paths inserted with -I command-line option
         *  n-1       path determined by rcdatadir
         */
        import_paths = sys_getitem(STRCONST_ID(import_path));
        /*
         * Bug, not error.  This is early, before user could have
         * manipulated sys.import_path[].
         */
        bug_on(!import_paths || !isvar_array(import_paths));
        array_insert_chunk(import_paths, 1, &pathobj, 1);

        VAR_DECR_REF(pathobj);
        VAR_DECR_REF(import_paths);
        return RES_OK;
}

/**
 * @requested_file: Name of import file as written in "import" command.
 */
FILE *
push_path(const char *requested_file)
{

        if (path_is_absolute(requested_file)) {
                return push_path_from(requested_file, NULL, false);
        } else {
                /*
                 * Try each of the paths in "sys.import_path", which
                 * will begin with the directory of the current loaded
                 * script (or the current working directory if in
                 * interactive mode).
                 */
                Object *paths;
                size_t i, npath;
                FILE *fp;

                paths = sys_getitem(STRCONST_ID(import_path));
                bug_on(!paths || !isvar_seq(paths));
                npath = seqvar_size(paths);
                fp = NULL;
                for (i = 0; i < npath; i++) {
                        const char *trypath_s;
                        Object *trypath_o;

                        trypath_o = array_borrowitem(paths, i);
                        /* XXX: Should I warn? */
                        bug_on(!trypath_o);
                        if (!isvar_string(trypath_o))
                                continue;
                        trypath_s = string_cstring(trypath_o);
                        /*
                         * If embedded nulchars in path, it definitely
                         * won't work, but trying it might have some
                         * unpredicted side effects.
                         */
                        if (string_nbytes(trypath_o) != strlen(trypath_s))
                                break;

                        fp = push_path_from(requested_file,
                                            trypath_s, i == 0);
                        if (fp)
                                break;
                }
                VAR_DECR_REF(paths);
                return fp;
        }
}

void
pop_path(FILE *fp)
{
        Object *bc, *import_path;

        bc = sys_getitem(STRCONST_ID(breadcrumbs));
        import_path = sys_getitem(STRCONST_ID(import_path));

        bug_on(!bc || !isvar_array(bc));
        bug_on(!import_path || !isvar_array(import_path));

        array_setitem(bc, seqvar_size(bc)-1, NULL);

        /*
         * Replace 'sys.import_path[0]' with directory of
         * upstream script
         */
        if (seqvar_size(bc) == 0) {
                /* TODO: bug_on(not interactive mode) */
                array_setitem(import_path, 0, gbl_cwd());
        } else {
                Object *prev, *new_importdir;
                const char *prevstr, *notdir;

                /*
                 * seqvar_size needs to be called again, because
                 * above array_setitem() changed its size.
                 */
                prev = array_borrowitem(bc, seqvar_size(bc)-1);
                bug_on(!isvar_string(prev));

                prevstr = string_cstring(prev);
                notdir = find_notdir(prevstr);

                new_importdir = stringvar_newn(prevstr, notdir - prevstr);
                array_setitem(import_path, 0, new_importdir);

                VAR_DECR_REF(new_importdir);
        }
        VAR_DECR_REF(import_path);
        VAR_DECR_REF(bc);
        fclose(fp);
}

