/*
 * array.c - Code for managing numerical arrays
 *
 * These are called "lists" in the documentation, since
 *      1) that's what Python calls them, so why not, and
 *      2) calling them "arrays" could mislead users into thinking
 *         these are fast in the way that C arrays are fast.
 *
 * But here I call them "arrays" because I started writing this
 * file before I thought things through. (LOL, cf. object.c)
 *
 * XXX REVISIT: Policy decision... Should I continue to enforce
 *      lists having all the same type of items?  JavaScript
 *      doesn't do that.  Neither does Python.  There are some
 *      actual advantages to NOT enforcing it.
 */
#include "var.h"
#include <stdlib.h>
#include <limits.h>

/**
 * struct array_handle_t - Handle to a numerical array
 * @lock:       lock to prevent add/remove during foreach
 * @nmemb:      Size of the array, in number of elements
 * @allocsize:  Size of the array, in number of bytes currently allocated
 *              for it
 * @datasize:   Size of each member of the array (so we don't have to
 *              keep figuring it out from @type all the time)
 */
struct array_handle_t {
        int lock;
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
 * Return: child member, or NULL if idx out of bounds.
 *         This does not return ErrorVar because the call could just
 *         be a probe.
 *
 * Calling code needs to call VAR_INCR_REF if it uses it.
 * That's not done here.
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
 * array_insert - Insert @child into @idx and throw error if
 *                array out of bounds.
 * @array:      Array to operate on
 * @idx:        Index into the array
 * @child:      Variable storing the data to set in array
 *
 * Data is moved from @child to the array member, @child must still be
 * dealt with w/r/t garbage collection by calling code.
 */
enum result_t
array_insert(struct var_t *array, struct var_t *idx, struct var_t *child)
{
        struct var_t **ppvar;
        int i;

        bug_on(array->magic != TYPE_LIST);

        if (idx->magic != TYPE_INT) {
                err_setstr(RuntimeError, "Array subscript must be integer");
                return RES_ERROR;
        }
        if (idx->i < INT_MIN || idx->i > INT_MAX) {
                err_setstr(RuntimeError, "Array index out of bounds");
                return RES_ERROR;
        }

        if (array->a->lock) {
                err_locked();
                return RES_ERROR;
        }

        ppvar = (struct var_t **)array->a->children.s;
        if ((i = index_translate(idx->i, array->a->nmemb)) < 0)
                return RES_ERROR;

        /* delete old entry */
        bug_on(ppvar[i] == NULL);
        VAR_DECR_REF(ppvar[i]);

        ppvar[i] = child;
        VAR_INCR_REF(child);
        return RES_OK;
}

/**
 * array_append - Append a new element to an array
 * @array:  Array to add to
 * @child:  New element to put in array
 */
enum result_t
array_append(struct var_t *array, struct var_t *child)
{
        struct array_handle_t *h = array->a;

        if (h->lock) {
                err_locked();
                return RES_ERROR;
        }
        if (child->magic == TYPE_EMPTY) {
                /* XXX bug? */
                err_setstr(RuntimeError, "You may not add an empty var to array");
                return RES_ERROR;
        }

        h->nmemb++;
        /* XXX: poor amortization, maybe assert_array_pos instead */
        buffer_putd(&h->children, &child, sizeof(void *));
        VAR_INCR_REF(child);
        return RES_OK;
}

int
array_length(struct var_t *array)
{
        bug_on(array->magic != TYPE_LIST);
        return array->a->nmemb;
}

struct var_t *
arrayvar_new(void)
{
        struct var_t *array = var_new();
        array->magic = TYPE_LIST;

        array->a = array_handle_new();
        return array;
}

static void
array_reset(struct var_t *a)
{
        TYPE_HANDLE_DECR_REF(a->a);
        a->a = NULL;
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

static struct var_t *
array_cp(struct var_t *a)
{
        VAR_INCR_REF(a);
        return a;
}

static const struct operator_methods_t array_primitives = {
        /* To do, I may want to support some of these */
        .cmp = array_cmp,
        .cp  = array_cp,
        .reset = array_reset,
};

static struct var_t *
do_array_len(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        bug_on(self->magic != TYPE_LIST);
        return intvar_new(self->a->nmemb);
}

static struct var_t *
do_array_foreach(struct vmframe_t *fr)
{
        struct var_t *self, *func, *argv[2], **ppvar;
        unsigned int idx, lock;
        struct array_handle_t *h;
        int status = RES_OK;

        self = get_this(fr);
        func = frame_get_arg(fr, 0);
        bug_on(self->magic != TYPE_LIST);
        if (!func) {
                err_argtype("function");
                return ErrorVar;
        }
        h = self->a;
        if (!h->nmemb) /* nothing to iterate over */
                goto out;

        ppvar = (struct var_t **)h->children.s;

        argv[1] = intvar_new(0); /* index of item */

        lock = h->lock;
        h->lock = 1;
        for (idx = 0; idx < h->nmemb; idx++) {
                struct var_t *retval;

                argv[0] = ppvar[idx];
                argv[1]->i = idx;

                retval = vm_reenter(fr, func, NULL, 2, argv);
                if (retval == ErrorVar) {
                        status = RES_ERROR;
                        break;
                }
                /* foreach throws away retval */
                if (retval)
                        VAR_DECR_REF(retval);
        }
        h->lock = lock;

        VAR_DECR_REF(argv[1]);
out:
        return status == RES_OK ? NULL : ErrorVar;
}

static struct var_t *
do_array_append(struct vmframe_t *fr)
{
        struct var_t *self, *arg;
        self = get_this(fr);
        arg = vm_get_arg(fr, 0);
        bug_on(self->magic != TYPE_LIST);

        if (!arg) {
                /* ugh, I know what it means */
                err_argtype("item");
                return ErrorVar;
        }
        array_append(self, arg);
        return NULL;
}

static const struct type_inittbl_t array_methods[] = {
        V_INITTBL("append",     do_array_append,   0, 0),
        V_INITTBL("len",        do_array_len,      0, 0),
        V_INITTBL("foreach",    do_array_foreach,  0, 0),
        TBLEND,
};

void
typedefinit_array(void)
{
        var_config_type(TYPE_LIST, "list",
                        &array_primitives, array_methods);
}
