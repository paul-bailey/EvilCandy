#include <internal/global.h>
#include <internal/codec.h>
#include <internal/type_registry.h>
#include <internal/types/string.h>
#include <evilcandy/global.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/types/dict.h>
#include <evilcandy/types/number_types.h>
#include <evilcandy/types/string.h>

/* runtime state struct */
struct gbl_codec_subsys_t {
        /* maps codec to int obj */
        Object *codecs[N_CODEC];
};

static void
initialize_codec_dict(void)
{
        static const struct codectbl_t {
                int e;
                const char *name;
        } codectbl[] = {
                { .e = CODEC_UTF8,   .name = "utf-8"    },
                { .e = CODEC_UTF8,   .name = "UTF-8"    },
                { .e = CODEC_UTF8,   .name = "utf8"     },
                { .e = CODEC_UTF8,   .name = "UTF8"     },
                { .e = CODEC_LATIN1, .name = "latin1"   },
                { .e = CODEC_LATIN1, .name = "Latin1"   },
                { .e = CODEC_LATIN1, .name = "LATIN1"   },
                { .e = CODEC_LATIN1, .name = "latin-1"  },
                { .e = CODEC_LATIN1, .name = "Latin-1"  },
                { .e = CODEC_LATIN1, .name = "LATIN-1"  },
                /* XXX iso-88something-something... */
                { .e = CODEC_ASCII,  .name = "ascii"    },
                { .e = CODEC_ASCII,  .name = "ASCII"    },
                { .e = -1,           .name = NULL       },
        };
        const struct codectbl_t *t;
        Object *codecs = dictvar_new();
        for (t = codectbl; t->name != NULL; t++) {
                Object *o = intvar_new(t->e);
                Object *k = stringvar_new(t->name);
                dict_setitem(codecs, k, o);

                /* Reverse key-value for some default names */
                if (!strcmp(t->name, "utf-8") ||
                    !strcmp(t->name, "Latin1") ||
                    !strcmp(t->name, "ascii")) {
                        dict_setitem(codecs, o, k);
                }

                VAR_DECR_REF(k);
                VAR_DECR_REF(o);
        }
        gbl_set_mns_dict(MNS_CODEC, codecs);
}

/*
 * called from global.c
 *
 * Performs the double-task of initailizing the code dict
 * and returning the codec subsys.
 */
struct gbl_codec_subsys_t *
codec_init_gbl(void)
{
        struct gbl_codec_subsys_t *subsys;
        int i;

        if (!gbl_borrow_mns_dict(MNS_CODEC))
                initialize_codec_dict();

        subsys = emalloc(sizeof(*subsys));
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

