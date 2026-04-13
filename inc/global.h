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

#endif /* EVILCANDY_GLOBAL_H */
