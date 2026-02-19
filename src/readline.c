#include <evilcandy.h>
#include <stdlib.h>

/* FIXME: Add editline alternative; FSF have their heads up ykw. */
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

        bug_on(!linep);
        bug_on(!size);

        /*
         * XXX: Check using code, it should never call us if fp is
         * not both stdin and a tty, making setting of rl_instream
         * unnecessary.
         */
        inpsave = rl_instream;
        rl_instream = fp;

        if (*linep)
                free(*linep);

        *linep = readline(prompt);
        rl_instream = inpsave;

        if (*linep) {
                add_history(*linep);

                /* all I know is it's at least this amount */
                *size = strlen(*linep);
                return *size;
        } else {
                *size = 0;
                return -1;
        }
}
