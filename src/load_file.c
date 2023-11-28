#include "egq.h"
#include <stdlib.h>
#include <string.h>

enum {
        PATHLEN = 4096,
};

struct file_state_t {
        int sp;
        int fp; /* "frame pointer", not "file pointer" */
        struct marker_t pc;
};

static struct file_state_t ns_stack[LOAD_MAX];
static struct file_state_t *ns_sp = ns_stack;

static void
nspush(struct ns_t *new)
{
        if (ns_sp >= &ns_stack[LOAD_MAX])
                syntax("Too many imports; stack overflow");

        ns_sp->sp = q_.sp;
        ns_sp->fp = q_.fp;
        PC_SAVE(&ns_sp->pc);

        cur_ns = new;
        cur_oc = (struct opcode_t *)new->pgm.s;
        ns_sp++;
}

static void
nspop(void)
{
        bug_on(ns_sp <= ns_stack);
        --ns_sp;

        q_.fp = ns_sp->fp;
        PC_GOTO(&ns_sp->pc);

        /*
         * We may have "break"-en early, so some stuff is on stack
         * If an imported file wants something to be "seen" by the
         * calling file, it should have been appended to an
         * already-visible object, usu. "__gbl__"
         */
        bug_on(q_.sp < ns_sp->sp);
        stack_unwind_to(ns_sp->sp);
}


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

        /* The old path was relative to CWD, so use that */
        old_path = cur_ns ? cur_ns->fname : "";
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

static void
exec_block(void)
{
        /* Note: keep this re-entrant */
        int sp = q_.sp;
        for (;;) {
                int ret = expression(NULL, FE_TOP);
                if (ret) {
                        if (ret == 3)
                                break;
                        syntax("Cannot '%s' from top level",
                                ret == 1 ? "return" : "break");
                }
        }
        stack_unwind_to(sp);
}

void
load_file(const char *filename)
{
        struct ns_t *ns;
        char *path;

        bug_on(!filename);

        /*
         * TODO (someday): If !filename, skip prescan and enter
         * interactive mode.
         */
        path = filename[0] == '/'
                ? literal_put(filename) : convert_path(filename);
        ns = prescan(path);
        if (!ns)
                return;

        nspush(ns);

        /*
         * dirty, but we only do it here: we want the first
         * call to qlex() to get the FIRST opcode, not the SECOND.
         * We don't call q_unlex, because that will trap an
         * out-of-bounds bug.  Like I said, it's dirty.
         */
        cur_oc--;

        exec_block();
        nspop();

        /*
         * We "popped" out of our starting file, so we're done.
         * FIXME: way to properly unwind
         */
        if (!cur_ns) {
                bug_on(ns_sp != ns_stack);
                exit(0);
        }
}
