#ifndef STR_H
#define STR_H

#include <stdlib.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define str_isempty(s) (!((s).length))
#define nullstr ((string) { 0, 0, NULL })
#define str_isnull(s) (((s).c_str == NULL) && ((s).length == 0) && ((s).size == 0))

typedef struct {
    size_t length;
    size_t size;
    wchar_t* c_str;
} string;

string str_new(size_t max_size);
void str_delete(string* x);
void str_debug(string s);
string str_append(string* x, const wchar_t* s, size_t n);

#define str_append_ch(x, c) {if ((x)->length < (x)->size - 1) { (x)->c_str[(x)->length] = (c); (x)->length++; (x)->c_str[(x)->length] = L'\0'; }}

string _str_append_ch(string* x, wchar_t c);
string str_cpy(const string x);


#ifdef __cplusplus
}
#endif
#endif