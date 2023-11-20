#include "egq.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SIMPLE_ALLOC 0

struct type_t TYPEDEFS[Q_NMAGIC] = {
        { .name = "empty" },
        { .name = "object" },
        { .name = "function" },
        { .name = "float" },
        { .name = "int" },
        { .name = "string" },
        { .name = "pointer" },
        { .name = "built_in_function" },
        { .name = "array" },
};

#if SIMPLE_ALLOC
static struct var_t *
var_alloc(void)
{
        return emalloc(sizeof(struct var_t));
}

static void
var_free(struct var_t *v)
{
        free(v);
}

#else
/*
 * So I don't have to keep malloc'ing and freeing these
 * in tiny bits.
 */
struct var_blk_t {
        /* Keep as first entry for easy de-reference */
        struct list_t list;
        uint64_t used;
        struct var_t a[64];
};

static struct list_t var_blk_list = {
        .next = &var_blk_list,
        .prev = &var_blk_list
};

#define list2blk(li) container_of(li, struct var_blk_t, list)

static struct var_t *
var_alloc(void)
{
        return emalloc(sizeof(struct var_t));
        struct list_t *iter;
        struct var_blk_t *b = NULL;
        uint64_t x;
        int i;

        list_foreach(iter, &var_blk_list) {
                b = list2blk(iter);
                /* Quick way to check at least one bit clear */
                if ((~b->used) != 0LL)
                        break;
        }

        if (!b) {
                /* need to allocate another */
                b = emalloc(sizeof(*b));
                b->used = 0LL;
                list_init(&b->list);
                list_add_tail(&b->list, &var_blk_list);
        }

        for (i = 0, x = 1; !!(b->used & x) && i < 64; x <<= 1, i++)
                ;
        bug_on(i == 64);
        b->used |= x;
        return &b->a[i];
}

static bool
last_in_list(struct var_blk_t *b)
{
        return b->list.next == &var_blk_list
                && b->list.prev == &var_blk_list;
}

static void
var_free(struct var_t *v)
{
        unsigned int idx;
        struct list_t *iter;
        struct var_blk_t *b = NULL;

        list_foreach(iter, &var_blk_list) {
                b = list2blk(iter);
                if (v >= b->a && v <= &b->a[64])
                        break;
        }

        /* if !b, v was a tmp struct declared on stack */
        if (!b)
                return;

        idx = v - b->a;
        bug_on(!(b->used & (1ull << idx)));
        b->used &= ~(1ull << idx);

        var_init(v);

        /*
         * keep always at least one, else free it if no longer used.
         *
         * XXX REVISIT: what if I keep mallocing and freeing because
         * eval() keeps grabbing and throwing away vars right on the
         * boundary of a used table?  (sim. to hashtable problem)
         */
        if (b->used == 0LL && !last_in_list(b)) {
                list_remove(&b->list);
                free(b);
        }
}
#endif

/**
 * var_init - Initialize a variable
 * @v: Variable to initialize.
 *
 * DO NOT call this on a struct you got from var_new(),
 * or you might clobber and zombify data.  Instead, call
 * it for a newly-declared struct on the stack.
 *
 * return: @v
 */
struct var_t *
var_init(struct var_t *v)
{
        v->magic = QEMPTY_MAGIC;
        v->name = NULL;
        list_init(&v->siblings);
        return v;
}

/**
 * var_new - Get a new initialized, empty, and unattached variable
 */
struct var_t *
var_new(void)
{
        return var_init(var_alloc());
}

/**
 * var_delete - Delete a variable.
 * @v: variable to delete.  If @v was just a temporary struct declared
 *      on the stack, call var_reset() only, not this.
 *
 * Note: Calling code should deal with v->name before calling this.
 * var_new didn't set the name, so it won't free it either.
 */
void
var_delete(struct var_t *v)
{
        var_reset(v);
        list_remove(&v->siblings);
        /* XXX REVISIT: we didn't set v->name, so we're not freeing it */
        var_free(v);
}

/**
 * var_copy - like qop_mov, but in the case of an object,
 *              all of @from's elements will be re-instantiated
 *              into @to
 */
void
var_copy(struct var_t *to, struct var_t *from)
{
        warning("%s not supported yet", __FUNCTION__);
        bug();
}

static void
object_reset(struct var_t *o)
{
        struct list_t *child, *tmp;
        bug_on(o->magic != QOBJECT_MAGIC);
        list_foreach_safe(child, tmp, &o->o.h->children)
                var_delete(container_of(child, struct var_t, siblings));
}

static void
array_reset(struct var_t *a)
{
        struct list_t *child, *tmp;
        bug_on(a->magic != QARRAY_MAGIC);
        list_foreach_safe(child, tmp, &a->a)
                var_delete(container_of(child, struct var_t, a));
}

/**
 * var_reset - Empty a variable
 * @v: variable to empty.
 *
 * This does not remove @v from its sibling list or delete its name.
 */
void
var_reset(struct var_t *v)
{
        switch (v->magic) {
        case QEMPTY_MAGIC:
                return;
        case QINT_MAGIC:
        case QFLOAT_MAGIC:
        case QFUNCTION_MAGIC:
        case QINTL_MAGIC:
        case QPTRX_MAGIC:
                /* Nothing to free or be fancy with */
                break;
        case QSTRING_MAGIC:
                buffer_free(&v->s);
                break;
        case QOBJECT_MAGIC:
                v->o.h->nref--;
                if (v->o.h->nref <= 0) {
                        bug_on(v->o.h->nref < 0);
                        object_reset(v);
                }
                v->o.owner = NULL;
                break;
        case QARRAY_MAGIC:
                array_reset(v);
                break;
        default:
                bug();
        }
        list_remove(&v->siblings);
        v->magic = QEMPTY_MAGIC;
}

struct var_t *
object_new(struct var_t *owner, const char *name)
{
        struct var_t *o = object_from_empty(var_new());
        o->name = literal(name);
        o->o.owner = owner;
        return o;
}

/**
 * object_from_empty - Convert an empty variable into an initialized
 *                      object type.
 * @v: variable
 *
 * Return: @v cast to an object type
 *
 * This is an alternative to object_new();
 */
struct var_t *
object_from_empty(struct var_t *v)
{
        struct var_t *o = (struct var_t*)v;

        bug_on(o->magic != QEMPTY_MAGIC);
        o->magic = QOBJECT_MAGIC;

        o->o.h = emalloc(sizeof(*o->o.h));
        list_init(&o->o.h->children);
        o->o.h->nref = 1;
        o->o.owner = NULL;
        return o;
}

/**
 * object_child - Return an object's child
 * @o:  Object to seek the child of.
 * @s:  Name of the child
 *
 * Return:    - the child if found
 *            - the built-in method matching @s if the child is not found
 *            - NULL if neither are found.
 */
struct var_t *
object_child(struct var_t *o, const char *s)
{
        struct list_t *child, *parent = &o->o.h->children;
        bug_on(!parent);
        list_foreach(child, parent) {
                struct var_t *v = container_of(child,
                                        struct var_t, siblings);
                if (!strcmp(v->name, s))
                        return v;
        }
        return builtin_method(o, s);
}

/**
 * eobject_child - like object_child, but throw an error if not found.
 */
struct var_t *
eobject_child(struct var_t *o, const char *s)
{
        struct var_t *v;
        bug_on(o->magic != QOBJECT_MAGIC);
        v = object_child(o, s);
        if (!v)
                syntax("object %s has no child %s", nameof(o), cur_oc->s);
        return v;
}

/* n begins at zero, not one */
struct var_t *
object_nth_child(struct var_t *o, int n)
{
        int i = 0;
        struct list_t *child, *parent = &o->o.h->children;
        list_foreach(child, parent) {
                if (i == n)
                        return container_of(child, struct var_t, siblings);
                i++;
        }
        return NULL;
}

void
object_add_child(struct var_t *parent, struct var_t *child)
{

        if (child->magic == QOBJECT_MAGIC) {
                child->o.owner = parent;
        } else if (child->magic == QFUNCTION_MAGIC) {
                child->fn.owner = parent;
        }
        list_add_tail(&child->siblings, &parent->o.h->children);
}

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
                        return container_of(child, struct var_t, a);
                i++;
        }
        return NULL;
}

struct var_t *
earray_child(struct var_t *array, int n)
{
        struct var_t *ret = array_child(array, n);
        if (!ret)
                syntax("Array %s has no %llith element", nameof(array), n);
        return ret;
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

