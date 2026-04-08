#include <evilcandy.h>
#include <evilcandy/locations.h>
#include <types/xptr.h>

struct debug_locations_t {
        size_t instr_offset;
        Object *xptr;
};

static struct debug_locations_t *debug_locations = NULL;
static ssize_t debug_locations_alloc = 0;
static ssize_t debug_nlocations = 0;

void
debug_push_location(Frame *fr, size_t instr_offset)
{
        struct debug_locations_t *loc;
        if (debug_nlocations >= debug_locations_alloc) {
                debug_locations_alloc += 16;
                debug_locations = erealloc(
                        debug_locations,
                        debug_locations_alloc
                         * sizeof(struct debug_locations_t));
                memset(&debug_locations[debug_nlocations], 0,
                       (debug_locations_alloc - debug_nlocations)
                        * sizeof(struct debug_locations_t));
        }
        loc = &debug_locations[debug_nlocations];
        debug_nlocations++;

        loc->instr_offset = instr_offset;
        loc->xptr = (Object *)fr->ex;
        if (loc->xptr)
                VAR_INCR_REF(loc->xptr);
}

void
debug_pop_location(void)
{
        struct debug_locations_t *loc;

        bug_on(debug_nlocations <= 0 || !debug_locations);
        --debug_nlocations;
        loc = &debug_locations[debug_nlocations];
        if (loc->xptr)
                VAR_DECR_REF(loc->xptr);
        memset(loc, 0, sizeof(*loc));
}

void
debug_clear_locations(void)
{
        if (debug_locations) {
                while (debug_nlocations)
                        debug_pop_location();
                efree(debug_locations);
                debug_locations = NULL;
        }
}

static void
print_trace_chars(FILE *fp, ssize_t level)
{
        level *= 2;
        while (level-- > 0)
                fputc(' ', fp);
        fprintf(fp, ">> ");
}

static void
print_err_line(FILE *fpout, const char *filename, int startline, int level)
{
        int c;
        FILE *fpin = fopen(filename, "r");
        if (!fpin)
                return;

        /* skip to line, indexed from 1 */
        startline--;
        while (startline > 0) {
                while ((c = fgetc(fpin)) != EOF) {
                        if (c == '\n') {
                                startline--;
                                break;
                        }
                }

                if (c == EOF)
                        goto close;
        }

        print_trace_chars(fpout, level);
        fprintf(fpout, "line: ");
        while ((c = fgetc(fpin)) != EOF) {
                fputc(c, fpout);
                if (c == '\n')
                        break;
        }
        if (c != '\n')
                fputc('\n', fpout);

close:
        fclose(fpin);
}

/**
 * debug_print_trace - Print trace of locations, used for
 *                     error messages.
 * @fp: File to print to, usually stderr
 * @print_lines: true to attempt reopening file to print line where
 *      suspected error occurred, as well as calling function.  This will
 *      skip pipes and TTYs.
 *
 * Do not call this if there wasn't an untrapped exception; it will
 * be very misleading otherwise.
 */
void
debug_print_trace(FILE *fp, bool print_lines)
{
        struct debug_locations_t *dbgloc;
        ssize_t i;

        dbgloc = &debug_locations[debug_nlocations - 1];
        for (i = 0; i < debug_nlocations; i++, dbgloc--) {
                const char *filename;
                const char *funcname;
                Object *fname_obj;
                struct location_t locations;
                struct xptrvar_t *xptr = (struct xptrvar_t *)dbgloc->xptr;

                if (!xptr)
                        continue;

                if (location_unpack(xptr->locations,
                                    xptr->locations_size,
                                    dbgloc->instr_offset,
                                    &locations) == RES_ERROR) {
                        /* anything else we print is likely unreliable */
                        return;
                }

                print_trace_chars(fp, i);

                fname_obj = xptr->funcname;
                if (fname_obj)
                        funcname = string_cstring(fname_obj);
                else
                        funcname = "<anonymous>";
                if (i > 0)
                        fprintf(fp, "called from %s ", funcname);
                else
                        fprintf(fp, "in function %s ", funcname);

                filename = xptr->file_name;
                if (!filename)
                        filename = "<unnamed>";
                fprintf(fp, "in file %s line %d\n", filename,
                        (int)locations.loc_startline);
                if (print_lines && filename[0] != '<') {
                        print_err_line(fp, filename,
                                       locations.loc_startline, i);
                }
        }

        debug_clear_locations();
}

