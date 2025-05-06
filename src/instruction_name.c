/*
 * instruction_name.c - Get an instruction name from an enum or vice-versa
 */
#include <evilcandy.h>

static const char *INSTR_NAMES[N_INSTR] = {
#include "disassemble_gen.c.h"
};

static Object *instr_dict = NULL;

static void
initdict(void)
{
        int i;
        instr_dict = dictvar_new();
        for (i = 0; i < N_INSTR; i++) {
                Object *key, *value;
                const char *s = INSTR_NAMES[i];
                if (!s)
                        continue;
                key = stringvar_new(s);
                value = intvar_new(i);
                dict_setitem(instr_dict, key, value);
                VAR_DECR_REF(key);
                VAR_DECR_REF(value);
        }
}

const char *
instruction_name(int opcode)
{
        bug_on((unsigned)opcode >= N_INSTR);
        return INSTR_NAMES[opcode];
}

/* return INSTR_xxx enum or -1 if @name does not match */
int
instruction_from_name(const char *name)
{
        Object *key;
        int ival;
        if (!instr_dict)
                initdict();
        key = stringvar_new(name);
        ival = instruction_from_key(key);
        VAR_DECR_REF(key);
        return ival;
}

/* return INSTR_xxx enum or -1 if @key does not match */
int
instruction_from_key(Object *key)
{
        Object *v;
        int ival;
        if (!instr_dict)
                initdict();
        v = dict_getitem(instr_dict, key);
        if (v == NULL)
                return -1;
        bug_on(!isvar_int(v));
        ival = intvar_toi(v);
        bug_on(err_occurred() || ival < 0 || ival >= N_INSTR);
        return ival;
}

/*
 * This function exists just to provide symmetry with
 * moduledeinit_instruction_name
 */
void
moduleinit_instruction_name(void)
{
        ;
}

void
moduledeinit_instruction_name(void)
{
        if (instr_dict)
                VAR_DECR_REF(instr_dict);
}

