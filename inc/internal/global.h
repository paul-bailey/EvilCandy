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
        /* maps codec to int obj */
        Object *codecs[N_CODEC];
        /* c-api handles to some built-in classes */
        Object *classes[N_GBL_CLASSES];
        Object *interned_strings; /*< a set */

        /*
         * private fields, put in global struct just so cleanup
         * can be done without having to call a ton of little
         * cleanup functions throughout the source tree.
         */
        struct {
                /* token.c, interactive-mode saved line */
                char *line;
                char *s;
                int lineno;
                size_t _slen;
        } iatok;

        /* err.c manages this */
        Object *exception_last;

        /* import.c */
        Object *import_dict;
};

extern struct global_t gbl;

#endif /* EVC_INC_INTERNAL_GLOBAL_H */
