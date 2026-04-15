/*
 * debug.c - A stack of function-enter-and-exit points, to save location
 *           info in case of an error.
 *
 * When entering a UAPI function: call debug_push_location().
 * When exiting a UAPI function: call debug_pop_location().
 * When reporting a location where an exception was raised: use
 *      debug_mark_error()
 */
#include <evilcandy.h>
#include <evilcandy/ewrappers.h>
#include <internal/locations.h>
#include <internal/types/string.h>
#include <internal/types/xptr.h>
#include <internal/vm.h>

struct debug_locations_t {
        size_t instr_offset;
        Object *xptr;
};

struct debug_stack_t {
        struct debug_locations_t *locations;
        ssize_t locations_alloc;
        ssize_t nlocations;
} debug = {
        .locations = NULL,
        .locations_alloc = 0,
        .nlocations = 0,
};

static struct debug_stack_t debug_saved_error;
static bool debug_error = false;

/*
 * Push once more, then freeze state.  Future calls to debug_mark_error
 * will have no effect until error is cleared. This is because multiple
 * calls to debug_mark_error will occur while unwinding the stack to the
 * top level.
 *
 * @instr_offset: - If error occured in a user function, this is the
 *                  index from the base of the instructions array.
 *                - If error occured in a built-in function, this is -1.
 *                  For built-in functions, we'll have to live with a
 *                  traceback which stops at the built-in function call.
 */
void
debug_mark_error(Frame *fr, ssize_t instr_offset)
{
        struct debug_locations_t *saved_locations;
        ssize_t i, locations_nbytes;

        if (debug_error)
                return;

        debug_error = true;

        if (instr_offset >= 0)
                debug_push_location(fr, instr_offset);

        locations_nbytes = debug.nlocations * sizeof(*saved_locations);
        saved_locations = emalloc(locations_nbytes);
        memcpy(saved_locations, debug.locations, locations_nbytes);
        for (i = 0; i < debug.nlocations; i++)
                VAR_INCR_REF(saved_locations[i].xptr);

        debug_saved_error.locations       = saved_locations;
        debug_saved_error.locations_alloc = locations_nbytes;
        debug_saved_error.nlocations      = debug.nlocations;

        /*
         * Don't save the error location with the normal debug stack,
         * only keep it with debug_saved_error
         */
        debug_pop_location();
}

bool
debug_has_error(void)
{
        return debug_error;
}

void
debug_free_error(void)
{
        ssize_t i;

        bug_on(!debug_error);
        for (i = 0; i < debug_saved_error.nlocations; i++) {
                struct debug_locations_t *loc;

                loc = &debug_saved_error.locations[i];
                bug_on(!loc->xptr);
                VAR_DECR_REF(loc->xptr);
                loc->xptr = NULL;
        }
        efree(debug_saved_error.locations);
        memset(&debug_saved_error, 0, sizeof(debug_saved_error));
        debug_error = false;
}

void
debug_push_location(Frame *fr, size_t instr_offset)
{
        struct debug_locations_t *loc;
        if (debug.nlocations >= debug.locations_alloc) {
                debug.locations_alloc += 16;
                debug.locations = erealloc(
                        debug.locations,
                        debug.locations_alloc
                         * sizeof(struct debug_locations_t));
                memset(&debug.locations[debug.nlocations], 0,
                       (debug.locations_alloc - debug.nlocations)
                        * sizeof(struct debug_locations_t));
        }
        loc = &debug.locations[debug.nlocations];
        debug.nlocations++;

        loc->instr_offset = instr_offset;
        loc->xptr = (Object *)fr->ex;
        if (loc->xptr)
                VAR_INCR_REF(loc->xptr);
}

void
debug_pop_location(void)
{
        struct debug_locations_t *loc;

        bug_on(debug.nlocations <= 0 || !debug.locations);
        --debug.nlocations;
        loc = &debug.locations[debug.nlocations];
        if (loc->xptr)
                VAR_DECR_REF(loc->xptr);
        memset(loc, 0, sizeof(*loc));
}

/*
 * Only call this during de-init at the end of the program.
 * For normal runtime operation just call debug_push/pop_location().
 */
void
debug_clear_locations(void)
{
        if (debug.locations) {
                while (debug.nlocations)
                        debug_pop_location();
                efree(debug.locations);
                debug.locations = NULL;
                debug.locations_alloc = 0;
        }
        if (debug_error)
                debug_free_error();
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
 * This clears the error traceback.
 */
void
debug_print_trace(FILE *fp, bool print_lines)
{
        /*
         * TODO: Move all this to a helper function so we can give a choice
         * between printing an error traceback or a normal traceback.
         */
        struct debug_stack_t *dbg = &debug_saved_error;
        struct debug_locations_t *dbgloc;
        ssize_t i;

        /* print nothing */
        if (!debug_error)
                return;

        dbgloc = &dbg->locations[dbg->nlocations - 1];
        for (i = 0; i < dbg->nlocations; i++, dbgloc--) {
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

        debug_free_error();
}

