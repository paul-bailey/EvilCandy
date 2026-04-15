#include <evilcandy/config.h>
#include <evilcandy/ewrappers.h>
#include <internal/builtin/uuid.h>

#include <stddef.h> /*< NULL definition */

#if defined(HAVE_UUID_H) || defined(HAVE_UUID_UUID_H)
# ifdef HAVE_UUID_H
#  include <uuid.h>
# elif defined(HAVE_UUID_UUID_H)
#  include <uuid/uuid.h>
# endif

#define UUID_STR_LEN 100

/*
 * TODO: Change these CAPI functions (which none of this source tree
 * actually uses) into more useful UAPI functions.
 */

/**
 * uuidstr - Get a UUID string.
 *
 * Return: A (hypothetically) different string for every call.
 */
char *
uuidstr(void)
{
        char buf[UUID_STR_LEN];
        uuid_t b;
        uuid_generate(b);
        uuid_unparse_lower(b, buf);
        return estrdup(buf);
}
#else
char *
uuidstr(void)
{
        return NULL;
}
#endif
