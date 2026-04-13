#ifndef EVC_INC_INTERNAL_ERR_H
#define EVC_INC_INTERNAL_ERR_H

struct gbl_err_subsys_t;
extern void err_deinit_gbl(struct gbl_err_subsys_t *err);
extern void err_init_gbl(struct gbl_err_subsys_t *err);

#endif /* EVC_INC_INTERNAL_ERR_H */

