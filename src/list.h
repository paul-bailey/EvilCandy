#ifndef LIST_H
#define LIST_H

#include <stdbool.h>
#include <stddef.h>

struct list_t {
        struct list_t *next;
        struct list_t *prev;
};

extern void list_insert_before(struct list_t *a, struct list_t *b);
extern void list_insert_after(struct list_t *a, struct list_t *b);
extern void list_remove(struct list_t *list);
static inline void list_init(struct list_t *list)
        { list->next = list->prev = list; }
static inline bool list_is_empty(struct list_t *list)
        { return list->next == list; }
static inline void
list_add_tail(struct list_t *list, struct list_t *owner)
        { list_insert_before(list, owner); }
static inline void
list_add_front(struct list_t *list, struct list_t *owner)
        { list_insert_after(list, owner); }
#define list_foreach(iter_, top_) \
        for (iter_ = (top_)->next; iter_ != (top_); iter_ = (iter_)->next)
#define list_foreach_safe(iter_, tmp_, top_) \
        for (iter_ = (top_)->next, tmp_ = (iter_)->next; \
             iter_ != (top_); iter_ = tmp_, tmp_ = (iter_)->next)

#endif /* LIST_H */
