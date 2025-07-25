/**
 * Simple Map
 * (c) Woxell.co
 */
#ifndef _U_MAP_
#define _U_MAP_
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>

#define MAP_NPOS SIZE_MAX

typedef wchar_t mapt_t;
//typedef struct {} u_map;
typedef struct m_node
{
	mapt_t *token;
	struct m_node *next;
	void *data;
} u_map_node;

typedef struct {
	u_map_node* node;
	size_t count;
	int exists;
} u_map_node_counted;

typedef struct
{
	u_map_node *head;
	u_map_node *last;
	size_t size;
	size_t *s;
} u_map;

u_map *map_new();
u_map_node* mapn_new(const mapt_t* token, void* data);
u_map_node* mapn_find(u_map_node* begin, const mapt_t* token);
u_map_node* mapn_at(u_map_node* begin, size_t i);
//int map_get(u_map *x, const char *token, void *output);
//void map_set(u_map *x, const char *token, void *data, int free_old);
void map_delete(u_map *x);
int map_get(u_map *x, const mapt_t *token, void **output);
int map_get_at(u_map* x, size_t idx, void** output);
size_t map_get_counted(u_map* x, const mapt_t* token, void** output);
void map_set(u_map *x, const mapt_t *token, void *data, int free_old);
void map_debug_tokens(u_map *x);

#ifdef __cplusplus
}
#endif
#endif
