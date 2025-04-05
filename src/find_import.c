/*
 * find_inport.c - resolves path for ``load'' command
 *
 * This checks the new file path relative to the current loaded
 * directory.  If not found there, try RCDATADIR.
 *
 * TODO: Way to clarify to calling code all the different ways
 * this procedure could fail, something better than just returning
 * NULL.  Problem is, if this is first import, it's too early to
 * throw a fail() or syntax().
 */
#include <evilcandy.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

enum {
        SEP = '/'
};

static FILE *
import_at_(const char *path, const char *file_name)
{
        int fd;
        DIR *dirp;

        dirp = opendir(path);
        if (!dirp)
                return NULL;
        fd = openat(dirfd(dirp), file_name, O_RDONLY, 0666);
        if (fd >= 0) {
                /* TODO: fail if dir */
                FILE *fp = fdopen(fd, "r");
                if (fp) {
                        closedir(dirp);
                        return fp;
                }
                close(fd);
        }
        closedir(dirp);
        return NULL;
}

static FILE *
import_at(const char *path, const char *file_name,
          char *pathfill, size_t size,
          const char *notdir, size_t newdir_len)
{
        size_t cur_len = strlen(path);
        if (cur_len + newdir_len + 2 > size)
                return NULL;

        memcpy(pathfill, path, cur_len);
        if (newdir_len > 0) {
                pathfill[cur_len] = SEP;
                strncpy(&pathfill[cur_len + 1], file_name, newdir_len);
                pathfill[cur_len + newdir_len + 1] = '\0';
        } else {
                pathfill[cur_len] = '\0';
        }
        return import_at_(pathfill, notdir);
}


/*
 * the actual find_import_, find_import wraps this by saving
 * errno.
 */
static FILE *
find_import_(const char *cur_path, const char *file_name,
            char *pathfill, size_t size)
{
        const char *notdir;
        size_t newdir_len;

        notdir = my_strrchrnul(file_name + 1, SEP);
        if (notdir[0] == '\0') {
                notdir = file_name;
                newdir_len = notdir - file_name;
        } else {
                /* file name ends with SEP? */
                notdir++;
                if (notdir[0] == '\0')
                        return NULL;
                newdir_len = notdir - 1 - file_name;
        }

        if (file_name[0] == SEP) {
                /* absolute path */
                if (newdir_len >= size)
                        return NULL;
                memcpy(pathfill, file_name, newdir_len);
                pathfill[newdir_len] = '\0';
                return fopen(file_name, "r");
        } else {
                FILE *fp;
                const char *p_cur_path;

                /*
                 * leading "./" are like styrofoam peanuts,
                 * they just accumulate and get everywhere.
                 */
                p_cur_path = cur_path;
                while (p_cur_path[0] == '.' && p_cur_path[1] == '/')
                        p_cur_path += 2;

                fp = import_at(p_cur_path, file_name, pathfill,
                               size, notdir, newdir_len);
                if (!fp) {
                        /*
                         * XXX this would mean a library script has a bug,
                         * trying to import a script that doesn't exist!
                         * We should warn or something.
                         */
                        if (!strcmp(cur_path, RCDATADIR))
                                return NULL;

                        fp = import_at(RCDATADIR, file_name, pathfill,
                                       size, notdir, newdir_len);
                }
                return fp;
        }
}

/**
 * find_import - Get a file to import
 * @cur_path:   Path of the currently executed file
 * @file_name:  Name of the file as written after the "load" statement
 * @pathfill:   Buffer to store resultant path name (not counting the
 *              file name).  Caller should then push @cur_path onto a
 *              stack and set @cur_path to @pathfill
 * @size: Length of @pathfill.
 *
 * Return:
 * file pointer to new file being imported, or NULL if file could not be
 * found or opened.
 */
FILE *
find_import(const char *cur_path, const char *file_name,
            char *pathfill, size_t size)
{
        int errno_save = errno;
        FILE *fp = find_import_(cur_path, file_name, pathfill, size);
        errno = errno_save;
        return fp;
}


