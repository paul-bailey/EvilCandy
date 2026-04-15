#ifndef EVC_INC_DISASSEMBLE_H
#define EVC_INC_DISASSEMBLE_H

/* disassemble.c */
extern void disassemble(FILE *fp, Object *ex,
                        const char *sourcefile_name);
extern void disassemble_lite(FILE *fp, Object *ex);
extern void disassemble_minimal(FILE *fp, Object *ex);

#endif /* EVC_INC_DISASSEMBLE_H */
