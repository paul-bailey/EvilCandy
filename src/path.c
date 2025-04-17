/*
 * FIXME: The path resolution in this file is naive.  We need need some
 * way to know that we aren't cyclically loading the same files back and
 * forth.
 */
#include <evilcandy.h>
#include <stdlib.h>

#define MAX_LOADS RECURSION_MAX

static const char *paths[MAX_LOADS];
static int path_sp = 0;

static const char *
current_path(void)
{
        return path_sp <= 0 ? "." : paths[path_sp-1];
}

static void
push_path_(const char *path)
{
        if (path_sp >= MAX_LOADS)
                fail("File loads nested too deeply");
        paths[path_sp++] = path;
}

/**
 * pop_path - Pop an EvilCandy script off the path stack and close it.
 * @fp:         File handle to script, should be return value of balanced
 *              call to push_path
 */
void
pop_path(FILE *fp)
{
        fclose(fp);
        bug_on(path_sp <= 0);
        --path_sp;
        free((char *)paths[path_sp]);
}

/**
 * push_path - Push an EvilCandy script onto the path stack and open it.
 * @filename:   File to open.  Path should be absolute, relative to path
 *              of containing script (or current working directory if in
 *              interactive mode), or the built-in path for library files.
 *
 * Return:      File pointer to @filename, opened with "r" permissions.
 *              If failed, this will throw an error rather than return.
 */
FILE *
push_path(const char *filename)
{
        /*
         * XXX this isn't a reentrant func, should this giant buffer
         * be on the stack?
         */
        char pathfill[PATH_MAX];
        FILE *fp = find_import(current_path(), filename,
                               pathfill, PATH_MAX);
#warning "replace with err_setstr"
        if (!fp)
                fail("Could not open '%s'", filename);
        push_path_(estrdup(pathfill));
        return fp;
}


