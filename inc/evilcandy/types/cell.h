#ifndef EVILCANDY_TYPES_CELL_H
#define EVILCANDY_TYPES_CELL_H

#include <evilcandy/typedefs.h>

/* types/cell.c */
extern Object *cellvar_new(Object *value);
extern Object *cell_get_value(Object *cell);
extern void cell_replace_value(Object *cell, Object *new_value);

#endif /* EVILCANDY_TYPES_CELL_H */
