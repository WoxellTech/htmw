/**
 * NX Vector
 * (c) Woxell.co
 */
#include "nx/vector.h"

/*typedef struct
{
	uint8_t *data;
	size_t size;
	size_t type_s;
	char config;
} vect;*/

vect *vect_new(size_t size, size_t type_s)
{
	vect *x = (vect *)malloc(sizeof(vect));
	x->data = (uint8_t *)malloc(size * type_s);
	x->size = size;
	x->type_s = type_s;
	return x;
}

/*vect *vect_new_a(size_t size, size_t type_s, int *data)
{
    vect *x = vect_new(size, type_s);
    memcpy(x->data, data, size * type_s);
    return x;
}*/

void *vect_at(vect *x, size_t idx)
{
	if (idx < x->size)
		return (x->data + (idx * x->type_s));
	return NULL;
}

size_t vect_size(vect *x)
{
	return x->size;
}

void vect_resize(vect *x, size_t size)
{
	uint8_t *new_buffer = (uint8_t *)realloc(x->data, size * x->type_s);
	if (new_buffer == NULL)
	{
		// ...
	}
	x->data = new_buffer;
}

void vect_foreach(vect *x, void (*call_back)(vect *, size_t, void *))
{
	for (size_t i = 0; i < x->size; i++)
	{
		call_back(x, i, vect_at(x, i));
	}
}

void vect_swap(vect *x, size_t e1, size_t e2)
{
    //if (x->type_s < 16)
    void *buffer = malloc(x->type_s);
    memcpy(buffer, vect_at(x, e1), x->type_s);
    memcpy(vect_at(x, e1), vect_at(x, e2), x->type_s);
    memcpy(vect_at(x, e2), buffer, x->type_s);
    free(buffer);
}

void vect_delete(vect *x)
{
	free(x->data);
	free(x);
}
