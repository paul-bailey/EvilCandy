/*
 * XXX: This file and its contents should eventually be renamed
 * something like "runtime" instead of "global".
 */
#ifndef EVC_INC_INTERNAL_GLOBAL_H
#define EVC_INC_INTERNAL_GLOBAL_H

#include <stdbool.h>
#include <stddef.h>
#include <typedefs.h>
#include <evcenums.h>

struct global_t {
        bool interactive;
        Object *nl;
        Object *stdout_file;
        Object *strconsts[N_STRCONST];
        Object *one;
        Object *zero;
        Object *empty_bytes;
        Object *cwd;
        Object *mns[N_MNS];
        /* c-api handles to some built-in classes */
        Object *classes[N_GBL_CLASSES];
        Object *interned_strings; /*< a set */

        /*
         * Remaining fields private to subsystems
         *
         * TODO: Change each of gbl.subsys.xxxx to pointers;
         * xxx_[de]init_gbl() will then allocate/free them, and that way
         * the struct definitions can be local to the files which
         * manipulate them.  They will not need to include this header
         * directly.
         */

        struct {
                /* token.c manages this */
                struct gbl_token_subsys_t {
                        /*
                         * token.c, interactive-mode saved line.  Needed
                         * for occasions where more than one statement
                         * are typed on the same line.
                         */
                        char *line;
                        char *s;
                        int lineno;
                        size_t _slen;
                } token;

                /* codec.c manages this */
                struct gbl_codec_subsys_t {
                        /* maps codec to int obj */
                        Object *codecs[N_CODEC];
                } codec;

                /* err.c manages this */
                struct gbl_err_subsys_t {
                        Object *exception_last;
                } err;

                /* import.c manages this */
                struct gbl_import_subsys_t {
                        Object *import_dict;
                } import;
        } subsys;
};

extern struct global_t gbl;

#endif /* EVC_INC_INTERNAL_GLOBAL_H */
