#include <evilcandy/debug.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/myreadline.h>
#include <lib/helpers.h>

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>

/**
 * myreadline - Read line from input with readline or editline interface
 * @linep:      Pointer to variable to store result.
 * @sizecap:    Pointer to variable to store allocated size of line.
 *              (This is a convenient fiction; @sizecap will either be
 *              strlen(*linep) or zero.)
 * @fp:         Input stream.  There's no reason I can think of why this
 *              is not stdin and not a TTY.
 * @prompt:     Prompt to print for this line.
 *
 * Return: Number of characters retrieved, or -1 if EOF or error.
 */
ssize_t
myreadline(char **linep, size_t *size, FILE *fp, const char *prompt)
{
        FILE *inpsave;
        static bool init = false;
        char *new_line;

        bug_on(!linep);
        bug_on(!size);

        /*
         * XXX: Check using code, it should never call us if fp is
         * not both stdin and a tty, making setting of rl_instream
         * unnecessary.
         */
        inpsave = rl_instream;
        rl_instream = fp;

        if (!init) {
                rl_initialize();
                rl_bind_key('\t', rl_insert);
                clear_history();
                using_history();
                add_history("");
                init = true;
        }

        new_line = readline(prompt);
        rl_instream = inpsave;

        if (new_line) {
                char *old_line = *linep;
                size_t n = strlen(new_line);
                if (n + 2 > *size) {
                        if (old_line)
                                efree(old_line);
                        old_line = emalloc(n + 2);
                        *size = n + 2;
                } else {
                        bug_on(!old_line);
                }
                memcpy(old_line, new_line, n);
                old_line[n] = '\n';
                old_line[n + 1] = '\0';
                *linep = old_line;

                if (n > 0)
                        add_history(new_line);
                /*
                 * See ewrappers.c. efree() is just for bookkeepping,
                 * but new_line was not malloc'd by our mm system, so
                 * call free() directly.
                 */
                free(new_line);

                /* +1 becase we needed to add our own '\n' */
                return n + 1;
        } else {
                if (*linep) {
                        efree(*linep);
                        *linep = NULL;
                }
                *size = 0;
                return -1;
        }
}
#else /* !HAVE_READLINE */

/*
 * If readline is unavailable, just use getline() and live with the
 * lousy UI that comes with it.
 */
ssize_t
myreadline(char **linep, size_t *size, FILE *fp, const char *prompt)
{
        if (prompt) {
                fprintf(stderr, "%s", prompt);
                fflush(stderr);
        }
        return egetline(linep, size, fp);
}

#endif /* !HAVE_READLINE */

