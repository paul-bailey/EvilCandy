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
check_type_match(struct array_handle_t *h, struct var_t *child)
{
        if (h->type != child->magic) {
                syntax("Trying to add type '%s' to '%d' array",
                        typestr(child), h->type);
        }
}

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
int
array_set_child(struct var_t *array, int idx, struct var_t *child)
{
        struct var_t *memb;

        if (array->a->lock)
                syntax("attempting to modify array while locked");

        memb = array_child(array, idx);
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

        if (h->lock)
                syntax("attempting to modify array while locked");
        if (child->magic == TYPE_EMPTY)
                syntax("You may not add an empty var to array");
        if (h->type == TYPE_EMPTY) {
                /* first time, set type and assign datasize */
                bug_on(h->nmemb != 0);
                h->type = child->magic;
        }

        check_type_match(h, child);

        h->nmemb++;
        VAR_INCR_REF(child);
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

static const struct operator_methods_t array_primitives = {
        /* To do, I may want to support some of these */
        .mov = array_mov,
        .reset = array_reset,
};

static void
array_len(struct var_t *ret)
{
        struct var_t *self = get_this();
        bug_on(self->magic != TYPE_LIST);
        integer_init(ret, self->a->nmemb);
}

static void
array_foreach(struct var_t *ret)
{
        struct var_t *self, *func, *argv[2], **ppvar;
        unsigned int idx, lock;
        struct array_handle_t *h;

        self = get_this();
        func = frame_get_arg(0);
        bug_on(self->magic != TYPE_LIST);
        if (!func)
                syntax("Expected: function");
        h = self->a;
        if (!h->nmemb)
                return;

        ppvar = (struct var_t **)h->children.s;

        argv[0] = var_new(); /* item */
        argv[1] = var_new(); /* index of item */
        integer_init(argv[1], 0);

        lock = h->lock;
        h->lock = 1;
        for (idx = 0; idx < h->nmemb; idx++) {
                var_reset(argv[0]);
                qop_mov(argv[0], ppvar[idx]);
                argv[1]->i = idx;

                vm_reenter(func, NULL, 2, argv);
        }
        h->lock = lock;

        VAR_DECR_REF(argv[0]);
        VAR_DECR_REF(argv[1]);
}

static void
array_append(struct var_t *ret)
{
        struct var_t *self, *arg;
        struct array_handle_t *ah;
        self = get_this();
        arg = vm_get_arg(0);
        bug_on(self->magic != TYPE_LIST);
        ah = self->a;

        if (!arg) {
                syntax("Expected: item");
        }
        if (ah->type != TYPE_EMPTY && ah->type != arg->magic) {
                bug_on(ah->nmemb <= 0);
                /*
                 * bloated way to print type, but we're already
                 * in failure mode, so time is not a consideration
                 * anymore.
                 */
                struct var_t *somechild = array_child(self, 0);
                syntax("Cannot append type %s to list type %s",
                       typestr(somechild), typestr(arg));
        }
        array_add_child(self, arg);
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
