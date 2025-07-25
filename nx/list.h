/**
 * NX List
 * (c) Woxell.co
 */
#ifndef NX_LIST
#define  NX_LIST

#define __need_NULL
#define __need_size_t
#ifndef __STRICT_ANSI__
#define __need_wchar_t
#endif
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct n_list_node {
	void* data;
	struct n_list_node* next;
	//struct node* back;
} list_node;

typedef struct {
	size_t type_s;
	//void* x;
	list_node* head;
	list_node* last;
	size_t size;
} list;
list *list_new();
void list_delete(list *l);
size_t list_size(list *l);
void *list_get(list *l, size_t i);
void list_add(list *l, void *data);
void list_cat(list *l, list *lcat);
void list_insert(list *l, size_t i, void *data);
void list_insertcat(list *l, size_t i, list *lcat);
void list_set(list *l, size_t i, void *data);
void list_remove(list *l, size_t i);
void list_clear(list *l);
void list_copy(list *l, list *src);

#ifdef __cplusplus
}
#endif
#endif