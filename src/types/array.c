/* array.c - Code for managing numerical arrays */
#include "var.h"
#include <stdlib.h>

/**
 * struct array_handle_t - Handle to a numerical array
 * @nref:       Number of variables with access to this array
 *              Used for garbage collection
 * @type:       type of data stored in the array, a Q*_MAGIC enum
 * @nmemb:      Size of the array, in number of elements
 * @allocsize:  Size of the array, in number of bytes currently allocated
 *              for it
 * @datasize:   Size of each member of the array (so we don't have to
 *              keep figuring it out from @type all the time)
 */
struct array_handle_t {
        int nref;
        int type;
        unsigned int nmemb;
        struct buffer_t children;
};

static void
check_type_match(struct array_handle_t *h, struct var_t *child)
{
        if (h->type != child->magic) {
                syntax("Trying to add type '%s' to '%s' array",
                        typestr(child->magic), h->type);
        }
}

static struct array_handle_t *
array_handle_new(void)
{
        struct array_handle_t *ret = ecalloc(sizeof(*ret));
        ret->type = QEMPTY_MAGIC;
        buffer_init(&ret->children);
        ret->nref = 1;
        return ret;
}

/**
 * array_child - Get nth member of an array
 * @array: Array to seek
 * @idx:   Index into the array
 * @child: Variable to store the result, which must be permitted
 *         to receive it (ie. it ought to be QEMPTY_MAGIC)
 *
 * Return 0 for success, -1 for failure
 *
 */
struct var_t *
array_child(struct var_t *array, int idx)
{
        struct array_handle_t *h = array->a;
        struct var_t **ppvar = (struct var_t **)h->children.s;

        idx = index_translate(idx, h->nmemb);
        if (idx < 0)
                return NULL;

        return ppvar[idx];
}

/**
 * array_set_child - Set the value of an array member
 * @array:      Array to operate on
 * @idx:        Index into the array
 * @child:      Variable storing the data to set in array
 *
 * Data is moved from @child to the array member, @child must still be
 * dealt with w/r/t garbage collection by calling code.
 */
int
array_set_child(struct var_t *array, int idx, struct var_t *child)
{
        struct var_t *memb = array_child(array, idx);
        if (!memb)
                return -1;
        qop_mov(memb, child);
        return 0;
}

/**
 * array_add_child - Append a new element to an array
 * @array:  Array to add to
 * @child:  New element to put in array
 *
 * @child may not be an empty variable.  It must be the same type as the
 * other variables stored in @array.  If this is the first call to
 * array_add_child for this array, then the array's type will be locked
 * to @child->magic.
 */
void
array_add_child(struct var_t *array, struct var_t *child)
{
        struct array_handle_t *h = array->a;
        if (child->magic == QEMPTY_MAGIC)
                syntax("You may not add an empty var to array");
        if (h->type == QEMPTY_MAGIC) {
                /* first time, set type and assign datasize */
                bug_on(h->nmemb != 0);
                h->type = child->magic;
        }

        check_type_match(h, child);

        h->nmemb++;
        buffer_putd(&h->children, &child, sizeof(void *));
}

/**
 * array_from_empty - Turn an empty variable into a new array
 * @array: Variable to turn into an array
 *
 * Return: @array, always
 *
 * This will create a new array handle
 */
struct var_t *
array_from_empty(struct var_t *array)
{
        bug_on(array->magic != QEMPTY_MAGIC);
        array->magic = QARRAY_MAGIC;

        array->a = array_handle_new();
        return array;
}

static void
array_reset(struct var_t *a)
{
        a->a->nref--;
        if (a->a->nref <= 0) {
                bug_on(a->a->nref < 0);
                buffer_free(&a->a->children);
                free(a->a);
        }
        a->a = NULL;
}

static void
array_mov(struct var_t *to, struct var_t *from)
{
        if (from->magic != QARRAY_MAGIC) {
                syntax("Cannot change type from array to %s",
                       typestr(from->magic));
        }
        to->a = from->a;
        to->a->nref++;
}

static const struct operator_methods_t array_primitives = {
        /* To do, I may want to support some of these */
        .mov = array_mov,
        .reset = array_reset,
};

static void
array_len(struct var_t *ret)
{
        struct var_t *self = get_this();
        bug_on(self->magic != QARRAY_MAGIC);
        qop_assign_int(ret, self->a->nmemb);
}

static const struct type_inittbl_t array_methods[] = {
        V_INITTBL("len",        array_len,      0, 0),
        TBLEND,
};

void
typedefinit_array(void)
{
        var_config_type(QARRAY_MAGIC, "list",
                        &array_primitives, array_methods);
}
