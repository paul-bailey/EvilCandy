/*
 * instruction_name.c - Get an instruction name from an enum or vice-versa
 */
#include <evilcandy.h>

static const char *INSTR_NAMES[N_INSTR] = {
#include "disassemble_gen.c.h"
};

static Object *
assert_dict_init(void)
{
        Object *ret = gbl.mns[MNS_INSNAME];
        if (!ret) {
                int i;
                ret = dictvar_new();
                for (i = 0; i < N_INSTR; i++) {
                        Object *key, *value;
                        const char *s = INSTR_NAMES[i];
                        if (!s)
                                continue;
                        key = stringvar_new(s);
                        value = intvar_new(i);
                        dict_setitem(ret, key, value);
                        VAR_DECR_REF(key);
                        VAR_DECR_REF(value);
                }
                gbl.mns[MNS_INSNAME] = ret;
        }

        return ret;

}

const char *
instruction_name(int opcode)
{
        bug_on((unsigned)opcode >= N_INSTR);
        return INSTR_NAMES[opcode];
}

/* return INSTR_xxx enum or -1 if @key does not match */
int
instruction_from_key(Object *key)
{
        Object *v, *dict;
        int ival;

        dict = assert_dict_init();
        v = dict_getitem(dict, key);
        if (v == NULL)
                return -1;
        bug_on(!isvar_int(v));
        ival = intvar_toi(v);
        bug_on(err_occurred() || ival < 0 || ival >= N_INSTR);
        return ival;
}

