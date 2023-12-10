#include <evilcandy.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOADS RECURSION_MAX

static const char *paths[MAX_LOADS];
static int path_sp = 0;

static const char *
cur_path(void)
{
        return path_sp <= 0 ? "" : paths[path_sp-1];
}

/*
 * Since we don't change directory into path of new files,
 * we need to fake it.
 * @name:       path of new file relative to previous file.
 * Return:      (newly allocated) path of new file relative to the
 *              working directory,
 */
static char *
convert_path(const char *name)
{
        const char *path, *notdir;
        char *new_path;
        size_t old_len;

        /* absolute path? */
        if (name[0] == '/')
                goto new_only;

        path = cur_path();
        if (path[0] == '\0')
                goto new_only;

        notdir = my_strrchrnul(path, '/');
        if (notdir[0] == '\0')
                goto new_only;

        /* maybe not a bug, but who would put a script in '/'? */
        bug_on(notdir == path);

        old_len = notdir - path;

        /* +2 for '/' and nulchar at end */
        new_path = malloc(old_len + strlen(name) + 2);
        memcpy(new_path, path, old_len);
        new_path[old_len] = '/';
        strcpy(new_path + old_len + 1, name);
        return new_path;

new_only:
        return strdup(name);
}

static void
push_path(const char *path)
{
        if (path_sp >= MAX_LOADS)
                fail("Files loads nested too deep");
        paths[path_sp++] = path;
}

static void
pop_path(void)
{
        bug_on(path_sp <= 0);
        --path_sp;
        free((char *)paths[path_sp]);
}

/**
 * load_file - Read in a file, tokenize it, assemble it, execute it.
 * @filename: Path to file relative to current working directory
 */
void
load_file(const char *filename)
{
        struct token_t *oc;
        char *path;
        struct executable_t *ex;

        bug_on(!filename);
        path = filename[0] == '/'
                ? strdup(filename) : convert_path(filename);
        push_path(path);
        oc = prescan(path);
        if (!oc)
                goto out;

        if ((ex = assemble(filename, oc)) == NULL)
                syntax("Failed to assemble");

        if (q_.opt.disassemble_only)
                goto out;

        if (ex)
                vm_execute(ex);
out:
        pop_path();
}
