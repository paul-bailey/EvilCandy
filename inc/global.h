#ifndef EVILCANDY_GLOBAL_H
#define EVILCANDY_GLOBAL_H

struct global_t {
        struct {
                bool disassemble;
                bool disassemble_only;
                bool disassemble_minimum;
                char *disassemble_outfile;
                char *infile;
        } opt;
        Object *nl;
        Object *stdout_file;
        Object *strconsts[N_STRCONST];
        Object *neg_one;
        Object *one;
        Object *zero;
        Object *eight;
        Object *empty_bytes;
        Object *spc_bytes;
        Object *fzero;
        Object *cwd;
        Object *mns[N_MNS];
        Object *codecs[N_CODEC]; /* maps codec to int obj */

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
};

#define STRCONST_ID(X)    (gbl.strconsts[STRCONST_IDX_##X])

/*
 * main.c
 */
extern struct global_t gbl;
extern Object *ErrorVar;
extern Object *NullVar;
extern Object *GlobalObject;

extern Object *ArgumentError;
extern Object *KeyError;
extern Object *IndexError;
extern Object *NameError;
extern Object *NotImplementedError;
extern Object *NumberError;
extern Object *RangeError;
extern Object *RecursionError;
extern Object *RuntimeError;
extern Object *SyntaxError;
extern Object *SystemError;
extern Object *TypeError;
extern Object *ValueError;


#endif /* EVILCANDY_GLOBAL_H */
