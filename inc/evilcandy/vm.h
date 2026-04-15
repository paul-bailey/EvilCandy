/* vm.h - API with vm.c functions */
#ifndef EVILCANDY_VM_H
#define EVILCANDY_VM_H

#include <stdbool.h>
#include <evilcandy/typedefs.h>
#include <evilcandy/enums.h>

/* vm.c */
extern Object *vm_exec_script(Object *top_level, Frame *fr);
extern Object *vm_exec_func(Frame *fr, Object *func,
                            Object *args, Object *kwargs);
extern void vm_add_global(Object *name, Object *var);
extern bool vm_symbol_exists(Object *key);
extern Object *vm_get_this(Frame *fr);

/* vm_getargs.c */
extern enum result_t vm_getargs(Frame *fr, const char *fmt, ...);
extern enum result_t vm_getargs_sv(Object *sv, const char *fmt, ...);
#define VM_REFUSE_ARGS(fr_, fname_) vm_getargs((fr_), "[!]{!}:" fname_)

#endif /* EVILCANDY_VM_H */
