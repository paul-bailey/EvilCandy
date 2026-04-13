#ifndef EVC_INC_INTERNAL_INIT_H
#define EVC_INC_INTERNAL_INIT_H

extern void initialize_program(void);
extern void end_program(void);

/* constructors/destructors for certain C file */
/* global.c */
extern void cfile_init_global(void);
extern void cfile_deinit_global(void);
/* ewrappers.c */
extern void cfile_init_ewrappers(void);
/* var.c */
extern void cfile_init_var(void);
extern void cfile_deinit_var(void);
/* vm.c */
extern void cfile_init_vm(void);
extern void cfile_deinit_vm(void);

/* constructors/destructors for built-in modules */
/* builtin/builtin.c */
extern void moduleinit_builtin(void);
/* builtin/math.c */
extern void moduleinit_math(void);
/* builtin/io.c */
extern void moduleinit_io(void);
/* builtin/socket.c */
extern void moduleinit_socket(void);
/* builtin/sys.c */
extern void moduleinit_sys(void);

#endif /* EVC_INC_INTERNAL_INIT_H */

