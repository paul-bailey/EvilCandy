#ifndef EVILCANDY_GLOBAL_H
#define EVILCANDY_GLOBAL_H

#include <typedefs.h>
#include <stdbool.h>
#include <evcenums.h>

/* declared in main.c */
extern Object *ErrorVar;
extern Object *NullVar;
extern Object *GlobalObject;

extern Object *ArgumentError;
extern Object *KeyError;
extern Object *IndexError;
extern Object *NameError;
extern Object *NotImplementedError;
extern Object *NumberError;
extern Object *RangeError;
extern Object *RecursionError;
extern Object *RuntimeError;
extern Object *SyntaxError;
extern Object *SystemError;
extern Object *TypeError;
extern Object *ValueError;

extern Object *gbl_new_empty_bytes(void);
extern Object *gbl_new_bool(bool cond);
extern Object *gbl_borrow_bool(bool cond);
extern Object *gbl_borrow_strconst(enum evc_strconst_t id);
#define STRCONST_ID(X)    gbl_borrow_strconst(STRCONST_IDX_##X)

extern void gbl_set_interactive(bool is_interactive);
extern bool gbl_is_interactive(void);

extern Object *gbl_borrow_mns_dict(enum gbl_mns_t mns);
extern void gbl_set_mns_dict(enum gbl_mns_t mns, Object *dict);
extern Object *gbl_intern_string(Object *str);
extern Object *gbl_borrow_builtin_class(enum gbl_class_idx_t idx);
extern void gbl_set_builtin_class(enum gbl_class_idx_t idx,
                                  Object *class);

#endif /* EVILCANDY_GLOBAL_H */
