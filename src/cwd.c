/*
 * cwd.c - Hooks to things like getcwd, chdir, etc.
 */
#include <evilcandy.h>
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
        size_t size = 64;
        char *buf = emalloc(size);
        char *wd;

        size = 64;
        buf = erealloc(NULL, size);
retry:
        errno = 0;
        wd = getcwd(buf, size);
        if (!wd && errno == ERANGE) {
                size += 64;
                buf = erealloc(buf, size);
                goto retry;
        }

        if (!wd) {
                efree(buf);
                /*
                 * I want to return ErrorVar, but we're called too
                 * early for that.
                 */
                return NULL;
        }

        return stringvar_nocopy(wd);
}

/* TODO: chdir stuff & al. */

