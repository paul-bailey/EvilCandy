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
        Object *one;
        Object *zero;
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
