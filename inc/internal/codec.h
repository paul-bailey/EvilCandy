#ifndef EVC_INC_INTERNAL_CODEC_H
#define EVC_INC_INTERNAL_CODEC_H

#include <stddef.h>
#include <typedefs.h>

extern char *codec_str(int codec, char *buf, size_t size);
extern Object *codec_strobj(int codec);
extern void codec_init_gbl(void);
extern void codec_deinit_gbl(void);

#endif /* EVC_INC_INTERNAL_CODEC_H */
