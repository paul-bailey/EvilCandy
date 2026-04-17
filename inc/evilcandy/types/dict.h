#ifndef EVILCANDY_TYPES_DICT_H
#define EVILCANDY_TYPES_DICT_H

#include <evilcandy/typedefs.h>
#include <evilcandy/enums.h>
#include <stdbool.h>

struct type_method_t;

/* types/dict.c */
extern Object *dictvar_new(void);
extern Object *dictvar_from_methods(Object *parent,
                                    const struct type_method_t *tbl,
                                    bool bind);
extern Object *dict_keys(Object *obj, bool sorted);
extern Object *dict_getitem(Object *o, Object *key);
extern Object *dict_getitem_cstr(Object *o, const char *cstr_key);
extern enum result_t dict_setitem(Object *o, Object *key, Object *attr);
extern void dict_add_to_globals(Object *obj);
extern enum result_t dict_setitem_replace(Object *dict,
                                Object *key, Object *attr);
extern enum result_t dict_setitem_exclusive(Object *dict,
                                Object *key, Object *attr);
extern int dict_copyto(Object *to, Object *from);

#endif /* EVILCANDY_TYPES_DICT_H */
