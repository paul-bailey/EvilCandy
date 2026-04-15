/*
 * cwd.c - Hooks to things like getcwd, chdir, etc.
 */
#include <evilcandy/ewrappers.h>
#include <evilcandy/types/string.h>
#include <internal/cwd.h>
#include <unistd.h>
#include <errno.h>

/**
 * evc_getcwd - Get current working directory
 *
 * Return: String object of current working directory.  Note that this
 * is NOT NECESSARILY the same as the current import directory.
 */
Object *
evc_getcwd(void)
{
        size_t size = 0;
        char *buf = NULL;
        char *wd;
        Object *ret;

        do {
                size += 32;
                buf = erealloc(buf, size);
                errno = 0;
                wd = getcwd(buf, size);
        } while (!wd && errno == ERANGE);

        if (!wd) {
                efree(buf);
                /*
                 * I want to return ErrorVar, but we're called too
                 * early for that.
                 */
                return NULL;
        }

        ret = stringvar_new(wd);
        efree(wd);
        return ret;
}

/* TODO: chdir stuff & al. */

