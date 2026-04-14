#ifndef EVC_INC_INTERNAL_ERR_H
#define EVC_INC_INTERNAL_ERR_H

struct gbl_err_subsys_t;
extern void err_deinit_gbl(struct gbl_err_subsys_t *err);
extern struct gbl_err_subsys_t *err_init_gbl(void);

#endif /* EVC_INC_INTERNAL_ERR_H */

