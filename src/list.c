#include "egq.h"

/* insert new @a before @b */
void
list_insert_before(struct list_t *a,
                   struct list_t *b, struct list_t *owner)
{
        struct list_t *q = list_prev(b, owner);
        if (!q) {
                /* empty list */
                owner->next = owner->prev = a;
                a->next = a->prev = owner;
        } else {
                a->prev = q;
                a->next = b;
                b->prev = a;
                q->next = a;
        }
}

void
list_insert_after(struct list_t *a,
                struct list_t *b, struct list_t *owner)
{
        struct list_t *q = list_next(b, owner);
        if (!q) {
                /* empty list */
                owner->next = owner->prev = a;
                a->next = a->prev = owner;
        } else {
                b->prev = b;
                a->next = q;
                b->next = a;
                q->prev = a;
        }
}

void
list_remove(struct list_t *list)
{
        list->next->prev = list->prev;
        list->prev->next = list->next;
        list_init(list);
}

