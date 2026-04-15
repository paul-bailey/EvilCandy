#ifndef EVILCANDY_TYPES_STRING_H
#define EVILCANDY_TYPES_STRING_H

#include <evilcandy/typedefs.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* types/string.c */
extern Object *stringvar_new(const char *cstr);
extern Object *stringvar_newn(const char *cstr, size_t n);
extern Object *stringvar_from_source(const char *tokenstr, bool imm);
extern Object *stringvar_from_binary(const void *data, size_t n, int encoding);
extern Object *stringvar_from_format(const char *fmt, ...);
extern Object *stringvar_from_vformat(const char *fmt, va_list ap);
extern Object *stringvar_from_ascii(const char *cstr);
extern Object *stringvar_from_substr(Object *old, size_t start, size_t stop);
extern long string_ord(Object *str, size_t idx);
extern Object *string_format(Object *str, Object *tup);
extern Object *string_cat(Object *romeo, Object *juliet);
extern size_t string_slide(Object *str, Object *sep, size_t startpos);
extern bool string_chr(Object *str, long pt);
extern ssize_t string_search(Object *haystack, Object *needle, size_t startpos);
extern char *string_encode_utf8(Object *str, size_t *size);


#endif /* EVILCANDY_TYPES_STRING_H */
