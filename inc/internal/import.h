#ifndef EVC_INC_INTERNAL_IMPORT_H
#define EVC_INC_INTERNAL_IMPORT_H

#include <evilcandy/typedefs.h>

/* import.c */
struct gbl_import_subsys_t;
extern struct gbl_import_subsys_t *import_init_gbl(void);
extern void import_deinit_gbl(struct gbl_import_subsys_t *subsys);
extern Object *evc_import(Frame *fr, const char *file_name);

#endif /* EVC_INC_INTERNAL_IMPORT_H */

