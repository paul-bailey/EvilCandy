#include <internal/type_protocol.h>
#include <internal/type_registry.h>
#include <internal/init.h>
#include <evilcandy/types/class.h>
#include <evilcandy/var.h>

static struct type_t *const VAR_TYPES_TBL[] = {
        &TypeType,
        &ArrayType,
        &TupleType,
        &EmptyType,
        &FloatType,
        &ComplexType,
        &FunctionType,
        &MethodType,
        &IntType,
        &XptrType,
        &DictType,
        &StringType,
        &BytesType,
        &PropertyType,
        &RangeType,
        &SetType,
        &UuidptrType,
        &IdType,
        &CellType,

        /* in builtins/ */
        &BinFileType,
        &RawFileType,
        &TextFileType,
        &SocketType,

        /* the iterators */
        &ArrayIterType,
        &BytesIterType,
        &DictIterType,
        &TupleIterType,
        &SetIterType,
        &RangeIterType,
        &StringIterType,

        /* special extra iters */
        &DictItemsType,
        &DictItemsIterType,
        NULL,
};

void
cfile_init_type_registry(void)
{
        int i;
        for (i = 0; VAR_TYPES_TBL[i] != NULL; i++) {
                struct type_t *tp = VAR_TYPES_TBL[i];
                var_initialize_static((Object *)tp, &TypeType);
                type_init_builtin((Object *)tp, false);
        }

}

void
cfile_deinit_type_registry(void)
{
        int i;
        for (i = 0; VAR_TYPES_TBL[i] != NULL; i++) {
                struct type_t *tp = VAR_TYPES_TBL[i];
                VAR_DECR_REF(tp->methods);
                tp->methods = NULL;
        }

        /*
         * Do this outside of above pass, since some vars may
         * get added to freelist on later 'i'
         */
        for (i = 0; VAR_TYPES_TBL[i] != NULL; i++) {
                struct type_t *tp = VAR_TYPES_TBL[i];
                var_type_clear_freelist(tp);
        }
}


