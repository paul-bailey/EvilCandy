#include "var.h"
#include <stdlib.h>

/**
 * type_handle_new - allocate a type-specific handle.
 * @size: Size of the handle
 * @destructor: Callback to destroy the handle (short of freeing it,
 *      don't do that)
 *
 * Return: pointer to new handle.  Do not free this.  Instead use
 * TYPE_HANDLE_INCR_REF and TYPE_HANDLE_DECR_REF, let that take care
 * of the GC.
 */
void *
type_handle_new(size_t size, void (*destructor)(void *))
{
        struct type_handle_preheader_t_ *ph = ecalloc(sizeof(*ph) + size);
        ph->nref = 1;
        ph->destructor = destructor;
        return (void *)(ph + 1);
}

/*
 * wrapped by TYPE_HANDLE_DECR_REF - call h's destructor if reference
 * counter goes down to zero.
 */
void
type_handle_destroy__(struct type_handle_preheader_t_ *ph)
{
        if (ph->destructor)
                ph->destructor((void *)(ph + 1));
        free(ph);
}

