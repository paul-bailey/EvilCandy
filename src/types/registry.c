#include <internal/type_protocol.h>
#include <internal/type_registry.h>
#include <internal/init.h>
#include <evilcandy/types/class.h>
#include <evilcandy/var.h>

/*
 * XXX need better name than "hidden".  These are only hidden insofar
 * as they do not get added to the global namespace at startup.
 */
static struct type_t *const VAR_HIDDEN_TYPES_TBL[] = {
        /* in builtins/ */
        &BinFileType,
        &RawFileType,
        &TextFileType,
        &SocketType,
        NULL,
};

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
        struct type_t *const *tbl;
        for (tbl = &VAR_TYPES_TBL[0]; *tbl != NULL; tbl++) {
                struct type_t *tp = *tbl;
                var_initialize_static((Object *)tp, &TypeType);
                type_init_builtin((Object *)tp, false, true);
        }

        for (tbl = &VAR_HIDDEN_TYPES_TBL[0]; *tbl != NULL; tbl++) {
                struct type_t *tp = *tbl;
                var_initialize_static((Object *)tp, &TypeType);
                type_init_builtin((Object *)tp, false, false);
        }
}

static void
type_deinit_builtin_static(struct type_t *tp)
{
        if (tp->bases) {
                VAR_DECR_REF(tp->bases);
                tp->bases = NULL;
        }
        if (tp->priv) {
                VAR_DECR_REF(tp->priv);
                tp->priv = NULL;
        }
        if (tp->all_priv) {
                VAR_DECR_REF(tp->all_priv);
                tp->all_priv = NULL;
        }
        if (tp->mro) {
                VAR_DECR_REF(tp->mro);
                tp->mro = NULL;
        }
        if (tp->delegate_name) {
                VAR_DECR_REF(tp->delegate_name);
                tp->delegate_name = NULL;
        }
        if (tp->methods) {
                VAR_DECR_REF(tp->methods);
                tp->methods = NULL;
        }
}

void
cfile_deinit_type_registry(void)
{
        struct type_t *const *tbl;
        for (tbl = &VAR_TYPES_TBL[0]; *tbl != NULL; tbl++) {
                type_deinit_builtin_static(*tbl);
        }
        for (tbl = &VAR_HIDDEN_TYPES_TBL[0]; *tbl != NULL; tbl++) {
                type_deinit_builtin_static(*tbl);
        }

        /*
         * Do this outside of above pass, since some vars may
         * get added to freelist on later 'i'
         */
        for (tbl = &VAR_TYPES_TBL[0]; *tbl != NULL; tbl++) {
                struct type_t *tp = *tbl;
                var_type_clear_freelist(tp);
        }
        for (tbl = &VAR_HIDDEN_TYPES_TBL[0]; *tbl != NULL; tbl++) {
                struct type_t *tp = *tbl;
                var_type_clear_freelist(tp);
        }
}


