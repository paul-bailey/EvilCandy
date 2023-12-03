#include "egq.h"
#include <stdlib.h>
#include <string.h>

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

static struct buffer_t path = {
        .s = NULL,
        .p = 0,
        .size = 0,
};

static char *
prev_or_start(char *p, char *start, int c)
{
        while (p > start && *p != (char)c)
                --p;
        return p;
}

static void
convert_path_helper(const char *src, const char *src_end)
{
        while (src < src_end) {
                if (*src == '/') {
                        while (*src == '/')
                                ++src;
                } else if (issamedir(src)) {
                        src += 2;
                } else if (isupdir(src)) {
                        /* try reduce "a/b/../" to just "a/" */
                        char *start = path.s;
                        char *dst = path.s + path.p;
                        if (dst == start ||
                            (dst - start >= 3 && isupdir(dst-3))) {
                                /* We're upstream of CWD */
                                buffer_putc(&path, *src++);
                                buffer_putc(&path, *src++);
                                buffer_putc(&path, *src++);
                        } else {
                                /*
                                 * Downstream of CWD, we can reduce
                                 * Reset struct buffer_t to previous '/'.
                                 *
                                 *      Hack alert!
                                 * Requires knowledge of token.c code.
                                 * Even though we have '/' in place,
                                 * move ptr to that pos and buffer_putc '/'
                                 * anyway.  It guarantees a nulchar right
                                 * afterward in case we finish here.
                                 */
                                bug_on(dst[-1] != '/');
                                bug_on(dst - start < 2);
                                dst = prev_or_start(dst - 2, start, '/');
                                path.p = dst - start;
                                buffer_putc(&path, '/');
                                src += 3;
                        }
                } else {
                        /* Just copy this dir */
                        int c;
                        while (src < src_end && (c = *src++) != '/')
                                buffer_putc(&path, c);
                        if (src < src_end)
                                buffer_putc(&path, '/');
                }
        }
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
        char *old_path;

        if (!path.s)
                buffer_putc(&path, 'a');
        buffer_reset(&path);

        /* TODO: This will be different when we implement ``load'' keyword */
        old_path = "";
        if (!old_path)  /* <- in case of stdin */
                old_path = "";
        convert_path_helper(old_path, my_strrchrnul(old_path, '/'));
        if (path.s[0] != '\0')
                buffer_putc(&path, '/');

        /* skip the most frequently-typed redundancy in path */
        while (issamedir(name))
                name += 2;

        convert_path_helper(name, name + strlen(name));
        return literal_put(path.s);
}

void
load_file(const char *filename)
{
        struct token_t *oc;
        char *path;
        struct executable_t *ex;

        bug_on(!filename);

        /*
         * TODO (someday): If !filename, skip prescan and enter
         * interactive mode.
         */
        path = filename[0] == '/'
                ? literal_put(filename) : convert_path(filename);
        oc = prescan(path);
        if (!oc)
                return;

        if ((ex = assemble(filename, oc)) == NULL)
                warning("Failed to assemble");

        if (q_.opt.disassemble_only)
                return;

        if (ex)
                vm_execute(ex);
}
