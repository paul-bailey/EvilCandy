/*
 * To test reduce_pathname_in_place:
 *      set TEST_RPIP to 1
 *      cc -DTEST_RPIP path2.c
 *      ./a.out
 * If you can think of some more ways to test it,
 * add it to main() below.
 */

#ifdef TEST_RPIP
# include <assert.h>
# include <string.h>
# include <stdio.h>
# define bug_on(cond) assert(!(cond))
#else
# include <evilcandy.h>
#endif

#define SEP '/'
#define DOT '.'
#define BACKDIR "../"
#define BACKDIR_LEN 3
#define THISDIR "./"
#define THISDIR_LEN 2

/*
 * TODO: Move reduce_pathname_in_place and TEST_RPIP stuff to
 * separate file.
 */

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

/*
 * Reduce an absolute path name to its smallest representation, ie get
 * rid of all the superfluous "./" and "../"
 *
 * FIXME: This is naive.  push_path changes current IMPORT directory,
 * not the current WORKING directory, so there's no chdir or such.
 * This does not crawl through the directory tree the way the OS might
 * (cf namei() in Unix and old versions of Linux); it just reduces all
 * the ".." and "." to make a minimally-expressed absolute path.  This
 * means that a path request like "/a/b/c/../d" might succeed even if
 * the 'c' directory does not exist.
 *
 * XXX: does it matter anymore? File is closed by the time it's being
 * executed.  Recursion is still possible without the breadcrumbs check,
 * but a larger amount of it can be tolerated.
 */
static void
reduce_pathname_in_place(char *path)
{
        char *head, *tail;

        bug_on(path[0] != SEP);

        /*
         * Some redundancies ("." and "..") may reveal others ("//"),
         * so if anything changes, this loop will iterate additional
         * times until it stops changing @path.
         */
        do {
                tail = head = path;
                while (*head != '\0') {
                        bug_on(head[0] != SEP);
                        while (head[1] == SEP)
                                head++;
                        *tail++ = *head++;
                        if (!strncmp(head, THISDIR, THISDIR_LEN)) {
                                head++;
                        } else if (!strncmp(head, BACKDIR, BACKDIR_LEN)) {
                                /*
                                 * Undo last directory copy.
                                 *
                                 * Don't make a scene if there are too
                                 * many of these. "ls -a /" reveals a
                                 * directory named ".." even though it's
                                 * from the top level, so treat "/../"
                                 * the same as "/".
                                 */
                                head += 2;
                                --tail;

                                while (*tail == SEP && tail > path)
                                        tail--;
                                while (*tail != SEP && tail > path)
                                        tail--;
                                bug_on(*tail != SEP && tail != path);
                        } else while (*head != SEP && *head != '\0') {
                                *tail++ = *head++;
                        }
                }
                *tail = '\0';
        } while (tail != head);

        if (tail == path) {
                *tail++ = SEP;
                *tail = '\0';
        }
}

#ifndef TEST_RPIP

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
        char *newpath;
        FILE *fp;

        bug_on(!refpath && requested_file[0] != SEP);

        if (!refpath)
                refpath = "";
        newpath = emalloc(strlen(requested_file) + strlen(refpath) + 2);
        sprintf(newpath, "%s/%s", refpath, requested_file);
        bug_on(newpath[0] != SEP);

        fp = fopen(newpath, "r");
        if (fp) {
                /* TODO: Check that @fp is not a directory. */
                Object *bc, *bcnew;
                char *notdir;

                reduce_pathname_in_place(newpath);

                bc = sys_getitem(STRCONST_ID(breadcrumbs));
                bug_on(!bc || bc == ErrorVar || !isvar_array(bc));
                bcnew = stringvar_new(newpath);
                array_append(bc, bcnew);
                VAR_DECR_REF(bcnew);

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

/* FIXME: Obviously this does not work on Windows */
static bool
path_is_absolute(const char *path)
{
        return path[0] == SEP;
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
                array_setitem(import_path, 0, gbl.cwd);
        } else {
                Object *prev, *new_importdir;
                const char *prevstr;
                char *notdir;

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
}

#else /* TEST_RPIP vvv defined vvv ... ^^^ !defined ^^^ */

static int fail = 0;

static void
test_rpip(const char *trypath, const char *expect)
{
        char path[1000];
        bug_on(strlen(trypath) > sizeof(path)-1);
        strcpy(path, trypath);
        reduce_pathname_in_place(path);
        if (strcmp(path, expect)) {
                fail++;
                fprintf(stderr, "Expect '%s' but got '%s'\n", expect, path);
        }
}

int
main(void)
{
        /* TODO: a bunch more */
        test_rpip("/../a", "/a");
        test_rpip("////a/././b", "/a/b");
        test_rpip("/a/b/c", "/a/b/c");
        test_rpip("/a/../../../b/./.c/..c", "/b/.c/..c");

        fprintf(stderr, "#failures: %d\n", fail);
}

#endif /* TEST_RPIP */
