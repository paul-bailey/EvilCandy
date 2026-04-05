#include <evilcandy.h>
/* TODO: config.h stuff, not every platform has this */
#if defined(HAVE_UUID_H) || defined(HAVE_UUID_H)
# ifdef HAVE_UUID_H
#  include <uuid.h>
# elif defined(HAVE_UUID_UUID_H)
#  include <uuid/uuid.h>
# endif


#define UUID_STR_LEN 100

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
        fail("UUID not implemented");
        return NULL;
}
#endif
