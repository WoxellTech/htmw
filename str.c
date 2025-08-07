#include "str.h"

string str_new(size_t max_size) {
    wchar_t* data = (wchar_t*)malloc(max_size * sizeof(wchar_t));
    data[0] = L'\0';
    return (string) {
        .length = 0,
        .size = max_size,
        .c_str = data
    };
}

void str_delete(string* x) {
    if (x->size)
        free(x->c_str);
    *x = nullstr;
}

void str_debug(string s) {
    char c = L'\0';
    puts("---[string debug begin]---");
    printf("(length: %zu, size: %zu)\"", s.length, s.size);
    /*for (size_t i = 0; (c = s.c_str[i]) != L'\0'; i++) {
        putwc(c, stdout);
    }*/
    for (size_t i = 0; i < s.length; i++) {
        c = s.c_str[i];
        putwc(c, stdout);
    }
    putc('"', stdout);
    puts("---[string debug end]---");
}

string str_append(string* x, const wchar_t* s, size_t n) {
    if (x->c_str == NULL) return nullstr;
    if (s == NULL) return *x;
    size_t add_size = wcslen(s);
    add_size = add_size < n ? add_size : n;
    wcsncpy(x->c_str + x->length, s, add_size);
    x->length += add_size;
    x->c_str[x->length] = L'\0';
    return *x;
}

#define str_append_ch(x, c) {if ((x)->length < (x)->size - 1) { (x)->c_str[(x)->length] = (c); (x)->length++; (x)->c_str[(x)->length] = L'\0'; }}

string _str_append_ch(string* x, wchar_t c) {
    if (x->length < x->size - 1) {
        x->c_str[x->length] = c;
        x->length++;
        x->c_str[x->length] = L'\0';
    }
    return *x;
}

string str_cpy(const string x) {
    string o = (string){
        .length = x.length,
        .size = x.length + 1,
        .c_str = (wchar_t*)malloc((x.length + 1) * sizeof(wchar_t))
    };
#if DEBUG_MODE
    //printf("\n%u\n", x.length);
    //str_debug(x);
#endif
    wcsncpy(o.c_str, x.c_str, x.length);
    o.c_str[x.length] = L'\0';
    return o;
}