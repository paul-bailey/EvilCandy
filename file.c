/*
 * file.c - Access to input files.
 *
 * file_push() pushes the current input file onto a local stack and opens
 * a new file.
 *
 * next_line() gets the next line from the current input file.  It also
 * wraps the `pop' equivalent of file_push().
 */
#include "egq.h"
#include <string.h>
#include <stdlib.h>

enum {
        PATHLEN = 1024,
        NFILES = 128,
};

/**
 * struct fdata_t - Input file state to save on our local stack.
 * @fp: File handle
 * @lineno: Most recent line number read from file
 * @curpath: Path of the file relative to actual current working directory
 * @infilename: Saved name of file, including path relative to
 *      higher-level file's path, or current working directory if this
 *      is the top-level stack entry.
 */
struct fdata_t {
        FILE *fp;
        int lineno;
        char *curpath;
        char *infilename;
};

/*
 * We don't truly change directory.  We just keep track of the current
 * file's path relative to our true cwd, and we store it in the array
 * curpath[].
 *
 * Every depth of file_push() will save a duplicate in the pushed struct
 * fdata_t.  During file_pop(), it will be copied back into curpath[] and
 * the duplicate will be freed.
 *
 * This is a lot of malloc'ing and freeing for a lot of strings that will
 * look awfully similar, but there's no way to know the previous path
 * unless we save it.
 */
static char curpath[PATHLEN] = { 0 };

/* Our stack of file info */
static struct fdata_t file_stack[NFILES];

/* Index of current depth of file_stack[] */
static int file_sp = 0;

static inline bool
isupdir(const char *s)
{
        return s[0] == '.' && s[1] == '.' && s[2] == '/';
}

static inline bool
issamedir(const char *s)
{
        return s[0] == '.' && s[1] == '/';
}

/*
 * fill_path_helper - copy src to dst, but reduce "a/b/../" to just "a/"
 *
 * @dst_start is the start of the destination buffer, and
 * @dst points at the offset of @dst_start for this specific call.
 *
 * Return: Updated @dst, points after the last char placed.  This
 * could be earlier than starting @dst, but never earlier than
 * @dst_start.
 */
static char *
fill_path_helper(char *dst, const char *src, char *dst_start)
{
        char *end = dst_start + PATHLEN;

        while (*src != '\0' && dst < end) {
                if (*src == '/') {
                        /*
                         * Skip duplicates of '/' delimiter.
                         * We should have already trapped abs path.
                         */
                        while (*src == '/')
                                ++src;
                } else if (issamedir(src)) {
                        src += 2;
                } else if (isupdir(src)) {
                        /* try reduce "a/b/../" to just "a/" */
                        if ((dst == dst_start)
                            || (dst - dst_start >= 3 && isupdir(dst-3))) {
                                /* We're upstream of CWD */
                                if (dst + 2 < end) {
                                        *dst++ = *src++;
                                        *dst++ = *src++;
                                        *dst++ = *src++;
                                } else
                                        goto toolong;
                        } else {
                                /* Downstream of CWD, we can reduce */
                                bug_on(dst[-1] != '/');
                                bug_on(dst - dst_start < 2);
                                dst -= 2;
                                while (*dst != '/' && dst > dst_start)
                                        --dst;
                                if (*dst == '/')
                                        ++dst;
                                src += 3;
                        }
                } else {
                        /* Just copy this dir */
                        int c;
                        while ((c = *src++) != '/' && c != '\0') {
                                if (dst < end)
                                        *dst++ = c;
                        }
                        if (c == '\0') {
                                --src;
                                break;
                        }
                        if (dst < end)
                                *dst++ = '/';
                }
        }

        bug_on(dst > end);
        /* Because after this we still need to add either '/' or '\0' */
        if (dst == end)
                goto toolong;
        return dst;

toolong:
        qsyntax("File name too long");
        return NULL; /* <- so compiler won't gripe */
}

/**
 * fill_path_name - a path conversion function
 * @name:       path of new file relative to previous file.  This may not
 *              be absolute.
 * @path:       Char array (length PATHLEN) to store the result, which
 *              is the path relative to the current working directory.
 */
static void
fill_path_name(const char *name, char *path)
{
        char *dst;

        /* skip the most frequently-typed redundancy in path */
        while (issamedir(name))
                name += 2;

        dst = fill_path_helper(path, curpath, path);
        if (dst != path)
                *dst++ = '/';
        dst = fill_path_helper(dst, name, path);
        *dst = '\0';

        /*
         * Leading "../" in final result implies an @include command is
         * used for a file outside our source tree (since we're most
         * likely called from the same dir as our top-level Makefile),
         * so throw a warning.
         *
         * The second check is so that we only warn once for each NEW
         * departure outside the source tree. We don't want to warn for
         * all the nested @includes in the out-of-tree file; this state
         * will reset when we file_pop() back to something downstream of
         * our CWD.
         *
         * Hypothetically, an "upstream" path could be "downstream" if
         * "../" was abused badly enough, but since I don't want to
         * bother with comparing path to getcwd() and such, I have no
         * way to tell if a dir name leads us back into our CWD.
         */
        if (isupdir(path) && !isupdir(curpath)) {
                warning("`%s' is upstream of the current working directory",
                        path);
        }

        bug_on(path[0] == '/');
}

/**
 * file_push - Save file state and open new file
 * @name: Name of new file relative to path of old file
 */
void
file_push(const char *name)
{
        char path[PATHLEN];
        char *slash;
        struct fdata_t *f;

        if (name[0] == '/')
                qsyntax("Cannot @include files with absolute paths");

        f = &file_stack[file_sp];
        file_sp++;
        if (file_sp > NFILES)
                qsyntax("Excessive @include recursion");

        f->fp           = q_.infile;
        f->lineno       = q_.lineno;
        f->curpath      = estrdup(curpath);
        f->infilename   = q_.infilename;

        fill_path_name(name, path);

        /* Update curpath */
        strcpy(curpath, path);
        slash = strrchr(curpath, '/');
        if (slash)
                *slash = '\0';
        else
                curpath[0] = '\0';

        q_.infilename = estrdup(name);
        q_.lineno = 0;
        q_.infile = fopen(path, "r");
        if (!q_.infile)
                fail("Cannot open `%s'", path);

}

/* Opposite of file_push */
static void
file_pop(void)
{
        struct fdata_t *f;

        bug_on(q_.infile == NULL);
        fclose(q_.infile);
        bug_on(!q_.infilename);
        free(q_.infilename);

        --file_sp;
        bug_on(file_sp < 0);
        f = &file_stack[file_sp];

        q_.infilename  = f->infilename;
        q_.infile      = f->fp;
        q_.lineno      = f->lineno;
        strcpy(curpath, f->curpath);
        if (file_sp) {
                bug_on(!f->curpath);
                free(f->curpath);
        }
        memset(f, 0, sizeof(*f));
}

/**
 * next_line - Get next line from input
 * @flags: If NL_INFILE and EOF is reached, return NULL instead of
 *      getting next line of next-higher-level file.
 *
 * Return: Next line of input, or NULL if last line in top-level
 * input file has been read. Do not free this return value.
 */
char *
next_line(unsigned int flags)
{
        static char *line = NULL;
        static size_t n = 0;
        ssize_t res;

retry:
        if (!q_.infile)
                return NULL;
        res = getline(&line, &n, q_.infile);
        if (res == -1) {
                /* TODO: Distinguish between EOF and error */
                if (file_sp > 0) {
                        file_pop();
                        goto retry;
                }
                return NULL;
        }

        ++q_.lineno;
        return line;
}


