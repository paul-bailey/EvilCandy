/*
 * XXX: This file and its contents should eventually be renamed
 * something like "runtime" instead of "global".
 */
#ifndef EVC_INC_INTERNAL_GLOBAL_H
#define EVC_INC_INTERNAL_GLOBAL_H

extern struct gbl_token_subsys_t *gbl_get_token_subsys(void);
extern struct gbl_codec_subsys_t *gbl_get_codec_subsys(void);
extern struct gbl_import_subsys_t *gbl_get_import_subsys(void);
extern struct gbl_err_subsys_t *gbl_get_err_subsys(void);

#endif /* EVC_INC_INTERNAL_GLOBAL_H */
