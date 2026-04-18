/*
 * debug.c - A stack of function-enter-and-exit points, to save location
 *           info in case of an error.
 *
 * When entering a UAPI function: call debug_push_location().
 * When exiting a UAPI function: call debug_pop_location().
 * When reporting a location where an exception was raised: use
 *      debug_mark_error()
 *
 * XXX REVISIT: This file and its functions ought to be named "trace",
 * "call_trace", or something like that.  This has very little relation
 * to the stuff in evilcandy/debug.h.
 */
#include <evilcandy/debug.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/global.h>
#include <evilcandy/types/number_types.h>
#include <evilcandy/types/tuple.h>
#include <internal/locations.h>
#include <internal/types/string.h>
#include <internal/types/xptr.h>
#include <internal/vm.h>
#include <internal/types/number_types.h>

/*
 * FIXME: debug and debug_error ought to be a part of the runtime state
 * in gbl.
 */
struct debug_locations_t {
        size_t instr_offset;
        Object *xptr;
};

struct debug_stack_t {
        struct debug_locations_t *locations;
        ssize_t locations_alloc;
        ssize_t nr_locations;
} debug = {
        .locations = NULL,
        .locations_alloc = 0,
        .nr_locations = 0,
};

static bool debug_error = false;

static void
debug_free_stack(struct debug_stack_t *dbg)
{
        size_t i;
        for (i = 0; i < dbg->nr_locations; i++) {
                Object *xptr = dbg->locations[i].xptr;
                if (xptr)
                        VAR_DECR_REF(xptr);
        }
        efree(dbg->locations);
        efree(dbg);
}

static void
debug_push_location_to(Frame *fr, size_t instr_offset,
                       struct debug_stack_t *dbg)
{
        struct debug_locations_t *loc;
        if (dbg->nr_locations >= dbg->locations_alloc) {
                dbg->locations_alloc += 16;
                dbg->locations = erealloc(
                        dbg->locations,
                        dbg->locations_alloc
                         * sizeof(struct debug_locations_t));
                memset(&dbg->locations[dbg->nr_locations], 0,
                       (dbg->locations_alloc - dbg->nr_locations)
                        * sizeof(struct debug_locations_t));
        }
        loc = &dbg->locations[dbg->nr_locations];
        dbg->nr_locations++;

        loc->instr_offset = instr_offset;
        loc->xptr = (Object *)fr->ex;
        if (loc->xptr)
                VAR_INCR_REF(loc->xptr);
}

static Object *
debug_stack_to_object(struct debug_stack_t *dbg)
{
        /* avoid yet-another-malloc-and-free if we can help it */
        enum { ON_STACK_SIZE = 12 };
        size_t i;
        Object **stack, *ret;
        Object *stack_stack[ON_STACK_SIZE];

        if (dbg->nr_locations > ON_STACK_SIZE)
                stack = emalloc(sizeof(Object *) * dbg->nr_locations);
        else
                stack = stack_stack;

        for (i = 0; i < dbg->nr_locations; i++) {
                Object *tup_stack[2];
                struct debug_locations_t *loc = &dbg->locations[i];

                tup_stack[0] = intvar_new(loc->instr_offset);
                if (loc->xptr)
                        tup_stack[1] = VAR_NEW_REF(loc->xptr);
                else
                        tup_stack[1] = VAR_NEW_REF(NullVar);
                stack[i] = tuplevar_from_stack(tup_stack, 2, true);
        }
        ret = tuplevar_from_stack(stack, dbg->nr_locations, true);

        if (stack != stack_stack)
                efree(stack);
        return ret;
}

static struct debug_stack_t *
debug_stack_copy(const struct debug_stack_t *from)
{
        struct debug_stack_t *dbg;
        struct debug_locations_t *locations;
        size_t i, nbytes;
        const size_t WIDTH = sizeof(struct debug_locations_t);

        dbg = emalloc(sizeof(*dbg));
        memset(dbg, 0, sizeof(*dbg));

        nbytes = from->nr_locations * WIDTH;
        /* we just happen to know we're going to push one more */
        locations = emalloc(nbytes + WIDTH);
        memcpy(locations, from->locations, nbytes);
        /* zero the trailing unused, for caution */
        memset(&locations[from->nr_locations], 0, WIDTH);

        for (i = 0; i < from->nr_locations; i++) {
                Object *xptr = locations[i].xptr;
                if (xptr)
                        VAR_INCR_REF(xptr);
        }

        dbg->locations          = locations;
        /* see above emalloc size */
        dbg->locations_alloc    = from->nr_locations + 1;
        dbg->nr_locations       = from->nr_locations;
        return dbg;
}

/**
 * debug_mark_error - Mark a spot where something bad happened.
 * @fr:           Frame where the error occurred.
 * @instr_offset: - If error occured in a user function, this is the
 *                  index from the base of the instructions array.
 *                - If error occured in a built-in function, this is -1.
 *                  For built-in functions, we'll have to live with a
 *                  traceback which stops at the built-in function call.
 *
 * Return: A tuple of the form ((offs1, xptr1), (offs2, xptr2)...)
 *         or NULL if state is not cleared.
 *
 * This freezes the state against future debug_mark_error() calls until
 * a call to debug_clear_error().  This is to prevent erroneous location
 * reporting; each nested call which unwinds during an exception will
 * want to mark the error, but it is only true for the deepest nested
 * call.  See where used in execute_loop().
 */
Object *
debug_mark_error(Frame *fr, ssize_t instr_offset)
{
        struct debug_stack_t *dbg;
        Object *ret;

        if (debug_error)
                return NULL;
        debug_error = true;

        /*
         * XXX REVISIT: There's a lot of redundant allocation/freeing
         * going on here.  I assume exceptions are special enough to
         * justify the slower-going, but some of these could be caught
         * in a try/catch statement sitting just a function upstream.
         */
        dbg = debug_stack_copy(&debug);
        if (instr_offset >= 0)
                debug_push_location_to(fr, instr_offset, dbg);

        ret = debug_stack_to_object(dbg);
        debug_free_stack(dbg);
        return ret;
}

/**
 * debug_clear_error - Clear the error state.
 *
 * Used whenever an exception is handled; either when it unwinds all the
 * way to the main loop and prints the error to stderr, or when it is
 * captured by a try/catch statement in execute_loop().
 */
void
debug_clear_error(void)
{
        debug_error = false;
}

void
debug_push_location(Frame *fr, size_t instr_offset)
{
        debug_push_location_to(fr, instr_offset, &debug);
}

void
debug_pop_location(void)
{
        struct debug_locations_t *loc;

        bug_on(debug.nr_locations <= 0 || !debug.locations);
        --debug.nr_locations;
        loc = &debug.locations[debug.nr_locations];
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
                while (debug.nr_locations)
                        debug_pop_location();
                efree(debug.locations);
                debug.locations = NULL;
                debug.locations_alloc = 0;
        }
        if (debug_error)
                debug_clear_error();
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
debug_print_trace(Object *dbg, FILE *fp, bool print_lines)
{
        ssize_t i, n;

        /* print nothing */
        if (!debug_error)
                return;

        if (!isvar_tuple(dbg))
                goto bail;

        n = seqvar_size(dbg);
        for (i = n - 1; i >= 0; i--) {
                Object *tup, *instr_offs, *xptr, *fname_obj;
                struct xptrvar_t *ex;
                struct location_t locations;
                const char *funcname, *filename;
                ssize_t layer = n - 1 - i;

                tup = tuple_borrowitem(dbg, i);
                if (!isvar_tuple(tup))
                        goto bail;

                instr_offs = tuple_borrowitem(tup, 0);
                xptr = tuple_borrowitem(tup, 1);
                if (!isvar_int(instr_offs))
                        goto bail;
                if (!isvar_xptr(xptr)) {
                        if (xptr != NullVar)
                                goto bail;
                        continue;
                }

                ex = (struct xptrvar_t *)xptr;
                if (location_unpack(ex->locations,
                                    ex->locations_size,
                                    intvar_toll(instr_offs),
                                    &locations) == RES_ERROR) {
                        /* anything else we print is likely unreliable */
                        goto bail;
                }

                print_trace_chars(fp, layer);

                fname_obj = ex->funcname;
                if (fname_obj)
                        funcname = string_cstring(fname_obj);
                else
                        funcname = "<anonymous>";
                if (i > 0)
                        fprintf(fp, "called from %s ", funcname);
                else
                        fprintf(fp, "in function %s ", funcname);

                filename = ex->file_name;
                if (!filename)
                        filename = "<unnamed>";
                fprintf(fp, "in file %s line %d\n", filename,
                        (int)locations.loc_startline);
                if (print_lines && filename[0] != '<') {
                        print_err_line(fp, filename,
                                       locations.loc_startline,
                                       layer);
                }
        }
        return;

bail:
        /*
         * TODO: If we're here, user may have corrupted @dbg.  We need
         * something like a --warn or --debug option to set warning level
         * and print these kinds of things to stderr.
         */
        return;
}

