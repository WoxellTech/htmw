/**
 * NX Stack
 * (c) Woxell.co
 */
#ifndef NX_STACK
#define NX_STACK
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct node {
	void* data;
	struct node* next;
} stack_node;

typedef struct {
	stack_node* head;
	int del_free;
} stack;

stack_node* snode_getlast(stack_node* sn);
void snode_delcat(stack_node* sn, int del_free);
stack* stack_new();
void stack_set_free_on_clear(stack* o, int value);
int stack_get_free_on_clear(stack* o);
int stack_empty(stack* o);
void stack_push(stack* o, void* data);
void* stack_pop(stack* o);
void* stack_top(stack* o);
void stack_clear(stack* o);
void stack_delete(stack* o);

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // !NX_STACK