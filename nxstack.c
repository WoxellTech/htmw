/**
 * NX Stack
 * (c) Woxell.co
 */
#include "nx/stack.h"
#include <stdlib.h>

stack_node* snode_getlast(stack_node* sn) {
	if (!sn->next) return sn;
	else return snode_getlast(sn->next);
}

void snode_delcat(stack_node* sn, int del_free) {
	if (sn->next) snode_delcat(sn->next, del_free);
	if (del_free) free(sn->data);
	free(sn);
}

stack* stack_new() {
	stack* o = calloc(1, sizeof(stack));
	return o;
}

void stack_set_free_on_clear(stack* o, int value) {
	o->del_free = value ? 1 : 0;
}

int stack_get_free_on_clear(stack* o) {
	return o->del_free;
}

int stack_empty(stack* o) {
	return !o->head;
}

void stack_push(stack* o, void* data) {
	stack_node* sn = calloc(1, sizeof(stack_node));
	sn->data = data;
	if (!stack_empty(o)) sn->next = o->head;
	o->head = sn;
}

void* stack_pop(stack* o) {
	if (stack_empty(o)) return NULL;
	stack_node* sn = o->head;
	o->head = sn->next;
	return sn->data;
}

void* stack_top(stack* o) {
	if (stack_empty(o)) return NULL;
	return o->head->data;
}

void stack_clear(stack* o) {
	snode_delcat(o->head, o->del_free);
	o->head = NULL;
}

void stack_delete(stack* o) {
	if (!stack_empty(o)) snode_delcat(o->head, o->del_free);
	free(o);
}