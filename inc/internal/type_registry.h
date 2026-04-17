#ifndef EVC_INC_INTERNAL_TYPE_REGISTRY_H
#define EVC_INC_INTERNAL_TYPE_REGISTRY_H

#include <internal/type_protocol.h>
#include <evilcandy/var.h>

/*
 * Declared in type C modules in types/xxx.c
 * Only put these here and give them extern linkage if they are meaningful
 * outside of whatever module that uses them.  Otherwise, keep them local
 * to the module so the namespace doesn't get cluttered up.
 */
extern struct type_t TypeType;
extern struct type_t ArrayType;
extern struct type_t TupleType;
extern struct type_t EmptyType; /* XXX should be NullType */
extern struct type_t FloatType;
extern struct type_t ComplexType;
extern struct type_t FunctionType;
extern struct type_t MethodType;
extern struct type_t IntType;
extern struct type_t XptrType;
extern struct type_t DictType;
extern struct type_t StringType;
extern struct type_t BytesType;
extern struct type_t PropertyType;
extern struct type_t RangeType;
extern struct type_t UuidptrType;
extern struct type_t IdType;
extern struct type_t SetType;
extern struct type_t CellType;

/* in builtins/ */
extern struct type_t BinFileType;
extern struct type_t RawFileType;
extern struct type_t TextFileType;
extern struct type_t SocketType;

/* iterators */
extern struct type_t ArrayIterType;
extern struct type_t BytesIterType;
extern struct type_t DictIterType;
extern struct type_t TupleIterType;
extern struct type_t SetIterType;
extern struct type_t RangeIterType;
extern struct type_t StringIterType;
extern struct type_t GeneratorType;

/* special-purpose iterators */
extern struct type_t DictItemsType;
extern struct type_t DictItemsIterType;

static inline bool isvar_array(Object *v)
        { return v->v_type == &ArrayType; }
static inline bool isvar_tuple(Object *v)
        { return v->v_type == &TupleType; }
static inline bool isvar_empty(Object *v)
        { return v->v_type == &EmptyType; }
static inline bool isvar_float(Object *v)
        { return v->v_type == &FloatType; }
static inline bool isvar_complex(Object *v)
        { return v->v_type == &ComplexType; }
static inline bool isvar_function(Object *v)
        { return v->v_type == &FunctionType; }
static inline bool isvar_method(Object *v)
        { return v->v_type == &MethodType; }
static inline bool isvar_int(Object *v)
        { return v->v_type == &IntType; }
static inline bool isvar_xptr(Object *v)
        { return v->v_type == &XptrType; }
static inline bool isvar_dict(Object *v)
        { return v->v_type == &DictType; }
static inline bool isvar_string(Object *v)
        { return v->v_type == &StringType; }
static inline bool isvar_bytes(Object *v)
        { return v->v_type == &BytesType; }
static inline bool isvar_range(Object *v)
        { return v->v_type == &RangeType; }
static inline bool isvar_uuidptr(Object *v)
        { return v->v_type == &UuidptrType; }
extern bool isvar_file(Object *v); /*< builtin/io.c */
static inline bool isvar_property(Object *v)
        { return v->v_type == &PropertyType; }
static inline bool isvar_set(Object *v)
        { return v->v_type == &SetType; }
static inline bool isvar_instance(Object *o)
        { return !!(o->v_type->flags & OBF_GP_INSTANCE); }
static inline bool isvar_generator(Object *obj)
        { return obj->v_type == &GeneratorType; }
static inline bool isvar_cell(Object *obj)
        { return obj->v_type == &CellType; }
static inline bool isvar_type(Object *obj)
        { return obj->v_type == &TypeType; }

static inline bool isvar_number(Object *v)
        { return !!(v->v_type->flags & OBF_NUMBER); }
static inline bool isvar_real(Object *v)
        { return !!(v->v_type->flags & OBF_REAL); }
static inline bool isvar_seq(Object *v)
        { return v->v_type->sqm != NULL; }
static inline bool isvar_seq_readable(Object *v)
        { return isvar_seq(v) && v->v_type->sqm->getitem != NULL; }
static inline bool isvar_map(Object *v)
        { return v->v_type->mpm != NULL; }
static inline bool hasvar_len(Object *v)
        { return v->v_type->get_iter != NULL; }


#endif /* EVC_INC_INTERNAL_TYPE_REGISTRY_H */
