/**
 * NX List
 * (c) Woxell.co
 */
#include <stdlib.h>
#include "nx/list.h"

list *list_new()
{
	list *l = (list *) calloc(1, sizeof(list));
	l->size = 0;
	return l;

	//puts("calloc called");
	//l->type_s = type_s;
	//l->x = calloc(size, type_s);
	//l = (list*) malloc(sizeof(list));
}

void lnode_delcat(list_node *ln)
{
	if (ln == NULL) return;
	if(ln->next != NULL)
	{
		lnode_delcat(ln->next);
	}
	free(ln);
}

void list_delete(list *l)
{
	if(l->head == NULL)
	{
		lnode_delcat(l->head);
	}
	free(l);
	//free(l->x);
}

list_node *lnode_getat(list_node *ln, size_t i)
{
	//printf("Searching on node %lu\n", i);
	if(i > 0)
	{
		return lnode_getat(ln->next, i - 1);
	}
	else if(i == 0)
	{
		return ln;
	}
	else
	{
		return NULL;
	}
}
//int i = 0;
list_node *lnode_getlast(list_node *ln)
{
	//printf("Searching on last-node %d\n", i++);
	if(ln->next == NULL)
	{
		//puts("return");
		return ln;
	}
	else
	{
		//puts("next");
		return lnode_getlast(ln->next);
	}
}

size_t lnode_nextdepth(list_node *ln, size_t i)
{
	if(ln->next == NULL)
	{
		return i;
	}
	else
	{
		return lnode_nextdepth(ln->next, i + 1);
	}
}

/*void* lnode_getdata(list_node* ln, size_t i)
{
	if(i < 0)
	{
		return NULL;
	}
	else if(i == 0)
	{
		return ln->data;
	}
	else
	{
		return lnode_getdata(ln->next, i - 1);
	}
}*/

size_t list_size(list *l)
{
	/*if(l->head == NULL)
	{
		return 0;
	}
	return lnode_nextdepth(l->head, 0) + 1;*/
	return l->size;
}

void* list_get(list* l, size_t i)
{
	
	return lnode_getat(l->head, i)->data;
	//return void* (((char*) l->x) + i * l->type_s);
}

void list_add(list *l, void *data)
{
	//list_node* newn = (list_node*) malloc(sizeof(list_node) * 2048);
	list_node *newn = (list_node *) malloc(sizeof(list_node));
	//printf("sizeof(list_node): %lu\n", sizeof(list_node));
	newn->next = NULL;
	//newn.data = malloc(l->type_s);
	newn->data = data;
	//printf("int: %s\n", newn->data);
	if(l->head == NULL)
	{
		l->head = newn;
		//puts("pop");
	}
	else
	{
		//puts("poc");
		//lnode_getlast(l->head)->next = newn;
		l->last->next = newn;
	}
	l->size++;
	l->last = newn;
	/*size_t s = sizeof(l->x) + 1;
	l->x = realloc(l->x, s * l->type_s);
	void* tmp;
	memcpy(tmp, l->x, sizeof(l->x));
	*(tmp + s) = value;
	memcpy(l->x, tmp, sizeof(tmp));*/
}

void list_cat(list *l, list *lcat)
{
	list_node *prev = lnode_getlast(l->head);
	for(unsigned long long int i = 0; i < list_size(lcat); i++)
	{
		list_node* newn = (list_node*) malloc(sizeof(list_node));
		newn->data = lnode_getat(lcat->head, i)->data;
		prev->next = newn;
		prev = newn;
	}
}

void list_insert(list *l, size_t i, void *data)
{
	list_node *newn = (list_node*) malloc(sizeof(list_node));
	newn->data = data;
	newn->next = lnode_getat(l->head, i);
	lnode_getat(l->head, i - 1)->next = newn;
}

void list_insertcat(list *l, size_t i, list *lcat)
{
	list_node *prev = lnode_getat(l->head, i - 1);
	list_node *ct = lnode_getat(l->head, i);
	for(unsigned long long int i = 0; i < list_size(lcat); i++)
	{
		list_node *newn = (list_node *) malloc(sizeof(list_node));
		newn->data = lnode_getat(lcat->head, i)->data;
		prev->next = newn;
		prev = newn;
	}
	prev->next = ct;
	l->size = l->size + lcat->size;
}

void list_set(list *l, size_t i, void *data)
{
	lnode_getat(l->head, i)->data = data;
}

void list_remove(list *l, size_t i)
{
	list_node* prev = NULL;
	list_node* td;
	if (i > 0) { // 07/20/2025
		prev = lnode_getat(l->head, i - 1);
		td = prev->next;
		prev->next = td->next;
	} else {
		td = lnode_getat(l->head, i);
		l->head = td->next;
	}

	if (td == l->last) l->last = prev;

	free(td);
	l->size--;
}

void list_clear(list *l)
{
	if(l->head != NULL)
	{
		lnode_delcat(l->head);
	}
	l->head = NULL;
	l->last = NULL;
	l->size = 0;
}

void list_copy(list *l, list *src)
{
	list_clear(l);
	list_node *newh = (list_node*) malloc(sizeof(list_node));
	newh->data = lnode_getat(src->head, 0)->data;
	l->head = newh;
	list_node* prev = newh;
	for(unsigned long long int i = 1; i < list_size(src); i++)
	{
		list_node *newn = (list_node *) malloc(sizeof(list_node));
		newn->data = lnode_getat(src->head, i)->data;
		prev->next = newn;
		prev = newn;
	}
	l->last = src->last;
	l->size = src->size;
}

/*size_t list_size(list* l)
{
	return sizeof(l->x) / l->type_s;
}*/