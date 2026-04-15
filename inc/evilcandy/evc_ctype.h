#ifndef EVC_INC_EVC_CTYPE_H
#define EVC_INC_EVC_CTYPE_H

#include <stdbool.h>

/* ctype.c */
extern bool evc_isalnum(unsigned long c);
extern bool evc_isalpha(unsigned long c);
extern bool evc_isdigit(unsigned long c);
extern bool evc_isprint(unsigned long c);
extern bool evc_isspace(unsigned long c);
extern bool evc_isupper(unsigned long c);
extern bool evc_islower(unsigned long c);
extern bool evc_isgraph(unsigned long c);
extern unsigned long evc_toupper(unsigned long c);
extern unsigned long evc_tolower(unsigned long c);
static inline bool evc_isascii(unsigned long c) { return c < 128; }

#endif /* EVC_INC_EVC_CTYPE_H */
