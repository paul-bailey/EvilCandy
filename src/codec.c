#include <internal/global.h>
#include <internal/codec.h>
#include <internal/type_registry.h>
#include <internal/types/string.h>
#include <evilcandy.h>

/* called from gbl.c */
void
codec_init_gbl(void)
{
        int i;
        for (i = 0; i < N_CODEC; i++)
                gbl.codecs[i] = intvar_new(i);
}

void
codec_deinit_gbl(void)
{
        int i;
        for (i = 0; i < N_CODEC; i++) {
                if (gbl.codecs[i])
                        VAR_DECR_REF(gbl.codecs[i]);
        }
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
        if (codec >= N_CODEC
            || gbl.codecs[codec] == NULL
            || codec_dict == NULL) {
                return NULL;
        }
        value = dict_getitem(codec_dict, gbl.codecs[codec]);
        if (!value || !isvar_string(value))
                return NULL;

        return value;
}

