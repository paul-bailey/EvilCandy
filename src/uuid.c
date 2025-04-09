#include <evilcandy.h>
/* TODO: config.h stuff, not every platform has this */
#include <uuid/uuid.h>

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
