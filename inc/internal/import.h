#ifndef EVC_INC_INTERNAL_IMPORT_H
#define EVC_INC_INTERNAL_IMPORT_H

struct gbl_import_subsys_t;
extern struct gbl_import_subsys_t *import_init_gbl(void);
extern void import_deinit_gbl(struct gbl_import_subsys_t *subsys);

#endif /* EVC_INC_INTERNAL_IMPORT_H */

