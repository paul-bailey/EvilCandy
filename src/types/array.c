/* array.c - Code for managing numerical arrays */
#include "var.h"
#include <stdlib.h>

/**
 * struct array_handle_t - Handle to a numerical array
 * @type:       type of data stored in the array, a TYPE_* enum
 * @lock:       lock to prevent add/remove during foreach
 * @nmemb:      Size of the array, in number of elements
 * @allocsize:  Size of the array, in number of bytes currently allocated
 *              for it
 * @datasize:   Size of each member of the array (so we don't have to
 *              keep figuring it out from @type all the time)
 */
struct array_handle_t {
        int type, lock;
        unsigned int nmemb;
        struct buffer_t children;
};

static void
array_handle_reset(void *arr)
{
        struct array_handle_t *ah = (struct array_handle_t *)arr;
        buffer_free(&ah->children);
}

static struct array_handle_t *
array_handle_new(void)
{
        struct array_handle_t *ret = type_handle_new(sizeof(*ret),
                                                     array_handle_reset);
        ret->type = TYPE_EMPTY;
        buffer_init(&ret->children);
        return ret;
}

static void
array_type_err(struct array_handle_t *h, struct var_t *would_be_child)
{
        err_setstr(RuntimeError,
                   "You man not add type %s to %s array",
                   typestr(would_be_child), typestr_(h->type));
}

/**
 * array_child - Get nth member of an array
 * @array: Array to seek
 * @idx:   Index into the array
 * @child: Variable to store the result, which must be permitted
 *         to receive it (ie. it ought to be TYPE_EMPTY)
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
enum result_t
array_set_child(struct var_t *array, int idx, struct var_t *child)
{
        struct var_t *memb;

        if (array->a->lock) {
                err_locked();
                return RES_ERROR;
        }

        memb = array_child(array, idx);
        if (!memb)
                return RES_ERROR;
        if (!qop_mov(memb, child))
                return RES_ERROR;
        return RES_OK;
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
enum result_t
array_add_child(struct var_t *array, struct var_t *child)
{
        struct array_handle_t *h = array->a;

        if (h->lock) {
                err_locked();
                return RES_ERROR;
        }
        if (child->magic == TYPE_EMPTY) {
                err_setstr(RuntimeError, "You may not add an empty var to array");
                return RES_ERROR;
        }
        if (h->type == TYPE_EMPTY) {
                /* first time, set type and assign datasize */
                bug_on(h->nmemb != 0);
                h->type = child->magic;
        }

        if (h->type != child->magic) {
                array_type_err(h, child);
                return RES_ERROR;
        }

        h->nmemb++;
        VAR_INCR_REF(child);
        buffer_putd(&h->children, &child, sizeof(void *));
        return RES_OK;
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
        bug_on(array->magic != TYPE_EMPTY);
        array->magic = TYPE_LIST;

        array->a = array_handle_new();
        return array;
}

/**
 * array_get_type - Get the type of data stored in an array
 * @array: Array containing stuff
 *
 * Return:
 * A TYPE_* enum matching the contents of the array
 */
int
array_get_type(struct var_t *array)
{
        bug_on(array->magic != TYPE_LIST);
        return array->a->type;
}

static void
array_reset(struct var_t *a)
{
        TYPE_HANDLE_DECR_REF(a->a);
        a->a = NULL;
}

static void
array_mov(struct var_t *to, struct var_t *from)
{
        to->a = from->a;
        TYPE_HANDLE_INCR_REF(to->a);
        to->magic = TYPE_LIST;
}

static int
array_cmp(struct var_t *a, struct var_t *b)
{
        if (b->magic != TYPE_LIST || b->a != a->a)
                return -1;
        /*
         * XXX REVISIT: Good policy choice?
         *      if different handle but same length, same type,
         *      check if all elements per index also match.
         */
        return 0;
}

static const struct operator_methods_t array_primitives = {
        /* To do, I may want to support some of these */
        .cmp = array_cmp,
        .mov = array_mov,
        .reset = array_reset,
};

static int
array_len(struct var_t *ret)
{
        struct var_t *self = get_this();
        bug_on(self->magic != TYPE_LIST);
        integer_init(ret, self->a->nmemb);
        return 0;
}

static int
array_foreach(struct var_t *ret)
{
        struct var_t *self, *func, *argv[2], **ppvar;
        unsigned int idx, lock;
        struct array_handle_t *h;
        int status = 0;

        self = get_this();
        func = frame_get_arg(0);
        bug_on(self->magic != TYPE_LIST);
        if (!func) {
                err_argtype("function");
                return -1;
        }
        h = self->a;
        if (!h->nmemb) /* nothing to iterate over */
                return 0;

        ppvar = (struct var_t **)h->children.s;

        argv[0] = var_new(); /* item */
        argv[1] = var_new(); /* index of item */
        integer_init(argv[1], 0);

        lock = h->lock;
        h->lock = 1;
        for (idx = 0; idx < h->nmemb; idx++) {
                struct var_t *retval;
                var_reset(argv[0]);
                if (!qop_mov(argv[0], ppvar[idx])) {
                        status = -1;
                        break;
                }
                argv[1]->i = idx;

                retval = vm_reenter(func, NULL, 2, argv);
                if (retval == ErrorVar) {
                        status = RES_ERROR;
                        break;
                }
                /* foreach throws away retval */
                VAR_DECR_REF(retval);
        }
        h->lock = lock;

        VAR_DECR_REF(argv[0]);
        VAR_DECR_REF(argv[1]);
        return status;
}

static int
array_append(struct var_t *ret)
{
        struct var_t *self, *arg;
        struct array_handle_t *ah;
        self = get_this();
        arg = vm_get_arg(0);
        bug_on(self->magic != TYPE_LIST);
        ah = self->a;

        if (!arg) {
                /* ugh, I know what it means */
                err_argtype("item");
                return -1;
        }
        if (ah->type != TYPE_EMPTY && ah->type != arg->magic) {
                bug_on(ah->nmemb <= 0);
                array_type_err(ah, arg);
                return -1;
        }
        array_add_child(self, arg);
        return 0;
}

static const struct type_inittbl_t array_methods[] = {
        V_INITTBL("append",     array_append,   0, 0),
        V_INITTBL("len",        array_len,      0, 0),
        V_INITTBL("foreach",    array_foreach,  0, 0),
        TBLEND,
};

void
typedefinit_array(void)
{
        var_config_type(TYPE_LIST, "list",
                        &array_primitives, array_methods);
}
