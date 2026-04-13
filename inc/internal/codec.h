#ifndef EVC_INC_INTERNAL_CODEC_H
#define EVC_INC_INTERNAL_CODEC_H

#include <stddef.h>
#include <typedefs.h>

struct gbl_codec_subsys_t;

extern char *codec_str(int codec, char *buf, size_t size);
extern Object *codec_strobj(int codec);
extern void codec_init_gbl(struct gbl_codec_subsys_t *subsys);
extern void codec_deinit_gbl(struct gbl_codec_subsys_t *subsys);

#endif /* EVC_INC_INTERNAL_CODEC_H */
