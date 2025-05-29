/*
 * Placeholders until I can auto-generate this file from UnicodeData.txt
 * Thus far they only manage ASCII chars.
 */
#include <evilcandy.h>
#include <ctype.h>

bool evc_isalnum(unsigned long c) { return c < 128 && isalnum(c); }
bool evc_isalpha(unsigned long c) { return c < 128 && isalpha(c); }
bool evc_isdigit(unsigned long c) { return c < 128 && isdigit(c); }
bool evc_isprint(unsigned long c) { return c < 128 && isprint(c); }
bool evc_isspace(unsigned long c) { return c < 128 && isspace(c); }
bool evc_isupper(unsigned long c) { return c < 128 && isupper(c); }
bool evc_islower(unsigned long c) { return c < 128 && islower(c); }
bool evc_isgraph(unsigned long c) { return c < 128 && isgraph(c); }

unsigned long
evc_toupper(unsigned long c)
{
        return c < 128 ? toupper(c) : c;
}

unsigned long
evc_tolower(unsigned long c)
{
        return c < 128 ? tolower(c) : c;
}
