/*
 * FIXME: The path resolution in this file is naive.  We need need some
 * way to know that we aren't cyclically loading the same files back and
 * forth.
 */
#include <evilcandy.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

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

/* fp should be return value of balanced call to push_path */
static void
pop_path(FILE *fp)
{
        fclose(fp);
        bug_on(path_sp <= 0);
        --path_sp;
        free((char *)paths[path_sp]);
}

static FILE *
push_path(const char *filename)
{
        /*
         * XXX this isn't a reentrant func, should this giant buffer
         * be on the stack?
         */
        char pathfill[PATH_MAX];
        FILE *fp = find_import(current_path(), filename,
                               pathfill, PATH_MAX);
        if (!fp)
                fail("Could not open '%s'", filename);
        push_path_(estrdup(pathfill));
        return fp;
}

/**
 * load_file - Read in a file, tokenize it, assemble it, execute it.
 * @filename:   Path to file as written after the "load" keyword or on
 *              the command line.
 */
void
load_file(const char *filename)
{
        FILE *fp = push_path(filename);
        run_script(filename, fp);
        pop_path(fp);
}

