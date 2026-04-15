#include <evilcandy.h>
#include <evilcandy/types/cell.h>
#include <internal/type_registry.h>

struct cellvar_t {
        Object base;
        Object *cell_value;
};

#define V2C(obj)        ((struct cellvar_t *)(obj))

static void
cell_reset(Object *cell)
{
        struct cellvar_t *cv = V2C(cell);
        bug_on(!isvar_cell(cell));
        bug_on(!cv->cell_value);
        VAR_DECR_REF(cv->cell_value);
        cv->cell_value = NULL;
}

Object *
cellvar_new(Object *value)
{
        Object *cell = var_new(&CellType);
        V2C(cell)->cell_value = VAR_NEW_REF(value);
        return cell;
}

Object *
cell_get_value(Object *cell)
{
        bug_on(!isvar_cell(cell));
        return VAR_NEW_REF(V2C(cell)->cell_value);
}

void
cell_replace_value(Object *cell, Object *new_value)
{
        struct cellvar_t *cv = V2C(cell);
        bug_on(!isvar_cell(cell));
        bug_on(!cv->cell_value);
        VAR_DECR_REF(cv->cell_value);
        cv->cell_value = VAR_NEW_REF(new_value);
}

struct type_t CellType = {
        .flags          = 0,
        .name           = "closure_cell",
        .opm            = NULL,
        .sqm            = NULL,
        .size           = sizeof(struct cellvar_t),
        .str            = NULL,
        .cmp            = NULL,
        .cmpz           = NULL,
        .cmpeq          = NULL,
        .reset          = cell_reset,
        .prop_getsets   = NULL,
        .create         = NULL,
        .hash           = NULL,
        .iter_next      = NULL,
        .get_iter       = NULL,
};
