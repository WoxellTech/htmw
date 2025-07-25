/**
 * Simple Map
 * (c) Woxell.co
 */
#include "map.h"
#include <stddef.h>
#include <stdio.h>

u_map *map_new()
{
	u_map *x = (u_map *)calloc(1, sizeof(u_map));
	x->size = 0;

	x->s = calloc(6, sizeof(size_t));

	return x;
}

u_map_node *mapn_new(const mapt_t *token, void *data)
{
	u_map_node *n = (u_map_node *)malloc(sizeof(u_map_node));
	n->token = wcsdup(token);
	n->data = data;
	n->next = NULL;

	return n;
}

u_map_node *mapn_find(u_map_node *begin, const mapt_t *token)
{
	if(begin == NULL) return NULL; //7/14/2024
	if(wcscmp(token, begin->token))
	{
		if(begin->next == NULL)
			return NULL;
		else
			return mapn_find(begin->next, token);
	}
	else return begin;
}

u_map_node_counted mapn_find_counted(u_map_node* begin, const mapt_t* token, size_t count) {
	if (begin == NULL) return (u_map_node_counted) { NULL, count, 0 }; //7/16/2025
	if (wcscmp(token, begin->token)) {
		if (begin->next == NULL)
			return (u_map_node_counted) { NULL, count, 0 };
		else
			return mapn_find_counted(begin->next, token, count + 1);
	} else return (u_map_node_counted) { begin, count, 1 };
}

u_map_node* mapn_at(u_map_node* begin, size_t i) {
	if (begin == NULL) return NULL;
	if (i > 0) {
		if (begin->next == NULL)
			return NULL;
		else
			return mapn_at(begin->next, i - 1);
	} else return begin;
}

size_t map_size(u_map *x)
{
	return x->size;
}

int map_get(u_map *x, const mapt_t *token, void **output)
{
	u_map_node *n = mapn_find(x->head, token);
	if(n == NULL)
	{
		if (output != NULL)
			*output = NULL;
		return 0;
	} //printf("%lu\n", *(size_t*)n->data);
	if (output != NULL)
		*output = n->data;
	return 1;
}

int map_get_at(u_map* x, size_t idx, void** output) // 07/16/2025
{
	u_map_node* n = mapn_at(x->head, idx);
	if (n == NULL) {
		if (output != NULL)
			*output = NULL;
		return 0;
	} //printf("%lu\n", *(size_t*)n->data);
	if (output != NULL)
		*output = n->data;
	return 1;
}

size_t map_get_counted(u_map* x, const mapt_t* token, void** output) {
	u_map_node_counted nc = mapn_find_counted(x->head, token, 0);
	u_map_node* n = nc.node;
	if (n == NULL) {
		if (output != NULL)
			*output = NULL;
		return -1;
	} //printf("%lu\n", *(size_t*)n->data);
	if (output != NULL)
		*output = n->data;
	return nc.count;
}

void map_set(u_map *x, const mapt_t *token, void *data, int free_old)
{
	u_map_node *n = mapn_find(x->head, token);//printf("%lu\n", *(size_t*)data);

	if(n == NULL)
	{
		n = mapn_new(token, data);
		if(x->last != NULL)
			x->last->next = n;
		x->last = n;
		x->size++;
		if(x->head == NULL)
			x->head = x->last;
		return;
	}
	//int e = map_get(x, token, o);

	if(free_old && n->data != NULL)
		free(n->data);

	n->data = data;
}

void map_iterate(void *out, u_map *x)
{
	
}

/*void map_remove(u_map *x, const char *token)
{
	
	free();
}*/

void map_delete(u_map *x)
{
	
}

void map_debug_tokens(u_map *x) {
	u_map_node *n = x->head;
	while (n != NULL) {
		printf("%ls\n", n->token);
		n = n->next;
	}
}