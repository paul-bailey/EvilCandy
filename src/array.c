/* array.c - Code for managing numerical arrays */
#include "egq.h"

/**
 * similar to object_nth_child, but specifically for arrays
 * @n is indexed from zero.
 */
struct var_t *
array_child(struct var_t *array, int n)
{
        struct list_t *child;
        int i = 0;
        if (n < 0)
                return NULL;
        list_foreach(child, &array->a) {
                if (i == n)
                        return list2var(child);
                i++;
        }
        return NULL;
}

void
array_add_child(struct var_t *array, struct var_t *child)
{
        if (!list_is_empty(&child->a))
                syntax("Adding an element already owned by something else");

        if (!list_is_empty(&array->a)) {
                struct var_t *check = array_child(array, 0);
                if (child->magic != check->magic)
                        syntax("Array cannot append elements of different type");
        }
        list_add_tail(&child->siblings, &array->a);
}

struct var_t *
array_from_empty(struct var_t *array)
{
        bug_on(array->magic != QEMPTY_MAGIC);
        array->magic = QARRAY_MAGIC;

        list_init(&array->a);
        return array;
}

void
array_reset(struct var_t *a)
{
        struct list_t *child, *tmp;
        bug_on(a->magic != QARRAY_MAGIC);
        list_foreach_safe(child, tmp, &a->a)
                var_delete(list2var(child));
}


