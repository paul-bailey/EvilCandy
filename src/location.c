/*
 * location.c - Figure out where we're at in an input script,
 *                 for error reporting messages.
 *
 * We have three stages of input processing, where we could
 * encounter an input error:
 *
 * o Tokenizing (lex.c)
 * o Assembling (assembler.c)
 * o Executing (vm.c)
 *
 * Each stage has its own way of figuring out where the erorr occured.
 * We can't naively have each stage set a global function pointer to
 * their method, because these stages are (or at least *ought to be*)
 * reentrant, eg. if we encounter "load", we'll go from executing back
 * into assembling and then executing a different script.
 *
 * So we use a stack.
 *
 * Before entering a state, call:
 *         getloc_push(your_function, your_private_data)
 *
 * When leaving the state, call
 *         getloc_pop()
 *
 * There may be some cases where we throw a syntax error in the
 * transitory state.  That should be a bug, because the only errors
 * we encountered then should call fail(), not syntax().
 */

#include <evilcandy.h>

#define GETLOC_STACK_DEPTH 256

typedef unsigned int (*getloc_t)(const char **, void *);

static struct getloc_t {
        getloc_t getloc;
        void *data;
} getloc_stack[GETLOC_STACK_DEPTH];
static unsigned int getloc_stackptr = 0;

static getloc_t cur_getloc = NULL;
static void *cur_locdata = NULL;

/**
 * getloc_push - Push handle to get location
 * @getloc:     Function to get location.  First arg is for retrieving
 *              file name.  Second arg will be @data
 * @data:       Private data to pass to @getloc.  This must persist and
 *              remain relevant in memory until the parallel call to
 *              getloc_pop
 */
void
getloc_push(unsigned int (*getloc)(const char **, void *), void *data)
{
        if (getloc_stackptr >= GETLOC_STACK_DEPTH)
                syntax("Recursion overrun");

        getloc_stack[getloc_stackptr].getloc = cur_getloc;
        getloc_stack[getloc_stackptr].data   = cur_locdata;

        getloc_stackptr++;

        cur_getloc = getloc;
        cur_locdata = data;
}

/**
 * getloc_pop - Pop the last handle to get location
 */
void
getloc_pop(void)
{
        bug_on(getloc_stackptr <= 0);
        getloc_stackptr--;
}

/**
 * get_location - Get location of current input processing state
 * @file_name: Pointer to variable to store file name
 *
 * Return: Line number
 *
 * Used by err.c for error reporting.
 */
unsigned int
get_location(const char **file_name)
{
        if (cur_getloc)
                return cur_getloc(file_name, cur_locdata);

        if (file_name)
                *file_name = NULL;
        return 0;
}
