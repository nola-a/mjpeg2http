#ifndef LIST_H
#define LIST_H

struct dlist {
	struct dlist *right, *left;
};

#define init_list_entry(ptr) \
        do { \
            (ptr)->left = ptr; (ptr)->right = ptr; } \
        while(0);
#define declare_list(_name_) struct dlist _name_ = {&(_name_), &(_name_) }

static inline void list_add_right(struct dlist *new, struct dlist *list)
{
	list->right->left = new;
	new->left = list;
	new->right = list->right;
	list->right = new;
}

static inline void list_add_left(struct dlist *new, struct dlist *list)
{
	list->left->right = new;
	new->left = list->left;
	new->right = list;
	list->left = new;
}
static inline void list_del(struct dlist *entry)
{
	entry->right->left = entry->left;
	entry->left->right = entry->right;
}

static inline int list_empty(struct dlist *list)
{
	return list->right == list;
}

#define list_get_first(listhead) \
        ((listhead)->right)

#define list_get_right(_entry_) \
        ((_entry_)->right)

#define list_get_entry(entry, type, structmember) \
        ((type *) ((char *)(entry) - (unsigned long)(&((type *)0)->structmember)))

#define list_iterate(entry, listhead) \
        for(entry = (listhead)->right; entry != (listhead); entry = entry->right)

#define list_iterate_safe(entry, save, listhead) \
        for(entry = (listhead)->right, save = entry->right; entry != (listhead); \
            entry = save, save = save->right)

#define list_size(n, listhead) \
        for(struct dlist* entry = (listhead)->right; entry != (listhead); entry = entry->right, ++n)


#endif

