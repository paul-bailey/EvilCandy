#include "egq.h"

/* insert new @a before @b */
void
list_insert_before(struct list_t *a, struct list_t *b)
{
        a->next = b;
        a->prev = b->prev;
        b->prev = a;
        a->prev->next = a;
}

void
list_insert_after(struct list_t *a, struct list_t *b)
{
        a->next = b->next;
        a->prev = b;
        b->next = a;
        a->next->prev = a;
}

void
list_remove(struct list_t *list)
{
        list->next->prev = list->prev;
        list->prev->next = list->next;
        list_init(list);
}

