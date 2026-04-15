#include <internal/global.h>
#include <internal/codec.h>
#include <internal/type_registry.h>
#include <internal/types/string.h>
#include <evilcandy/global.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/types/dict.h>
#include <evilcandy/types/number_types.h>

/* runtime state struct */
struct gbl_codec_subsys_t {
        /* maps codec to int obj */
        Object *codecs[N_CODEC];
};

/* called from global.c */
struct gbl_codec_subsys_t *
codec_init_gbl(void)
{
        int i;
        struct gbl_codec_subsys_t *subsys = emalloc(sizeof(*subsys));
        memset(subsys, 0, sizeof(*subsys));
        for (i = 0; i < N_CODEC; i++)
                subsys->codecs[i] = intvar_new(i);
        return subsys;
}

void
codec_deinit_gbl(struct gbl_codec_subsys_t *subsys)
{
        int i;
        for (i = 0; i < N_CODEC; i++) {
                if (subsys->codecs[i])
                        VAR_DECR_REF(subsys->codecs[i]);
        }
        efree(subsys);
}


char *
codec_str(int codec, char *buf, size_t size)
{
        const char *ret = "?";
        Object *str = codec_strobj(codec);
        if (str) {
                if (isvar_string(str))
                        ret = string_cstring(str);
                VAR_DECR_REF(str);
        }
        memset(buf, 0, size);
        strncpy(buf, ret, size-1);
        return buf;
}

Object *
codec_strobj(int codec)
{
        Object *value;
        Object *codec_dict = gbl_borrow_mns_dict(MNS_CODEC);
        struct gbl_codec_subsys_t *gcs = gbl_get_codec_subsys();
        if (codec >= N_CODEC
            || gcs->codecs[codec] == NULL
            || codec_dict == NULL) {
                return NULL;
        }
        value = dict_getitem(codec_dict, gcs->codecs[codec]);
        if (!value || !isvar_string(value))
                return NULL;

        return value;
}

