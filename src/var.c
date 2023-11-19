#include "egq.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

/*
 * So I don't have to keep malloc'ing and freeing these
 * in tiny bits.
 */
struct qvar_blk_t {
        /* Keep as first entry for easy de-reference */
        struct list_t list;
        uint64_t used;
        struct qvar_t a[64];
};

static struct list_t qvar_blk_list = {
        .next = &qvar_blk_list,
        .prev = &qvar_blk_list
};

#define foreach_blk(b) \
        for (b = (struct qvar_blk_t *)list_first(&qvar_blk_list); \
             b != NULL; \
             b = (struct qvar_blk_t *)list_next(&b->list, &qvar_blk_list))

static struct qvar_t *
qvar_alloc(void)
{
        struct qvar_blk_t *b;
        uint64_t x;
        int i;

        foreach_blk(b) {
                /* Quick way to check at least one bit clear */
                if ((~b->used) != 0LL)
                        break;
        }

        if (!b) {
                /* need to allocate another */
                b = emalloc(sizeof(*b));
                b->used = 0LL;
                list_init(&b->list);
                list_add_tail(&b->list, &qvar_blk_list);
        }

        for (i = 0, x = 1; !!(b->used & x) && i < 64; x <<= 1, i++)
                ;
        bug_on(i == 64);
        b->used |= x;
        return &b->a[i];
}

static bool
last_in_list(struct qvar_blk_t *b)
{
        return b->list.next == &qvar_blk_list
                && b->list.prev == &qvar_blk_list;
}

static void
qvar_free(struct qvar_t *v)
{
        unsigned int idx;
        struct qvar_blk_t *b;

        foreach_blk(b) {
                if (v >= b->a && v <= &b->a[64])
                        break;
        }

        /* if !b, v was a tmp struct declared on stack */
        if (!b)
                return;

        idx = v - b->a;
        bug_on(!(b->used & (1ull << idx)));
        b->used &= ~(1ull << idx);

        qvar_init(v);

        /*
         * keep always at least one, else free it if no longer used.
         *
         * XXX REVISIT: what if I keep mallocing and freeing because
         * q_eval keeps grabbing and throwing away vars right on the
         * boundary of a used table?  (sim. to hashtable problem)
         */
        if (b->used == 0LL && !last_in_list(b)) {
                list_remove(&b->list);
                free(b);
        }
}

/* object-specific iterators */
static struct qvar_t *
object_first_child(struct qvar_t *o)
{
        struct list_t *list = list_first(&o->o.h->children);
        if (!list)
                return NULL;
        return container_of(list, struct qvar_t, siblings);
}

static struct qvar_t *
object_next_child(struct qvar_t *v, struct qvar_t *owner)
{
        struct list_t *list = list_next(&v->siblings, &owner->o.h->children);
        if (!list)
                return NULL;
        return container_of(list, struct qvar_t, siblings);
}

#define object_foreach_child(v_, o_) \
        for (v_ = object_first_child(o_); \
             v_ != NULL; v_ = object_next_child(v_, o_))

/**
 * qvar_init - Initialize a variable
 * @v: Variable to initialize.
 *
 * DO NOT call this on a struct you got from qvar_new(),
 * or you might clobber and zombify data.  Instead, call
 * it for a newly-declared struct on the stack.
 *
 * return: @v
 */
struct qvar_t *
qvar_init(struct qvar_t *v)
{
        v->magic = QEMPTY_MAGIC;
        v->name = NULL;
        list_init(&v->siblings);
        return v;
}

/**
 * qvar_new - Get a new initialized, empty, and unattached variable
 */
struct qvar_t *
qvar_new(void)
{
        return qvar_init(qvar_alloc());
}

/**
 * qvar_delete - Delete a variable.
 * @v: variable to delete.  If @v was just a temporary struct declared
 *      on the stack, call qvar_reset() only, not this.
 *
 * Note: Calling code should deal with v->name before calling this.
 * qvar_new didn't set the name, so it won't free it either.
 */
void
qvar_delete(struct qvar_t *v)
{
        qvar_reset(v);
        list_remove(&v->siblings);
        /* XXX REVISIT: we didn't set v->name, so we're not freeing it */
        qvar_free(v);
}

/**
 * qvar_copy - like qop_mov, but in the case of an object,
 *              all of @from's elements will be re-instantiated
 *              into @to
 */
void
qvar_copy(struct qvar_t *to, struct qvar_t *from)
{
        warning("%s not supported yet", __FUNCTION__);
        bug();
}

static void
qobject_reset(struct qvar_t *o)
{
        /*
         * FIXME: Get parent of @o, so any children whose objects cannot
         * yet be deleted (there are handles to them in use) inherit
         * their grandparent as their new daddy.
         */

        /*
         * can't use the foreach macro,
         * because it's not safe for removals.
         */
        struct qvar_t *p = object_first_child(o);
        while (p != NULL) {
                struct qvar_t *q = object_next_child(p, o);
                if (q)
                        qvar_delete(p);
                p = q;
        }
}

static void
qarray_reset(struct qvar_t *a)
{
        struct list_t *child, *tmp;
        list_foreach_safe(child, tmp, &a->a)
                qvar_delete(container_of(child, struct qvar_t, a));
}

/**
 * qvar_reset - Empty a variable
 * @v: variable to empty.
 *
 * This does not remove @v from its sibling list or delete its name.
 */
void
qvar_reset(struct qvar_t *v)
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
                token_free(&v->s);
                break;
        case QOBJECT_MAGIC:
                v->o.h->nref--;
                if (v->o.h->nref <= 0) {
                        bug_on(v->o.h->nref < 0);
                        qobject_reset(v);
                }
                break;
        case QARRAY_MAGIC:
                qarray_reset(v);
                break;
        default:
                bug();
        }
        v->magic = QEMPTY_MAGIC;
}

struct qvar_t *
qobject_new(struct qvar_t *owner, const char *name)
{
        struct qvar_t *o = qobject_from_empty(qvar_new());
        o->name = q_literal(name);
        o->o.owner = owner;
        return o;
}

/**
 * qobject_from_empty - Convert an empty variable into an initialized
 *                      object type.
 * @v: variable
 *
 * Return: @v cast to an object type
 *
 * This is an alternative to qobject_new();
 */
struct qvar_t *
qobject_from_empty(struct qvar_t *v)
{
        struct qvar_t *o = (struct qvar_t*)v;

        bug_on(o->magic != QEMPTY_MAGIC);
        o->magic = QOBJECT_MAGIC;

        o->o.h = emalloc(sizeof(*o->o.h));
        list_init(&o->o.h->children);
        o->o.h->nref = 1;
        o->o.owner = NULL;
        return o;
}

struct qvar_t *
qobject_child(struct qvar_t *o, const char *s)
{
        struct qvar_t *v;
        object_foreach_child(v, o) {
                if (!strcmp(v->name, s))
                        return v;
        }
        return builtin_method(o, s);
}

/* n begins at zero, not one */
struct qvar_t *
qobject_nth_child(struct qvar_t *o, int n)
{
        struct qvar_t *v;
        int i = 0;
        object_foreach_child(v, o) {
                if (i == n)
                        return v;
                i++;
        }
        return NULL;
}

void
qobject_add_child(struct qvar_t *parent, struct qvar_t *child)
{
        if (child->magic == QOBJECT_MAGIC) {
                child->o.owner = parent;
        } else if (child->magic == QFUNCTION_MAGIC) {
                child->fn.owner = parent;
        }
        list_add_tail(&child->siblings, &parent->o.h->children);
}

/**
 * similar to qobject_nth_child, but specifically for arrays
 * @n is indexed from zero.
 */
struct qvar_t *
qarray_child(struct qvar_t *array, int n)
{
        struct list_t *child;
        int i = 0;
        if (n < 0)
                return NULL;
        list_foreach(child, &array->a) {
                if (i == n)
                        return container_of(child, struct qvar_t, a);
                i++;
        }
        return NULL;
}

void
qarray_add_child(struct qvar_t *array, struct qvar_t *child)
{
        if (!list_is_empty(&child->a))
                qsyntax("Adding an element already owned by something else");

        if (!list_is_empty(&array->a)) {
                struct qvar_t *check = qarray_child(array, 0);
                if (child->magic != check->magic)
                        qsyntax("Array cannot append elements of different type");
        }
        list_add_tail(&child->siblings, &array->a);
}

struct qvar_t *
qarray_from_empty(struct qvar_t *array)
{
        bug_on(array->magic != QEMPTY_MAGIC);
        array->magic = QARRAY_MAGIC;

        list_init(&array->a);
        return array;
}

