/**
 * NX Vector
 * (c) Woxell.co
 */
#ifndef NX_VECTOR
#define NX_VECTOR

//#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VECT_TD(t, v, i) (*(t*)vect_at(v, i))
#define VECT_FC(x, y, z) (vect* x, size_t y, void *z)
#define VECT_FCN(name, x, y, z) void name(vect* x, size_t y, void *z)
#define VECT_FCND(name) void name(vect* v, size_t i, void *o)
#define VECT_FCD (vect* v, size_t i, void *o)
#define VECT_A(t, v) ((t*)(v->data))
#define VECT_FOR(x, i) for (size_t i = 0; i < x->size; i++)

	typedef struct
	{
		uint8_t* data;
		size_t size;
		size_t type_s;
		char config;
	} vect;

	vect* vect_new(size_t size, size_t type_s);
	//vect *vect_new_a(size_t size, size_t type_s, int *data);
	void* vect_at(vect* x, size_t idx);
	size_t vect_size(vect* x);
	void vect_resize(vect* x, size_t size);
	void vect_foreach(vect* x, void (*call_back)(vect *, size_t, void *));
	void vect_swap(vect *x, size_t e1, size_t e2);
	void vect_delete(vect* x);

#ifdef __cplusplus
}
#endif

#endif // NX_VECTOR
