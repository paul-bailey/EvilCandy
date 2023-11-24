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
        size_t allocsize;
        size_t datasize;
        void *data;
};

static void
array_set_type(struct array_handle_t *h, int magic)
{
        h->type = magic;
        switch (magic) {
        case QFLOAT_MAGIC:
                h->datasize = sizeof(double);
                break;
        case QINT_MAGIC:
                h->datasize = sizeof(long long);
                break;
        case QSTRING_MAGIC:
                h->datasize = sizeof(struct buffer_t);
                break;
        case QOBJECT_MAGIC:
        case QFUNCTION_MAGIC:
        case QPTRXU_MAGIC:
        case QPTRXI_MAGIC:
        case QARRAY_MAGIC:
                h->datasize = sizeof(struct var_t);
                break;
        default:
                bug();
        }
}

static void
array_grow(struct array_handle_t *h, unsigned int nmemb)
{
        enum { ARRAY_BLKSIZE = 1024 };
        while ((h->nmemb + nmemb) * h->datasize > h->allocsize) {
                void *new_data;
                new_data = realloc(h->data, h->allocsize + ARRAY_BLKSIZE);
                if (!new_data)
                        fail("realloc failed");
                h->data = new_data;
                h->allocsize += ARRAY_BLKSIZE;
        }
}

static void
check_type_match(struct array_handle_t *h, struct var_t *child)
{
        if (h->type != child->magic) {
                syntax("Trying to add type '%s' to '%s' array",
                        typestr(child->magic), h->type);
        }
}

static void *
array_ptr(struct array_handle_t *h, unsigned int idx)
{
        return h->data + idx * h->datasize;
}

static void
array_incr_nmemb(struct array_handle_t *h)
{
        void *p;

        array_grow(h, 1);

        p = array_ptr(h, h->nmemb);
        switch (h->type) {
        case QFLOAT_MAGIC:
                *(double *)p = 0.;
                break;
        case QINT_MAGIC:
                *(long long *)p = 0LL;
                break;
        case QSTRING_MAGIC:
                buffer_init((struct buffer_t *)p);
                break;
        default:
                var_init((struct var_t *)p);
                break;
        }

        h->nmemb++;
}

/* after safety checks are done */
static void
array_set_child_safe(struct array_handle_t *h,
                     unsigned int idx, struct var_t *child)
{
        void *p = array_ptr(h, idx);
        switch (h->type) {
        case QFLOAT_MAGIC:
                *(double *)p = child->f;
                break;
        case QINT_MAGIC:
                *(long long *)p = child->i;
                break;
        case QSTRING_MAGIC:
            {
                struct buffer_t *buf = (struct buffer_t *)p;
                buffer_reset(buf);
                buffer_puts(buf, child->s.s);
                break;
            }
        default:
                qop_mov((struct var_t *)p, child);
        }
}

static struct array_handle_t *
array_handle_new(void)
{
        struct array_handle_t *ret = ecalloc(sizeof(*ret));
        ret->type = QEMPTY_MAGIC;
        return ret;
}

static int
index_of(struct array_handle_t *h, int idx)
{
        if (idx < 0) {
                idx = -idx;
                idx = h->nmemb - idx;
        }
        /* if still <0, error */
        return idx;
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
int
array_child(struct var_t *array, int idx, struct var_t *child)
{
        /*
         * Even though having @child be an argument rather than a return
         * value makes things ugly (see where it's used in eval.c), the
         * alternative is to store **all** data as struct qvar_t, but
         * that's hard on the RAM if user's going to use, say, an array
         * of a million floats.
         */
        struct array_handle_t *h = array->a;
        void *p;
        idx = index_of(h, idx);
        if (h->nmemb <= idx || idx < 0)
                return -1;

        p = array_ptr(h, idx);
        switch (h->type) {
        case QFLOAT_MAGIC:
                qop_assign_float(child, *(double *)p);
                break;
        case QINT_MAGIC:
                qop_assign_int(child, *(long long *)p);
                break;
        case QSTRING_MAGIC:
                qop_assign_cstring(child, ((struct buffer_t *)p)->s);
                break;
        default:
                qop_mov(child, (struct var_t *)p);
        }
        return 0;
}

/**
 * If array type is such that it holds a struct var, return that.
 * Otherwise return NULL
 */
struct var_t *
array_vchild(struct var_t *array, int idx)
{
        struct array_handle_t *h = array->a;

        idx = index_of(h, idx);
        if (h->nmemb <= idx || idx < 0)
                return NULL;

        switch (h->type) {
        case QFLOAT_MAGIC:
        case QINT_MAGIC:
        case QSTRING_MAGIC:
                return NULL;
        default:
                break;
        }
        return (struct var_t *)array_ptr(h, idx);
}

/**
 * array_set_child - Set the value of an array member
 * @array:      Array to operate on
 * @idx:        Index into the array
 * @child:      Variable storing the data to set in array
 *
 * This is for an existing member.
 */
int
array_set_child(struct var_t *array, int idx, struct var_t *child)
{
        struct array_handle_t *h = array->a;

        idx = index_of(h, idx);
        if (h->nmemb <= idx || idx < 0)
                return -1;
        check_type_match(h, child);
        array_set_child_safe(h, idx, child);
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
                array_set_type(h, child->magic);
        }

        check_type_match(h, child);

        array_incr_nmemb(h);
        array_set_child_safe(h, h->nmemb - 1, child);
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
        array->a->nref = 1;
        return array;
}

static void
array_reset(struct var_t *a)
{
        a->a->nref--;
        if (a->a->nref <= 0) {
                bug_on(a->a->nref < 0);
                free(a->a->data);
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

void
typedefinit_array(void)
{
        var_config_type(QARRAY_MAGIC, "array", &array_primitives, NULL);
}
