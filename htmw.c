/**
 * (c) HTMW vT.5 by Woxell.co
 * 
 * [WARNING]
 * The current version is a test version, it's unstable and there are known memory leaks and unhandled errors.
 * The purpose for now is just to implement and test it's functionality without focusing 100% on performance & optimization.
 */
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include <locale.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "map.h"
#include "nx/vector.h"
#include "nx/list.h"
#include "nx/stack.h"
#include "flag.h"

#include "str.h"
#include "jsw.h"

#ifdef _WIN32
#pragma comment(lib, "lua54.lib")
#endif // _WIN32

#define DEBUG_MODE 0

#if DEBUG_MODE
#ifdef _WIN32
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#endif

#define MAX_LEN_IN  100000000
#define MAX_LEN_OUT 100000000
#define MAX_LEN_LN 10000
#define MAX_TEMPLATE_LEN 1000000

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__linux__)
    #include <unistd.h>
    #include <limits.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

char* get_executable_dir() {
    static char buffer[2048];

#if defined(_WIN32)
    DWORD len = GetModuleFileNameA(NULL, buffer, sizeof(buffer));
    if (len == 0 || len == sizeof(buffer)) return NULL;
    // Strip filename
    for (int i = len - 1; i >= 0; i--) {
        if (buffer[i] == '\\') {
            buffer[i + 1] = '\0';
            break;
        }
    }
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len == -1) return NULL;
    buffer[len] = '\0';
    char* last_slash = strrchr(buffer, '/');
    if (last_slash) *(last_slash + 1) = '\0';
#elif defined(__APPLE__)
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) return NULL;
    char real_path[2048];
    if (!realpath(buffer, real_path)) return NULL;
    strcpy(buffer, real_path);
    char* last_slash = strrchr(buffer, '/');
    if (last_slash) *(last_slash + 1) = '\0';
#else
    #error "Unsupported platform"
#endif

    return buffer;
}

#define T_ARG_NULLABLE      (1 << 0)
#define T_ARG_DOUBLEASS     (1 << 1)

typedef struct {
    string name;
    string default_value;
    flag_t flags;                               // nullable or double ass
} ht_template_arg;

#define T_ARGV_STRLT        (1 << 0)
#define T_ARGV_VALUE        (1 << 1)

typedef struct {
    string name;
    string value;
    flag_t flags;
} ht_template_arg_value;

typedef struct {
    size_t pos;
    size_t idx;
} field_location;

typedef struct {
    size_t shifts_count;                        // total number of shifts (field "tpyes" - 1, $INNER is excluded)
    size_t fields_count;                        // total number of fields on the gorund
    wchar_t* field_names;                       // 1 block of array of wide characters, where each field name is listed
    size_t* field_shifts;                       // contains indices of each field name string except the first one
    field_location* fields;                     // contains the info where each field is located on the ground (pos) and which placeholder (idx)
    string ground;                              // inner with invisible placeholders
} ht_template_core;

typedef struct {
    string name;
    vect* args;
    string impl;
    string inner;
    ht_template_core* core;
    int ninner;
} ht_template;

typedef union {
    ht_template* template;
} ht_any;

#define CTX_HEAD    (1 << 0)

typedef struct {
    u_map* includes;                            // map of strings (path, content) global includes are prefixed with `>`
    u_map* templates;                           // (name, ht_template)
    string input;
    lua_State* L;
    flag_t flags;
} htmw_context;

typedef struct {
    size_t begin;
    size_t length;
} str_frag;

typedef struct {
    size_t fields_count;                        // number of fields on the ground
    size_t* inner_shifts;
    size_t args_count;                          // 
    size_t* arg_shifts;                         // contains the indices of the arguments, starting from the 2nd argument, the last value is the address of the byte overflow (after the null terminator)
    wchar_t* args;                              // contains strings of value passed in order for the constructor
    wchar_t* inners;                            // contains strings of the inner for each widget
    field_location* fields;                     // contains the info where each field is located on the ground (pos) and which placeholder (idx)
    string ground;                              // content with invisible placeholders
} txt_template_fill_data;

ht_template_core* htmw_process_template_inner(ht_template t);
htmw_context htmw_preprocess(lua_State* L, string in, htmw_context* ectx);
txt_template_fill_data htmw_process_txt(htmw_context ctx);
string htmw_compile_txt(htmw_context ctx, txt_template_fill_data* tf);

int check_lua(lua_State* L, int r) {
    if (r != LUA_OK) {
        char *msg = lua_tostring(L, -1);
        printf("%s\n", msg);
        return 0;
    }
    return 1;
}

typedef enum {
    h_null,
    h_include,
    h_define,
    h_comment,
    h_template,
    h_block,
    h_no
} htmw_mode;

#define E_EXIT 1

void throw_error(const char* msg, flag_t flags) {
    printf("[HTMW Error]: %s\n", msg);
    if (flag_includes(flags, E_EXIT)) {
        puts("Compilation interrupted, exiting...");
        exit(1);
    }
}

string read_file(const char* path, int exe_location) {
    string s = str_new(MAX_LEN_IN);
    FILE* f = fopen(path, "r");

    if (f == NULL) {
        throw_error("Couldn't open input file(s)", E_EXIT);
    }

    wchar_t* buffer = (wchar_t*)malloc(MAX_LEN_LN * sizeof(wchar_t));
    while (fgetws(buffer, MAX_LEN_LN, f)) {
        str_append(&s, buffer, MAX_LEN_LN);
    }

    fclose(f);
    return s;
}

FILE* fopen_g(const char* filename, const char* mode) {
    char* base_dir = get_executable_dir();
    if (!base_dir) {
        throw_error("Could not determine executable directory\n", E_EXIT);
        return NULL;
    }

    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s%s", base_dir, filename);

    return fopen(full_path, mode);
}

// creates a list of arguments in a given template and a given piece of a string (insde the parenthesis), `n` is the max allocation size
void parse_template_args(ht_template* t, string in, size_t n) {
    //printf("_________________n: %u___________________", n);
    string* args_split = (string*)malloc(n * sizeof(string));
    flag_t* args_flag = (flag_t*)calloc(n, sizeof(flag_t));
    string* args_default = (string*)malloc(n * sizeof(string));

#if DEBUG_MODE
    //str_debug(in);
#endif

    size_t count = 0;
    wchar_t c;
    int arg_phase = 0;
    size_t last_arg_begin_idx = 0;
    wchar_t delim = L'\0';
    size_t last_default_begin_idx = 0;
    for (size_t i = 0; i < in.length; i++) {
        c = in.c_str[i];
        //printf("c:'%lc'\tph:\t%d\n", c, arg_phase);
        switch (arg_phase) {
        case 0:
            args_default[count] = nullstr;
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {
                continue;
            } else if (c >= L'a' && c <= L'z' || c >= L'A' && c <= L'Z' || c >= L'0' && c <= L'9' || c == L'_') {
                last_arg_begin_idx = i;
                args_split[count].c_str = in.c_str + i;
                arg_phase = 1;
                if (i == in.length - 1) {
                    arg_phase = 0;
                    args_split[count++].length = i - last_arg_begin_idx + 1;
                }
            } else {
                // TODO: throw excpetion
            }
            break;
        case 1:
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {
                arg_phase = 2;
                args_split[count].length = i - last_arg_begin_idx;
            } else if (c == L'?') {
                arg_phase = 2;
                args_split[count].length = i - last_arg_begin_idx;
                args_flag[count] |= T_ARG_NULLABLE;
            } else if (c == L'=') {
                arg_phase = 3;
                args_split[count].length = i - last_arg_begin_idx;
            } else if (c == L',') {
                arg_phase = 0;
                args_split[count++].length = i - last_arg_begin_idx;
            } else if (i == in.length - 1) {
                arg_phase = 0;
                args_split[count++].length = i - last_arg_begin_idx + 1;
            } else {
                // TODO: throw excpetion
            }
            break;
        case 2:
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {

            } else if (c == L',' || i == in.length - 1) {
                count++;
                arg_phase = 0;
            } else if (c == L'=') { // default arg
                // for now throw exception, it will be implemented in the future
                arg_phase = 3;
            } else if (c == L'?') {
                args_flag[count] |= T_ARG_NULLABLE;
            } else {
                // TODO: throw excpetion
            }
            break;
        case 3:
            //puts("-----3-----");
            // what now :3
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {

            } else if (c == L'"') {
                args_default[count].c_str = in.c_str + i + 1;
                last_default_begin_idx = i + 1;
                delim = L'"';
                arg_phase = 4;
            } else if (c == L'\'') {
                args_default[count].c_str = in.c_str + i + 1;
                last_default_begin_idx = i + 1;
                delim = L'\'';
                arg_phase = 4;
            } else if (c == L',') {
                // TODO: handle exception
                throw_error("Argument with default values must have a value", E_EXIT);
            } else {
                args_default[count].c_str = in.c_str + i;
                last_default_begin_idx = i;
                delim = L'\0';
                arg_phase = 4;
            }
            break;
        case 4:
            // case4
            //if (count == 2) puts("3->4");
            if ((c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') && delim == L'\0') {
                args_default[count++].length = i - last_default_begin_idx;
                arg_phase = 5;
            } else if (c == L'"' && delim == L'"') {
                args_default[count++].length = i - last_default_begin_idx;
                arg_phase = 5;
            } else if (c == L'\'' && delim == L'\'') {
                args_default[count++].length = i - last_default_begin_idx;
                arg_phase = 5;
            } else if (c == L',' && delim == L'\0') {
                args_default[count++].length = i - last_default_begin_idx;
                arg_phase = 0;
            } else if (i == in.length - 1 && delim == L'\0') {
                args_default[count++].length = i - last_default_begin_idx + 1;
                arg_phase = 0;
            } else {

            }
            break;
        case 5:
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {

            } else if (c == L',' || i == in.length - 1) {
                arg_phase = 0;
            } else {
                // TODO: throw excpetion
            }
            break;
        }
    }

    vect* args = vect_new(n, sizeof(ht_template_arg));
    VECT_FOR(args, i) {
        VECT_A(ht_template_arg, args)[i] = (ht_template_arg){
            str_cpy(args_split[i]),
            str_isnull(args_default[i]) ? nullstr : str_cpy(args_default[i]),
            args_flag[i]
        };
#if DEBUG_MODE
        ht_template_arg a = VECT_A(ht_template_arg, args)[i];
        printf("\nname: |%ls|\ndefault: |%ls|\nnullable: %d\n-------\n",
            a.name.c_str,
            a.default_value.c_str,
            flag_includes(a.flags, T_ARG_NULLABLE)
        );
#endif
    }

    t->args = args;

    free(args_split);
    free(args_flag);

    /*for (size_t i = 0; i < n; i++) {
        printf("<<<<< \"%ls\" >>>>>\n", VECT_A(string, args)[i].c_str);
    }*/
}

// converts wide string to string and constructs the function for the template constructor
char* generate_lua_template_constructor(const string name, const string impl, const vect* args, ht_template_core* core) {
    //setlocale(LC_ALL, "");

    char** args_c_str = (char**)malloc(args->size * sizeof(char*));
    size_t* args_length = (size_t*)malloc(args->size * sizeof(size_t));

    char* name_c_str = (char*)malloc((name.length + 1) * sizeof(char));
    wcstombs(name_c_str, name.c_str, name.length);
    name_c_str[name.length] = '\0';

    size_t total_args_len = 0;
    VECT_FOR(args, i) {
        ht_template_arg e = VECT_A(ht_template_arg, args)[i];
        args_c_str[i] = (char*)malloc((e.name.length + 1) * sizeof(char));
        wcstombs(args_c_str[i], e.name.c_str, e.name.length);
        args_c_str[i][e.name.length] = '\0';
        args_length[i] = e.name.length;
        total_args_len += e.name.length;
    }
    
    /*
    * sum: "function " + "__template_construct_" + name + total_args + commas + parenthesis + new line + null terminator
    */

    const size_t lua_constructor_signature_len = 9 + 21 + name.length + total_args_len + (args->size > 1 ? 2 * (args->size - 1) : 0) + 2 + 1;
    //puts("cc");
    //printf("LASL: %zu\n", lua_constructor_signature_len);
    char* lua_contstructor_signature = (char*)malloc((lua_constructor_signature_len + 1) * sizeof(char));
    strncpy(lua_contstructor_signature, "function __template_construct_", 30);
    strncpy(lua_contstructor_signature + 30, name_c_str, name.length);
    size_t lua_contstructor_signature_idx = 30 + name.length;
    lua_contstructor_signature[lua_contstructor_signature_idx++] = '(';
    VECT_FOR(args, i) {
        strncpy(lua_contstructor_signature + lua_contstructor_signature_idx, args_c_str[i], args_length[i]);
        lua_contstructor_signature_idx += args_length[i];
        if (i != args->size - 1) {
            lua_contstructor_signature[lua_contstructor_signature_idx++] = ',';
            lua_contstructor_signature[lua_contstructor_signature_idx++] = ' ';
        }
    }
    lua_contstructor_signature[lua_contstructor_signature_idx++] = ')';
    lua_contstructor_signature[lua_contstructor_signature_idx++] = '\n';
    lua_contstructor_signature[lua_contstructor_signature_idx++] = '\0';

    //printf("________construct____>>>> \"%s\" <<<<", lua_contstructor_signature);

    /*
    * sum: "return {" & "}" + `["` & `"]=` + names aggregation + commas + NO null terminator
    */
    // sum: (length of the block + 1) - (number of variables (remove null terminator)) - ("INNER" + null terminator)
    size_t aggr_core_names_len = core->shifts_count ? core->field_shifts[core->shifts_count - 1] - (core->shifts_count) - 6 : 0;
    //printf("..................>>>>>>>>....>>>> aggr_core_names_len: %zu <<<<\n", aggr_core_names_len);
    const size_t lua_ret_block_len = 1 + 9 + (core->shifts_count * 15) + (2 * aggr_core_names_len) + (core->shifts_count > 1 ? 1 * (core->shifts_count - 1) : 0) + 0;
    //printf("__%zu, %zu, %zu\n", (core->shifts_count * 15), (2 * aggr_core_names_len), (core->shifts_count > 1 ? 1 * (core->shifts_count - 1) : 0));
    //printf("_calc sz: %zu\n", lua_ret_block_len);
    char* lua_ret_block = (char*)malloc((lua_ret_block_len + 1) * sizeof(char));
    size_t lua_ret_block_idx = 9;
    strncpy(lua_ret_block, "\nreturn {", 10);

    //char* var_names_mb = (char*)malloc((core->field_shifts[core->shifts_count - 1] - core->field_shifts[0]) * sizeof(char));
    //wcstombs(var_names_mb, core->field_names + core->field_shifts[0], core->field_shifts[core->shifts_count] - core->field_shifts[0] - 1);
    /*i < core->field_shifts[core->shifts_count] - core->field_shifts[0] - 1*/

    for (size_t i = 0; i < core->shifts_count; i++) {
        //printf("_idx: %zu\n", lua_ret_block_idx);
        size_t size = core->field_shifts[i] - (i ? core->field_shifts[i - 1] : 6);
        size_t shift = i ? core->field_shifts[i - 1] : 6;
        char* var_name = (char*)malloc(size * sizeof(char));
        wcstombs(var_name, core->field_names + shift, size);
        var_name[size - 1] = '\0';
        //printf("|(size: %zu, shift: %zu)__%s__", size, shift, var_name);

        lua_ret_block[lua_ret_block_idx] = '[';
        lua_ret_block[lua_ret_block_idx + 1] = '"';
        strncpy(lua_ret_block + lua_ret_block_idx + 2, var_name, size - 1);
        lua_ret_block_idx += size + 1;
        lua_ret_block[lua_ret_block_idx] = '"';
        lua_ret_block[lua_ret_block_idx + 1] = ']';
        lua_ret_block[lua_ret_block_idx + 2] = '=';
        lua_ret_block[lua_ret_block_idx + 3] = 't';
        lua_ret_block[lua_ret_block_idx + 4] = 'o';
        lua_ret_block[lua_ret_block_idx + 5] = 's';
        lua_ret_block[lua_ret_block_idx + 6] = 't';
        lua_ret_block[lua_ret_block_idx + 7] = 'r';
        lua_ret_block[lua_ret_block_idx + 8] = 'i';
        lua_ret_block[lua_ret_block_idx + 9] = 'n';
        lua_ret_block[lua_ret_block_idx + 10] = 'g';
        lua_ret_block[lua_ret_block_idx + 11] = '(';
        strncpy(lua_ret_block + lua_ret_block_idx + 12, var_name, size - 1);
        lua_ret_block_idx += size + 9;
        lua_ret_block[lua_ret_block_idx + 2] = ')';
        if (i != core->shifts_count - 1) {
            lua_ret_block[lua_ret_block_idx + 3] = ',';
            lua_ret_block_idx++;
        }
        lua_ret_block_idx += 3;

        free(var_name);
    }
    lua_ret_block[lua_ret_block_idx++] = '}';
    //printf("idx: %zu, len: %zu", lua_ret_block_idx, lua_ret_block_len);
    lua_ret_block[lua_ret_block_idx] = '\0';
    
    //printf("(sz: %zu)____%s____\n", lua_ret_block_idx, lua_ret_block);

    char* impl_c_str = (char*)malloc((impl.length + 1/* + 5*/) * sizeof(char));
    size_t impl_c_str_len = wcstombs(impl_c_str, impl.c_str, impl.length);
    //strncpy(impl_c_str + impl.length, "\nend\n", 5);
    impl_c_str[impl.length] = '\0';

    char* out = (char*)malloc((lua_constructor_signature_len + impl.length + 5 + 1 + lua_ret_block_len) * sizeof(char));
    strncpy(out, lua_contstructor_signature, lua_constructor_signature_len);
    out[lua_constructor_signature_len] = '\0';
    //strncat(out, impl_c_str, lua_constructor_signature_len + impl.length + 5);
    strncat(out, impl_c_str, lua_constructor_signature_len + impl.length);
    out[lua_constructor_signature_len + impl.length] = '\0';
    strncat(out, lua_ret_block, lua_constructor_signature_len + impl.length + lua_ret_block_len);
    out[lua_constructor_signature_len + impl.length + lua_ret_block_len] = '\0';
    strncat(out, "\nend\n", lua_constructor_signature_len + impl.length + lua_ret_block_len + 5);
    out[lua_constructor_signature_len + lua_ret_block_len + impl.length + 5] = '\0';

    free(args_length);
    free(name_c_str);
    VECT_FOR(args, i) {
        free(args_c_str[i]);
    }
    free(args_c_str);
    free(lua_contstructor_signature);
    free(impl_c_str);

    //printf("\n\n\n%d\n\n\n%s\n\n\n\n\n\n", impl_c_str_len, impl_c_str);
    //printf("\n\n\n\n\n\n>>%s<<\n\n\n\n\n\n", out);
    return out;
}

/*typedef struct {
    field_location fl;
    string path;
    flag_t flags;
} include_frag;*/

typedef struct {
    size_t idx;
    size_t replaced_length;
    string path;
    string* content;
} include_dir;

#define INCLUDE_ONCE 1
#define INCLUDE_GLOB 2

void htmw_apply_includes(string* in, u_map* includes) {
    list* dirs = list_new();    // list of include_dir
    size_t difference = 0;
    wchar_t c;
    //printf("in->length: %zu\n", in->length);
    for (size_t idx = 0; idx < in->length; idx++) {
        c = in->c_str[idx];
        //printf("i: %zu\tlen: %zu\tc: '%lc'\n", idx, in->length, c);
        if (c == L'<') {
            if (!wcsncmp(in->c_str + idx + 1, L"@include", 8)) {
#if DEBUG_MODE
                //printf("after include kw: \"%lc\"\n", in->c_str[idx + 1 + 8]);
#endif
                wchar_t c;
                uint8_t phase = 0;
                flag_t flags = 0;
                size_t path_begin_idx = 0;
                string view = nullstr;
                wchar_t delim = L'\0';
                include_dir* incl = (include_dir*)malloc(sizeof(include_dir));
                for (size_t i = 0; (c = in->c_str[idx + 1 + 8 + i]) != L'\0'; i++) {
                    switch (c) {
                    case L' ':
                    case L'\n':
                    case L'\t':
                    case L'\r':
                        if (!phase) {
                            phase = 1;
                        } else if (phase == 4) {
                            throw_error("Tags cannot contain whitespaces at the end (after the '/')", E_EXIT);
                        }
                        break;
                    case L'"':
                    case L'\'':
                        if (phase == 0 || phase == 1) {
                            view.c_str = in->c_str + idx + 1 + 8 + i + 1;
                            path_begin_idx = i;
                            phase = 2;
                            delim = c;
                        } else if (phase == 2) {
                            if (c == delim) {
                                view.length = i - 1 - path_begin_idx;
                                phase = 3;
                            }
                        } else {
                            throw_error("Invalid character ''' or '\"' in include directive", E_EXIT);
                        }
                        break;
                    case L'<':
                        if (phase == 0 || phase == 1) {
                            flags |= INCLUDE_GLOB;
                            view.c_str = in->c_str + idx + 1 + 8 + i + 1;
                            path_begin_idx = i;
                            phase = 2;
                            delim = L'>';
                        } else {
#if DEBUG_MODE
                            //printf("after include: \"%lc\"\n", in->c_str[idx + 1 + 8 + i + 1]);
#endif
                            throw_error("Invalid character '<' in include directive", E_EXIT);
                        }
                        break;
                    case L'>':
#if DEBUG_MODE
                            //printf("phase: \"%u\"\n", phase);
#endif
                        if (phase == 2) {
                            if (delim == L'>') {
                                view.length = i - 1 - path_begin_idx;
                                phase = 3;
                            } else {
                                // throw exception?
                            }
                        } else if (phase == 3 || phase == 4) {
                            incl->idx = idx - difference;
                            incl->replaced_length = i + 9 + 1;
                            //printf("lenn: %zu\n", i + 9);
                            difference += incl->replaced_length;
                            goto include_for_break;
                        } else {
                            throw_error("No include path specified", E_EXIT);
                        }
                        break;
                    case L'/':
                        if (phase == 3)
                            phase = 4;
                        break;
                    default:
                        if (!phase) {
                            if (!wcsncmp(in->c_str + idx + 1 + 8 + i, L"_once", 5)) {
                                flags |= INCLUDE_ONCE;
                                phase = 1;
                                i += 4;
                            } else {
                                throw_error("Invalid character in include directive", E_EXIT);
                            }
                        } else if (phase != 2) {
                            throw_error("Invalid character in include directive", E_EXIT);
                        }
                    }
                }
                include_for_break:;
#if DEBUG_MODE
                /*puts("<------------------->");
                printf("%zu\t%zu\t%p\n", view.length, view.size, view.c_str);
                printf("once: %d\n", flag_includes(flags, INCLUDE_ONCE));
                printf("glob: %d\n", flag_includes(flags, INCLUDE_GLOB));
                str_debug(view);*/
#endif
                //include_frag* incl = (include_frag*)malloc(sizeof(include_frag));
                //incl->path = str_cpy(view);
                //string name = str_cpy(view);
                string path;
                string* content = NULL;
                if (flag_includes(flags, INCLUDE_GLOB)) {
                    path = str_new(view.length + 2);
                    str_append_ch(&path, L'>');
                    str_append(&path, view.c_str, view.length);
                } else {
                    path = str_cpy(view);
                }
                
                if (!map_get(includes, path.c_str, &content)) {
                    //map_set(includes, path.c_str, (void*)content, 0);

                    content = (string*)malloc(sizeof(string));
                    if (flag_includes(flags, INCLUDE_GLOB)) {
                        
                        char* path_c_str = (char*)malloc((path.length + 1 + 8) * sizeof(char));
                        sprintf(path_c_str, "include/%ls", path.c_str + 1);
                        //wcstombs(path_c_str, path.c_str + 1, path.length);
                        path_c_str[path.length + 8] = '\0';
                        if (!strncmp(path_c_str + 8, "https://", 8) || !strncmp(path_c_str + 8, "http://", 7)) {
                            // TODO: implement HTTP Include
                            throw_error("HTTP Include is not implemented yet", E_EXIT);
                        } else {
#if DEBUG_MODE
                            //printf("path: %s\n", path_c_str);
#endif
                            *content = read_file(path_c_str, 1);
                            htmw_apply_includes(content, includes);
                        }
                    
                        free(path_c_str);
                    } else {
                        char* path_c_str = (char*)malloc((path.length + 1) * sizeof(char));
                        wcstombs(path_c_str, path.c_str, path.length);
                        path_c_str[path.length] = '\0';
                        *content = read_file(path_c_str, 0);
                        htmw_apply_includes(content, includes);
                        free(path_c_str);
                    }
                } else if (flag_includes(flags, INCLUDE_ONCE)) {
                    //puts("############################");
                    content = NULL;
                    //break;
                } else {
                    //printf("content: %p\n", content);
                    //////htmw_apply_includes(content, includes);



                    content = (string*)malloc(sizeof(string));
                    if (flag_includes(flags, INCLUDE_GLOB)) {
                        
                        char* path_c_str = (char*)malloc((path.length + 1 + 8) * sizeof(char));
                        sprintf(path_c_str, "include/%ls", path.c_str + 1);
                        //wcstombs(path_c_str, path.c_str + 1, path.length);
                        path_c_str[path.length + 8] = '\0';
                        if (!strncmp(path_c_str + 8, "https://", 8) || !strncmp(path_c_str + 8, "http://", 7)) {
                            // TODO: implement HTTP Include
                            throw_error("HTTP Include is not implemented yet", E_EXIT);
                        } else {
#if DEBUG_MODE
                            //printf("path: %s\n", path_c_str);
#endif
                            *content = read_file(path_c_str, 1);
                            htmw_apply_includes(content, includes);
                        }
                    
                        free(path_c_str);
                    } else {
                        char* path_c_str = (char*)malloc((path.length + 1) * sizeof(char));
                        wcstombs(path_c_str, path.c_str, path.length);
                        path_c_str[path.length] = '\0';
                        *content = read_file(path_c_str, 0);
                        htmw_apply_includes(content, includes);
                        free(path_c_str);
                    }


                }
                incl->path = path;
                incl->content = content;
#if DEBUG_MODE
                puts("_____________________________________");
                if (content != NULL)
                    str_debug(*content);
                else
                    puts("(null)");
#endif
                map_set(includes, path.c_str, (void*)content, 0);
                list_add(dirs, (void*)incl); // TODO: fix idx and length and replace includes with their file content
#if DEBUG_MODE
                printf("idx: %zu\nlen: %zu\npath: \"%ls\"\n-------\n\n", incl->idx, incl->replaced_length, incl->path.c_str);
#endif
            }
        }
    }
#if DEBUG_MODE
    /*u_map_node* incl = includes->head;
    puts("---[U_MAP]---");
    while (incl != NULL) {
        include_dir* dir = (include_dir*)incl->data;
        if (dir != NULL)
            printf("\"%ls\"\n", dir->path);
        else puts("(null)");
        incl = incl->next;
    }
    puts("---[U_END]---");*/

    list_node* incl = dirs->head;
    puts("---[U_MAP]---");
    while (incl != NULL) {
        include_dir* dir = (include_dir*)incl->data;
        if (dir != NULL) {
            printf("\"%ls\"\n", dir->path.c_str);
        } else puts("(null)");
        incl = incl->next;
    }
    puts("---[U_END]---");

    //printf("TOT diff: %zu\n", difference);
#endif
    size_t includes_len_tot = 0;

    for (size_t i = 0; i < dirs->size; i++) {
        include_dir* dir = (include_dir*)list_get(dirs, i);
        //str_debug(*dir->content);
    }

    list_node* ln = dirs->head;
    while (ln != NULL) {
        include_dir* dir = (include_dir*)ln->data;
        if (dir->content != NULL)
            includes_len_tot += dir->content->length;

        ln = ln->next;
    }

    //printf("TOT len: %zu\n", includes_len_tot);

    string n = str_new(in->length + includes_len_tot - difference + 1);

    ln = dirs->head;
    include_dir* dir = NULL;
    size_t i = 0;
    if (ln != NULL) {
        dir = (include_dir*)ln->data;
    } else goto include_rewrite_skip_cond;
    difference = 0;
    for (; i < in->length; i++) {
        //printf("i: %zu\n", i);
        if (/*dir != NULL && */i - difference == dir->idx) {
            string* content = dir->content;
            if (content != NULL) {
                for (size_t j = 0; j < content->length; j++) {
                    str_append_ch(&n, content->c_str[j]);
                    //printf(".");
                    //putwc(content->c_str[i], stdout);
                }
            }
            i += dir->replaced_length - 1;
            difference += dir->replaced_length;
            ln = ln->next;
            if (ln != NULL) {
                dir = (include_dir*)ln->data;
            } else {
                ++i;
                break;
            }
        } else {
            str_append_ch(&n, in->c_str[i]);
            //printf("_");
            //putwc(in->c_str[i], stdout);
        }
    }
    include_rewrite_skip_cond:
    for (; i < in->length; i++) {
        str_append_ch(&n, in->c_str[i]);
    }
    str_delete(in);
    *in = n;
}

// parses all the <@...> tags, and returns a clean string with saved templates + state in the context struct
htmw_context htmw_preprocess(lua_State* L, string in, htmw_context* ectx) {
    string post_in = str_new(MAX_LEN_OUT);

    u_map* templates = ectx == NULL ? map_new() : ectx->templates;
    u_map* includes = ectx == NULL ? map_new() : ectx->includes;

    //if (ectx == NULL)             commented out in case users want dynamic includes
    htmw_apply_includes(&in, includes);

    int comment_mode = 0;
    int php_mode = 0;
    size_t ntxt_out_count = 0;
    size_t last_idx = 0;
    htmw_mode mode = h_null;

    wchar_t c;

    ht_any current;
    string current_template_name = nullstr;
    for (size_t idx = 0; (c = in.c_str[idx]) != L'\0'; idx++) {
        //putwc(c, stdout);
        switch (mode) {
        case h_null:
            /*if (in.c_str[idx + 1] == L'\0') {
                //last_idx = idx;
                str_append(&post_in, in.c_str + last_idx, idx - last_idx + 1);
            } */if (comment_mode) {
                if (c == L'-') {
                    if (!wcsncmp(in.c_str + idx, L"-->", 3)) {
                        comment_mode = 0;
                        idx += 2;
                    } else continue;
                } else continue;
            }
            if (php_mode) {
                if (c == L'?') {
                    if (!wcsncmp(in.c_str + idx, L"?>", 2)) {
                        php_mode = 0;
                        idx += 1;
                    } else continue;
                } else continue;
            }

            if (c == L'<') {
                if (!wcsncmp(in.c_str + idx + 1, L"@template ", 10)) {
                    //str_append(&post_in, in.c_str + last_idx, idx - last_idx - 1);
                    mode = h_template;
                    ht_template* t = (ht_template*)malloc(sizeof(ht_template));
                    t->ninner = 0;
                    t->inner = str_new(MAX_TEMPLATE_LEN);
                    t->impl = nullstr;
                    current.template = t;

                    wchar_t c;
                    string name = str_new(128);
                    size_t i = 0;
                    idx += 10;
                    int sty = 0;
                    for (; (c = in.c_str[idx + i]) != L'\0'; i++) {
                        if (c >= L'a' && c <= L'z' || c >= L'A' && c <= L'Z' || c >= L'0' && c <= L'9' || c == L'_'/* || c == L'-'*/) {
                            if (!sty && !wcsncmp(in.c_str + idx + i, L"ninner", 6) && (in.c_str[idx + i + 6] == L' ' || in.c_str[idx + i + 6] == L'\n' || in.c_str[idx + i + 6] == L'\t' || in.c_str[idx + i + 6] == L'\r')) {
                                t->ninner = 1;
                                idx += 6;
                                continue;
                            }
                            sty = 1;
                            str_append_ch(&name, c);
                        } else if (str_isempty(name)) {
                            continue;
                        } else {
                            break;
                        }
                    }
                    t->name = name;
                    string args = nullstr;// = str_new(2048);
                    string impl = nullstr;
                    int constructing_constructor = 0;
                    size_t constructor_begin_idx_args = 0;
                    size_t constructor_begin_idx_impl = 0;
                    size_t args_count = 0;
                    size_t args_len = 0;
                    size_t impl_idx_begin = 0;
                    int impl_inner_braces = 0;
                    wchar_t in_str_type = '\0';
                    int in_str_backslashing = 0;
                    char lua_comment_mode = 0;
                    size_t temp = i;
                    for (; (c = in.c_str[idx + i]) != L'\0'; i++) {
                        if (i - temp >= 60)
                            printf("");
                        if (in_str_backslashing) {
                            in_str_backslashing = 0;
                            continue;
                        }
                        switch (c) {
                        case L'(':
                        {
                            if (constructing_constructor == 0) {
                                args_len = 0;
                                constructing_constructor = 1;
                                constructor_begin_idx_args = idx + i + 1;
                                args.c_str = in.c_str + constructor_begin_idx_args;
                            } else if (constructing_constructor == 1) {
                                args_len++;
                            } else {
                                // TODO: throw exception
                            }
                        } break;
                        case L',':
                        {
                            if (constructing_constructor == 1) {
                                args_len++;
                                if (in_str_type == L'\0')
                                    args_count++;
                            }
                        } break;
                        case L')':
                        {
                            if (constructing_constructor == 1) {
                                if (in_str_type != L'\0') {
                                    args_len++;
                                    break;
                                }
                                constructing_constructor = 2;
                                args.length = args_len;
                                args.size = args_len + 1;
                                //printf(">>>>>>>>>>>>>> args_len: %u >>>>>>>>>>>\n", args_len);
                                parse_template_args(t, args, args_count);
                            } else {
                                // TODO: throw exception
                            }
                        } break;
                        case L'{':
                        {
                            if (constructing_constructor == 2 || constructing_constructor == 0) {
                                constructing_constructor = 3;
                                constructor_begin_idx_impl = idx + i + 1;
                                impl_idx_begin = i;
                                impl.c_str = in.c_str + constructor_begin_idx_impl;
                                in_str_type = L'\0';
                            } else if (constructing_constructor == 3 && in_str_type == L'\0' && !lua_comment_mode) {
                                impl_inner_braces++;
                            } else if (constructing_constructor == 1) {
                                args_len++;
                            } else {
                                // TODO: throw exception
                            }
                        } break;
                        case L'}':
                        {
                            if (constructing_constructor == 3 && in_str_type == L'\0' && !lua_comment_mode) {
                                if (!impl_inner_braces) {
                                    constructing_constructor = 4;
                                    impl.length = i - impl_idx_begin - 1;
                                    impl.size = impl.length + 1;
                                    t->impl = str_cpy(impl);
                                } else {
                                    impl_inner_braces--;
                                }
                            } else if (constructing_constructor == 1) {
                                args_len++;
                            } else {
                                // TODO: throw exception
                            }
                        } break;
                        case L'\\':
                        {
                            if (constructing_constructor == 3) {
                                if (in_str_type != L'\0') {
                                    in_str_backslashing = !in_str_backslashing;
                                }
                            }
                        } break;
                        case L'"':
                        case L'\'':
                        {
                            if (constructing_constructor == 3 && !lua_comment_mode) {
                                if (in_str_type == L'\0') {
                                    in_str_type = c;
                                } else if (in_str_type == c && !in_str_backslashing) {
                                    in_str_type = L'\0';
                                }
                            } else if (constructing_constructor == 1) {
                                // default args
                                if (in_str_type == L'\0') {
                                    in_str_type = c;
                                } else if (in_str_type == c/* && !in_str_backslashing*/) {
                                    in_str_type = L'\0';
                                }
                                args_len++; //
                            } else {
                                // TODO: throw exception
                            }
                        } break;
                        case L'\n':
                        case L'\r':
                        {
                            if (constructing_constructor == 3) {
                                if (lua_comment_mode == 1) {
                                    lua_comment_mode = 0;
                                } else if (in_str_type == L'"' || in_str_type == L'\'') {
                                    in_str_type = L'\0';
                                }
                            }
                        }
                        case L' ':
                        case L'\t':
                        {
                            if (constructing_constructor == 1) {
                                args_len++;
                            }
                            //continue;
                            //goto pre_process_template_for_switch_continue;
                        } break;
                        case L'>':
                        {
                            if (constructing_constructor == 1) {
                                args_len++;
                                // TODO: throw exception
                            } else if (constructing_constructor != 3) {
                                //printf("_____--------___%lc____%u\n\n", c, idx + i);
                                idx += i;
                                goto pre_process_for_switch_continue;
                                //break;
                            };
                        } break;
                        default:
                        {
                            if (constructing_constructor == 1) {
                                if (!args_count) args_count++;
                                args_len++;
                            } else if (constructing_constructor == 3) {
                                if (c == L'-' && !lua_comment_mode && in_str_type == L'\0') {
                                    if (!wcsncmp(in.c_str + idx + i, L"--[[", 4)) {
                                        //puts("lua_comment_mode = 2");
                                        i += 3;
                                        lua_comment_mode = 2;
                                    } else if (!wcsncmp(in.c_str + idx + i, L"--", 2)) {
                                        //puts("lua_comment_mode = 1");
                                        i++;
                                        lua_comment_mode = 1;
                                    }
                                } else {
                                    if (c == L']' && !wcsncmp(in.c_str + idx + i, L"]]", 2)) {
                                        if (lua_comment_mode == 2) {
                                            i++;
                                            //puts("lua_comment_mode = 0");
                                            lua_comment_mode = 0;
                                        } else if (in_str_type = L']' && !in_str_backslashing) {
                                            i++;
                                            in_str_type = L'\0';
                                        }
                                    } else if (c == L'[' && !wcsncmp(in.c_str + idx + i, L"[[", 2) && !lua_comment_mode) {
                                        i++;
                                        in_str_type = L'[';
                                    }
                                }
                            }
                        } break;
                        }
                        //pre_process_template_for_switch_continue:
                    }
                } else if (!wcsncmp(in.c_str + idx + 1, L"@no>", 4)) {
                    //puts("no mode");
                    ///str_append(&post_in, in.c_str + last_idx, idx - last_idx - 1); // NEGATIVE SUM ON ZERO
                    mode = h_no;
                    goto pre_process_for_switch_continue;
                } else if (!wcsncmp(in.c_str + idx + 1, L"@comment>", 9)) {
                    //str_append(&post_in, in.c_str + last_idx, idx - last_idx - 1);
                    mode = h_comment;
                    goto pre_process_for_switch_continue;
                } else if (!wcsncmp(in.c_str + idx + 1, L"@ntxt>", 6)) {
                    ntxt_out_count++;
                    idx += 6;
                    goto pre_process_for_switch_continue;
                } else if (!wcsncmp(in.c_str + idx + 1, L"/@ntxt>", 7)) {
                    if (ntxt_out_count > 0) {
                        ntxt_out_count--;
                    }
                    idx += 7;
                    goto pre_process_for_switch_continue;
                } else if (ntxt_out_count == 0) {
                    str_append_ch(&post_in, in.c_str[idx])
                }
                //printf("a");
                //map_set(templates, "")
            } else if (ntxt_out_count == 0) {
                str_append_ch(&post_in, in.c_str[idx])
            }
        pre_process_for_switch_continue:;
            break;
        case h_template:
            //puts("----------------");
            //printf("__/___%lc____", c);
            if (c == L'<' && !wcsncmp(in.c_str + idx + 1, L"/@template>", 11)) {
                //puts("___________________________________________________________________________________");
                mode = h_null;
                map_set(templates, current.template->name.c_str, current.template, 0);
#if DEBUG_MODE
                //printf(">>>>>>>>>>>>>>>>>>>>>>> \"%ls\" registered >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n", current.template->name.c_str);
                //str_debug(current.template->impl);
#endif

                current.template->core = htmw_process_template_inner(*current.template);
                char* lua_impl = generate_lua_template_constructor(current.template->name, current.template->impl, current.template->args, current.template->core);
                //printf("___>>>%s<<<___", lua_impl);

                int r = luaL_dostring(L, lua_impl);
                if (check_lua(L, r)) {
                    //luaL_dostring(L, "__template_construct_template('x', 'y', 'z')");
                    //htmw_process_template_inner(*current.template);
                }

                idx += 11;
                last_idx = idx + 1;
            } else {
                str_append_ch(&current.template->inner, c);
            }
            break;
        case h_no:
            if (c == L'<' && !wcsncmp(in.c_str + idx + 1, L"/@no>", 5)) {
                mode = h_null;
                idx += 5;
                last_idx = idx + 1;
            }
            break;
        case h_comment:
            if (c == L'<' && !wcsncmp(in.c_str + idx + 1, L"/@comment>", 10)) {
                mode = h_null;
                idx += 10;
                last_idx = idx + 1;
            }
            break;
        }
    }
    flag_t flags = 0;
    if (ectx == NULL) {
        flags |= CTX_HEAD;
    }
    return (htmw_context) {
        includes,
        templates,
        post_in,
        L,
        flags
    };
}

// generates a template core (bare bones template information) from a given template
ht_template_core* htmw_process_template_inner(ht_template t) {
    const string inner = t.inner;
    string ground = str_new(t.inner.length + 1);
    u_map* variables = map_new();
    list* fields = list_new();

    int dollar_mode = 0;
    size_t last_dollar_idx_begin = 0;
    size_t last_dollar_len = 0;

    size_t difference = 0;

    size_t* len_buffer;
    field_location* idx_buffer;
    size_t var_name_alloc_size_tot = 6;
    //list_add(var_idxs, idx);
    //out.fields_count = 0;
    for (size_t i = 0; i < inner.length + 1; i++) {
        wchar_t c = inner.c_str[i];

        if (dollar_mode) {
            if (c >= L'a' && c <= L'z' || c >= L'A' && c <= L'Z' || c >= L'0' && c <= L'9' || c == L'_') {
                last_dollar_len++;
                continue;
            } else if (c == L'$') {
                if (!last_dollar_len) { // $$
                    dollar_mode = 0;
                    str_append_ch(&ground, L'$');
                    difference++;
                } else { // $x$
                    difference += last_dollar_len + 1;
                    //printf("diff: %zu\n", difference);
                    string view = (string){
                        .c_str = inner.c_str + last_dollar_idx_begin,
                        .length = last_dollar_len,
                        .size = last_dollar_len
                    };
                    string name = str_cpy(view);
                    idx_buffer = (field_location*)malloc(sizeof(field_location));
                    //out.fields_count++;
                    size_t map_find_result = map_get_counted(variables, name.c_str, NULL);
                    if (map_find_result != MAP_NPOS) {
                        //puts("\n\nalready registered\n\n");
                        *idx_buffer = (field_location){ i - difference, map_find_result + 1 };
                    } else {
                        if (wcscmp(name.c_str, L"INNER")) {
                            len_buffer = (size_t*)malloc(sizeof(size_t));
                            *len_buffer = name.length;
                            var_name_alloc_size_tot += *len_buffer + 1;
                            //fputws(name.c_str, stdout);
                            map_set(variables, name.c_str, len_buffer, 0);
                            *idx_buffer = (field_location){ i - difference, variables->size };
                            //printf("|a: (pos: %zu, var: %zu)", idx_buffer->pos, idx_buffer->var);
                        } else {
                            *idx_buffer = (field_location){ i - difference, 0 };
                        }
                    }
                    list_add(fields, (void*)idx_buffer);
                    str_delete(&name);
                    //str_debug(view);
                    last_dollar_idx_begin = i + 1;
                    last_dollar_len = 0;
                }
            } else {
                dollar_mode = 0;
                if (last_dollar_len) {
                    difference += last_dollar_len + 1;
                    //printf("diff: %zu\n", difference);
                    str_append_ch(&ground, c);
                    string view = (string){
                        .c_str = inner.c_str + last_dollar_idx_begin,
                        .length = last_dollar_len,
                        .size = last_dollar_len
                    };
                    string name = str_cpy(view);
                    idx_buffer = (field_location*)malloc(sizeof(field_location));
                    //out.fields_count++;
                    size_t map_find_result = map_get_counted(variables, name.c_str, NULL);
                    if (map_find_result != MAP_NPOS) {
                        //puts("\n\nalready registered\n\n");
                        *idx_buffer = (field_location){ i - difference, map_find_result + 1 };
                    } else {
                        if (wcscmp(name.c_str, L"INNER")) {
                            len_buffer = (size_t*)malloc(sizeof(size_t));
                            *len_buffer = name.length;
                            var_name_alloc_size_tot += *len_buffer + 1;
                            //fputws(name.c_str, stdout);
                            map_set(variables, name.c_str, len_buffer, 0);
                            *idx_buffer = (field_location){ i - difference, variables->size };
                            //printf("|b: (pos: %zu, var: %zu)", idx_buffer->pos, idx_buffer->var);
                        } else {
                            *idx_buffer = (field_location){ i - difference, 0 };
                        }
                    }
                    list_add(fields, (void*)idx_buffer);
                    str_delete(&name);
                    //str_debug(view);
                    last_dollar_len = 0;
                } else {
                    difference++;
                    str_append_ch(&ground, L'$');
                }
            }
        } else if (c == L'$') {
            dollar_mode = 1;
            last_dollar_idx_begin = i + 1;
            last_dollar_len = 0;
        } else {
            str_append_ch(&ground, c);
        }
    }

    ht_template_core* out = (ht_template_core*)malloc(sizeof(ht_template_core));
    out->fields_count = fields->size;
    out->shifts_count = variables->size;
    out->field_names = (wchar_t*)malloc(var_name_alloc_size_tot * sizeof(wchar_t));
    out->field_shifts = (size_t*)malloc(variables->size * sizeof(size_t));
    out->fields = (field_location*)malloc(fields->size * sizeof(field_location));
    out->ground = ground;

    wcsncpy(out->field_names, L"INNER", 5);
    out->field_names[5] = L'\0';

    size_t shift = 6;
    for (size_t i = 0; i < variables->size; i++) {
        u_map_node* umn = mapn_at(variables->head, i);
        //printf("<--token: %ls->>>", umn->token);
        //printf("<--token: %zu->>>", *(size_t*)umn->data);
        wcsncpy(out->field_names + shift, umn->token, *(size_t*)umn->data);
        shift += *(size_t*)umn->data + 1;
        out->field_shifts[i] = shift;
        //printf("<--shift: %zu->>>", shift);
        free(umn->data);
        out->field_names[shift - 1] = L'\0';
    }
    //out->field_shifts[variables->size] = shift;
    out->field_names[var_name_alloc_size_tot - 1] = L'\0';

    list_node* ln = fields->head;
    for (size_t i = 0; i < fields->size; i++) {
        if (i > 0) {
            ln = ln->next;
        }
        out->fields[i] = *(field_location*)ln->data;
        free(ln->data);
        //printf("(pos: %zu, var: %zu)", out->fields[i].pos, out->fields[i].var);
    }

    map_delete(variables);
    list_delete(fields);

    /*printf("\n---------------%ls-----------\n\n", out.field_names);
    for (size_t i = 0; i < variables->size; i++) {
        printf("\n---------------%ls-----------\n\n", out.field_names + out.field_shifts[i]);
    }*/
    /*for (size_t i = 0; i < var_name_alloc_size_tot; i++) {
        printf("__%lc__|", out->field_names[i]);
    }
    printf("|fields_count: %zu\n", out->fields_count);
    printf("|shifts_count: %zu\n", out->shifts_count);*/
    //str_debug(ground);
    //printf("&out: %p\n", &out);
    return out;
}

// returns a string in a fixed size allocated memory given the `core`, the `values` array and the `inner` value
string template_fill(ht_template_core* core, const string* values, const string inner) {
    size_t difference = 0;
    for (size_t i = 0; i < core->fields_count; i++) {
        size_t var = core->fields[i].idx;
        if (var) {
            difference += values[core->fields[i].idx - 1].length;
        } else {
            difference += inner.length;
        }
    }
    size_t len = core->ground.length + difference;
    string out = str_new(len + 1);
    size_t list_shift = 0;
    for (size_t i = 0; i < core->ground.length; i++) {
        field_location f = list_shift < core->fields_count ? core->fields[list_shift] : (field_location) { 0, 0 };
        if (list_shift < core->fields_count && i == f.pos) {
            string v = f.idx ? values[f.idx - 1] : inner;
            for (size_t j = 0; j < v.length; j++) {
                str_append_ch(&out, v.c_str[j]);
            }
            list_shift++;
            i--;
        } else {
            str_append_ch(&out, core->ground.c_str[i]);
        }
    }
    //printf("templated -%ls-", out.c_str);
    //printf("expected length: %zu\nlength: %zu\n", len, out.length);
    return out;
}

// process the template and returns a string
string process_template(htmw_context ctx, const ht_template* t, const wchar_t** args, const string inner) {
    ht_template_core* core = t->core;
    lua_State* L = ctx.L;

    //char* template_name = (char*)malloc((t->name.length + 1) * sizeof(char));
    char* construct_name = (char*)malloc((t->name.length + 21 + 1) * sizeof(char));
    strncpy(construct_name, "__template_construct_", 21);
    wcstombs(construct_name + 21, t->name.c_str, t->name.length);
    construct_name[t->name.length + 21] = '\0';

    lua_getglobal(L, construct_name);
    //ht_template_arg* = t->args->data
    for (size_t i = 0; i < t->args->size; i++) {
        if (args[i][0] == L'\b') {
            lua_pushnil(L);
        } else if (args[i][0] == L'\r') {
            //char* lin = (char*)malloc((wlin.length + 1 + 16 + 1 - 1) * sizeof(char)); // len + null terminator + "return tostring(" + ")" - "\r"
            size_t lin_len = wcslen(args[i]);
            char* lin = (char*)malloc((lin_len + 9 + 1) * sizeof(char));//printf("-----------|%ls|---\n", args[i] + 1);
            lin[0] = 'r';
            lin[1] = 'e';
            lin[2] = 't';
            lin[3] = 'u';
            lin[4] = 'r';
            lin[5] = 'n';
            lin[6] = ' ';
            lin[7] = '(';
            wcstombs(lin + 8, args[i] + 1, lin_len - 1);
            lin[lin_len + 8 - 1] = ')';
            lin[lin_len + 9 - 1] = '\0';
            int r = luaL_loadstring(L, lin);
            if (check_lua(L, r)) {
                if (lua_pcall(L, 0, 1, 0) == LUA_OK) {
                    // ...
                } else {
                    throw_error("Lua pcall is not ok", E_EXIT);
                }
            } else {
                throw_error("Lua status is not ok", E_EXIT);
            }
            free(lin);
        } else {  // if arg value is not null
            ht_template_arg a = VECT_A(ht_template_arg, t->args)[i];

            char arg[4096];
            //printf(">>>> %p\n", args[i]);
            //printf("2) %p -> \"%ls\"\n", args[i], args[i]);
            wcstombs(arg, args[i], 4095);
            lua_pushstring(L, arg);
        }
    }

    if (check_lua(L, lua_pcall(L, t->args->size, 1, 0))) {
        //setlocale(LC_ALL, "");
        if (lua_istable(L, -1)) {
            string* lvs = (string*)malloc(core->shifts_count * sizeof(string));
            for (size_t i = 0; i < core->shifts_count; i++) {
                char k[4096];
                wcstombs(k, core->field_names + (i ? core->field_shifts[i - 1] : 6), 4095);
                k[core->field_shifts[i] - 1] = '\0';
                lua_pushstring(L, k);
                lua_gettable(L, -2);
                char* v = lua_tostring(L, -1);
                lua_pop(L, 1);

                lvs[i] = str_new(4096);
                char c;
                for (size_t j = 0; (c = v[j]) != '\0'; j++) {
                    wchar_t wc;
                    int r = mbtowc(&wc, v + j, MB_CUR_MAX);
                    //printf("i: %d => %lc\n" , r, wc);
                    str_append_ch(&lvs[i], wc);
                }

                //printf("lv >> %ls\n", lvs[i].c_str);
            }
            string filled = template_fill(core, lvs, inner);
            for (size_t i = 0; i < core->shifts_count; i++) {
                str_delete(&lvs[i]);
            }
            free(lvs);
            // TODO: re-process AND FREE MEMORY
            htmw_context inner_ctx = htmw_preprocess(ctx.L, filled, &ctx);
            txt_template_fill_data tfd = htmw_process_txt(inner_ctx);
            //str_debug(inner_ctx.input);
            //printf("___%ls\n", ((ht_template_core*)inner_ctx.templates->head->next->data)->ground.c_str);
            
            if (tfd.fields_count) {
                string out = htmw_compile_txt(inner_ctx, &tfd);
                return out;
            } else {
                return inner_ctx.input;
            }
            //string out = str_new();
            
            //return filled;
        } else {
            // TODO: handle exception
        }
    }
}

typedef enum {
    ps_null,
    ps_tag
} parse_status;

typedef struct {
    list* txts;
    size_t len;
} txt_list_tot_len;

// takes a string of args (`in`) and outputs an ordered & filtered list of arg `name` + `value` in a txt_template based on the template (`t`)
int txt_parse_args(ht_template* t, const string in, txt_list_tot_len* out, lua_State* L) {
    list* argsl = list_new();

    int mode = 0;
    out->len = 0;
    ht_template_arg_value* arg = NULL;
    wchar_t value_bounds = L'\0';
    size_t last_ent = 0;
    int eq = 0;
    char value_mode = 0;    // 0: default, 1: ``, 2: {} /// unused????
    unsigned char neutral_mode; // bool: escaping in strlt or commenting in lua
    size_t nest = 0;
    wchar_t lua_str_mode = L'\0';
    for (size_t i = 0; i <= in.length; i++) {
        //printf("i: %zu\n", i);
        wchar_t c = i < in.length ? in.c_str[i] : L'\0';
        if (c >= L'a' && c <= L'z' || c >= L'A' && c <= L'Z' || c >= L'0' && c <= L'9' || c == L'_') {
            switch (mode) {
            case 0: // enter name mode
                arg = (ht_template_arg_value*)malloc(sizeof(ht_template_arg_value));
                arg->name = nullstr;
                arg->value = nullstr;
                arg->flags = 0;
                arg->name.c_str = in.c_str + i;
                neutral_mode = 0;
                last_ent = i;
                mode = 1;
                break;
            case 1: // name

                break;
            case 2: // default arg value and new attribute name
                arg->value = nullstr;
                //arg->value = str_cpy(!str_isnull(arg.default_value) ? arg.default_value)
                //arg->value = str_new(2);
                //str_append_ch(&arg->value, L'\b');
                list_add(argsl, arg);
                arg = (ht_template_arg_value*)malloc(sizeof(ht_template_arg_value));
                arg->name = nullstr;
                arg->value = nullstr;
                arg->flags = 0;
                arg->name.c_str = in.c_str + i;
                neutral_mode = 0;
                last_ent = i;
                mode = 1;
                break;
            case 3: // enter value mode without quotes
                value_bounds = L'\0';
                arg->value.c_str = in.c_str + i;
                last_ent = i;
                mode = 4;
                break;
            case 4: // value
                if (flag_includes(arg->flags, T_ARGV_STRLT)) {
                    if (!neutral_mode) {
                        str_append_ch(&arg->value, c);
                    } else {
                        switch (c)
                        {
                        case L'n':
                            str_append_ch(&arg->value, L'\n');
                            break;
                        case L't':
                            str_append_ch(&arg->value, L'\t');
                            break;
                        case L'r':
                            str_append_ch(&arg->value, L'\r');
                            break;
                        case L'v':
                            str_append_ch(&arg->value, L'\v');
                            break;
                        case L'f':
                            str_append_ch(&arg->value, L'\f');
                            break;
                        case L'a':
                            str_append_ch(&arg->value, L'\a');
                            break;
                        default:
                            str_append_ch(&arg->value, c);
                            break;
                        }
                    }
                    neutral_mode = 0;
                }
                break;
            }
        } else switch (c) {
        case L'[':
            switch (mode) {
            case 0: // !
                // TODO: throw exception
                throw_error("Unexpected character `[` in widget tag, arg name expected", E_EXIT);
                break;
            case 1: // name!
                // TODO: throw exception
                throw_error("Unexpected character `[` in widget tag, in arg name", E_EXIT);
                break;
            case 2: // name !
                // TODO: throw exception
                throw_error("Unexpected character `[ in widget tag, arg name or `=` expected", E_EXIT);
                break;
            case 3: // name=!
                value_bounds = L'\0';
                arg->value.c_str = in.c_str + i;
                last_ent = i;
                mode = 4;
                break;
            case 4: // name="!"
                if (flag_includes(arg->flags, T_ARGV_VALUE) && !neutral_mode && !wcsncmp(in.c_str + i, L"[[", 2)) {
                    if (lua_str_mode == L'\0') {
                        i++;
                        lua_str_mode = L']';
                    }
                } else if (flag_includes(arg->flags, T_ARGV_STRLT)) {
                    str_append_ch(&arg->value, L']');
                }
                break;
            }
            break;
        case L']':
            switch (mode) {
            case 0: // !
                // TODO: throw exception
                throw_error("Unexpected character `]` in widget tag, arg name expected", E_EXIT);
                break;
            case 1: // name!
                // TODO: throw exception
                throw_error("Unexpected character `]` in widget tag, in arg name", E_EXIT);
                break;
            case 2: // name !
                // TODO: throw exception
                throw_error("Unexpected character `]` in widget tag, arg name or `=` expected", E_EXIT);
                break;
            case 3: // name=!
                value_bounds = L'\0';
                arg->value.c_str = in.c_str + i;
                last_ent = i;
                mode = 4;
                break;
            case 4: // name="!"
                if (neutral_mode == 3) {
                    neutral_mode = 0;
                    break;
                } else if (flag_includes(arg->flags, T_ARGV_VALUE) && !wcsncmp(in.c_str + i, L"]]", 2)) {
                    if (neutral_mode == 2) {
                        i++;
                        neutral_mode = 0;
                    } else if (lua_str_mode == L']') {
                        i++;
                        lua_str_mode = L'\0';
                    }
                } else if (flag_includes(arg->flags, T_ARGV_STRLT)) {
                    str_append_ch(&arg->value, L']');
                }
                break;
            }
            break;
        case L'-':
            switch (mode) {
            case 0: // !
                // TODO: throw exception
                throw_error("Unexpected character `-` in widget tag, arg name expected", E_EXIT);
                break;
            case 1: // name!
                // TODO: throw exception
                throw_error("Unexpected character `-` in widget tag, in arg name", E_EXIT);
                break;
            case 2: // name !
                // TODO: throw exception
                throw_error("Unexpected character `-` in widget tag, arg name or `=` expected", E_EXIT);
                break;
            case 3: // name=!
                value_bounds = L'\0';
                arg->value.c_str = in.c_str + i;
                last_ent = i;
                mode = 4;
                break;
            case 4: // name="!"
                if (lua_str_mode != L'\0') {
                    break;
                }
                if (flag_includes(arg->flags, T_ARGV_VALUE) && !neutral_mode && lua_str_mode == L'\0') {
                    if (!wcsncmp(in.c_str + i, L"--[[", 4)) {
                        neutral_mode = 2;
                        i += 3;
                    } else if (!wcsncmp(in.c_str + i, L"--", 2)) {
                        neutral_mode = 1;
                        i++;
                    }
                } else if (flag_includes(arg->flags, T_ARGV_STRLT)) {
                    str_append_ch(&arg->value, L'-');
                }
                break;
            }
            break;
        case L'\\':
            switch (mode) {
            case 0: // !
                // TODO: throw exception
                throw_error("Unexpected character `\\` in widget tag, arg name expected", E_EXIT);
                break;
            case 1: // name!
                // TODO: throw exception
                throw_error("Unexpected character `\\` in widget tag, in arg name", E_EXIT);
                break;
            case 2: // name !
                // TODO: throw exception
                throw_error("Unexpected character `\\` in widget tag, arg name or `=` expected", E_EXIT);
                break;
            case 3: // name=!
                value_bounds = L'\0';
                arg->value.c_str = in.c_str + i;
                last_ent = i;
                mode = 4;
                break;
            case 4: // name="!"
                if (flag_includes(arg->flags, T_ARGV_STRLT)) {
                    neutral_mode = !neutral_mode;
                    if (!neutral_mode) {
                        str_append_ch(&arg->value, L'\\');
                    }
                } else if (flag_includes(arg->flags, T_ARGV_VALUE) && lua_str_mode != L'\0') {
                    neutral_mode = (!neutral_mode) * 3;
                }
                break;
            }
            break;
        case L'}':
        switch (mode) {
            case 0: // "value"
                // TODO: throw exception
                throw_error("Constructor argument values should have a name", E_EXIT);
                break;
            case 1: // name"
                // TODO: throw exception
                throw_error("Illegal constructor argument value begin", E_EXIT);
                break;
            case 2: // name "value"
                // TODO: throw exception
                break;
            case 3: // enter value with quotes
                value_bounds = L'\0';
                arg->value.c_str = in.c_str + i;
                last_ent = i;
                mode = 4;
                break;
            case 4: // exit value with quotes?
                //printf("%d, %d, %d\n", neutral_mode, lua_str_mode, nest);
                if (c == value_bounds && !neutral_mode && lua_str_mode == L'\0') {
                    if (!nest) {
                        value_mode = 0;
                        arg->value.length = i - last_ent - 1;
                        list_add(argsl, arg);
                        mode = 0;
                    } else {
                        nest--;
                    }
                } else if (flag_includes(arg->flags, T_ARGV_STRLT)) {
                    str_append_ch(&arg->value, L'}');
                    neutral_mode = 0;
                }
                break;
            }
            break;
        case L'{':
            switch (mode) {
            case 0: // "value"
                // TODO: throw exception
                throw_error("Constructor argument values should have a name", E_EXIT);
                break;
            case 1: // name"
                // TODO: throw exception
                throw_error("Illegal constructor argument value begin", E_EXIT);
                break;
            case 2: // name "value"
                // TODO: throw exception
                break;
            case 3: // enter value with quotes
                value_bounds = L'}';
                value_mode = 2;
                arg->flags |= T_ARGV_VALUE;
                arg->value.c_str = in.c_str + i + 1;
                last_ent = i;
                mode = 4;
                break;
            case 4: // exit value with quotes?
                if (flag_includes(arg->flags, T_ARGV_VALUE) && !neutral_mode && lua_str_mode == L'\0') {
                    nest++;
                } else if (flag_includes(arg->flags, T_ARGV_STRLT)) {
                    str_append_ch(&arg->value, L'{');
                    neutral_mode = 0;
                }
                break;
            }
            break;
        case L'`':
            switch (mode) {
            case 0: // "value"
                // TODO: throw exception
                throw_error("Constructor argument values should have a name", E_EXIT);
                break;
            case 1: // name"
                // TODO: throw exception
                throw_error("Illegal constructor argument value begin", E_EXIT);
                break;
            case 2: // name "value"
                // TODO: throw exception
                break;
            case 3: // enter value with quotes
                value_bounds = L'`';
                value_mode = 1;
                arg->flags |= T_ARGV_STRLT;
                arg->value = str_new(in.length - i);
                last_ent = i;
                mode = 4;
                break;
            case 4: // exit value with quotes?
                if (c == value_bounds && !neutral_mode) {
                    value_mode = 0;
                    //arg->value.length = i - last_ent - 1;
                    list_add(argsl, arg);
                    mode = 0;
                } else if (flag_includes(arg->flags, T_ARGV_STRLT)) {
                    str_append_ch(&arg->value, L'`');
                    neutral_mode = 0;
                } else if (flag_includes(arg->flags, T_ARGV_VALUE) && neutral_mode == 3) {
                    neutral_mode = 0;
                }
                break;
            }
            break; /// ^^^ NEW ^^^
        case L'\'':
        case L'"':
            switch (mode) {
            case 0: // "value"
                // TODO: throw exception
                throw_error("Constructor argument values should have a name", E_EXIT);
                break;
            case 1: // name"
                // TODO: throw exception
                throw_error("Illegal constructor argument value begin", E_EXIT);
                break;
            case 2: // name "value"
                // TODO: throw exception
                break;
            case 3: // enter value with quotes
                value_bounds = c;
                arg->value.c_str = in.c_str + i + 1;
                last_ent = i;
                mode = 4;
                break;
            case 4: // exit value with quotes?
                if (c == value_bounds) {
                    arg->value.length = i - last_ent - 1;
                    list_add(argsl, arg);
                    mode = 0;
                } else if (flag_includes(arg->flags, T_ARGV_STRLT)) {
                    str_append_ch(&arg->value, c);
                } else if (flag_includes(arg->flags, T_ARGV_VALUE)) {
                    if (lua_str_mode == L'\0' && !neutral_mode) {
                        lua_str_mode = c;
                    } else if (lua_str_mode == c && neutral_mode != 3) {
                        lua_str_mode = L'\0';
                    } else if (neutral_mode == 3) {
                        neutral_mode = 0;
                    }
                }
                break;
            }
            break;
        case L' ':
        case L'\t':
        case L'\n':
        case L'\r':
            switch (mode) {
            case 0:
                break;
            case 1: // exit name mode
                arg->name.length = i - last_ent;
                mode = 2;
                break;
            case 2: // ignore

                break;
            case 3: // ignore
                
                break;
            case 4:
                if (value_bounds == L'\0') { // exit value without quotes
                    arg->value.length = i - last_ent;
                    list_add(argsl, arg);
                    mode = 0;
                } else if (flag_includes(arg->flags, T_ARGV_VALUE)) { // exit single line lua comment mode
                    if (neutral_mode == 1 && c == L'\n') {
                        neutral_mode = 0;
                    } else if ((lua_str_mode == L'"' || lua_str_mode == L'\'') && c == L'\n') {
                        lua_str_mode = L'\0';
                    } else if (neutral_mode == 3) {
                        neutral_mode = 0;
                    }
                } else if (flag_includes(arg->flags, T_ARGV_STRLT)/* && !neutral_mode*/) {
                    str_append_ch(&arg->value, c);
                    neutral_mode = 0;
                }
                break;
            }
            break;
        case L'=':
            switch (mode) {
            case 0: // =
                // TODO: throw exception
                throw_error("Unexpected symbol '=' in widget tag", E_EXIT);
                break;
            case 1: // name=
                arg->name.length = i - last_ent;
                mode = 3;
                break;
            case 2:
                mode = 3;
                break;
            case 3: // name==
                // TODO: throw exception
                throw_error("Unexpected symbol '=' in widget tag", E_EXIT);
                break;
            case 4: // name="=" or // name= =
                //if (c != L'\0') {
                if (value_bounds != L'\0') {
                    /*arg->value.length = i - last_ent;
                    list_add(argsl, arg);
                    mode = 0;*/
                    if (flag_includes(arg->flags, T_ARGV_VALUE) && neutral_mode == 3) {
                        neutral_mode = 0;
                    }
                    break;
                } else if (flag_includes(arg->flags, T_ARGV_STRLT)/* && !neutral_mode*/) {
                    str_append_ch(&arg->value, L'=');
                    neutral_mode = 0;
                } else {
                    // TODO: throw exception
                    throw_error("Unexpected symbol '=' in widget tag", E_EXIT);
                }
                break;
            }
            break;
        case L'\0':
            switch (mode) {
            case 0:
                break;
            case 1: // exit name mode
                arg->name.length = i - last_ent;
                mode = 2;
                arg->value = nullstr;
                //printf("i: %zu | le: %zu\n", i, last_ent);
                //str_debug(arg->value);
                list_add(argsl, arg);
                break;
            case 2: // default arg value
                arg->value = nullstr;
                list_add(argsl, arg);
                break;
            case 3: // ignore
                break;
            case 4: // exit value without quotes
                if (value_bounds == L'\0') {
                    arg->value.length = i - last_ent;
                    list_add(argsl, arg);
                    mode = 0;
                } else if (flag_includes(arg->flags, T_ARGV_STRLT)/* && !neutral_mode*/) {
                    str_append_ch(&arg->value, L'\0');
                    neutral_mode = 0;
                }
                break;
            }
            break;
        default:
            switch (mode) {
            case 0: // !
                // TODO: throw exception
                //printf("'%lc'\n", c);
                throw_error("Unexpected symbol in widget tag, arg name expected", E_EXIT);
                break;
            case 1: // name!
                // TODO: throw exception
                throw_error("Unexpected symbol in widget tag, in arg name", E_EXIT);
                break;
            case 2: // name !
                // TODO: throw exception
                throw_error("Unexpected symbol in widget tag, arg name or `=` expected", E_EXIT);
                break;
            case 3: // name=!
                value_bounds = L'\0';
                arg->value.c_str = in.c_str + i;
                last_ent = i;
                mode = 4;
                break;
            case 4: // name="!"
                if (flag_includes(arg->flags, T_ARGV_STRLT)/* && !neutral_mode*/) {
                    str_append_ch(&arg->value, c);
                    neutral_mode = 0;
                } else if (flag_includes(arg->flags, T_ARGV_VALUE) && neutral_mode == 3) {
                    neutral_mode = 0;
                }
                break;
            }
            break;
        }
    }

#if DEBUG_MODE
    puts("---[ARGSL]---");
    for (size_t i = 0; i < argsl->size; i++) {
        ht_template_arg_value* a = list_get(argsl, i);
        printf("- %ls\t\t%ls\n", str_cpy(a->name).c_str, str_cpy(a->value).c_str);
    }
    puts("---[ END ]---");
#endif

    list* args = list_new(); // sorted & filtered output list
    vect* a = t->args;
    VECT_FOR(a, i) {
        int found = 0;
        list_node* ln = argsl->head;
        for (size_t j = 0; j < argsl->size; j++) {
            ht_template_arg_value* o = (ht_template_arg_value*)ln->data;

            if (!wcsncmp(VECT_A(ht_template_arg, a)[i].name.c_str, o->name.c_str, o->name.length)) {
                found = 1;
                if (str_isnull(o->value)) {
                    o->value = VECT_A(ht_template_arg, a)[i].default_value;
                } else if (flag_includes(o->flags, T_ARGV_VALUE)) {
                    //o->value = str_new(2);
                    //str_append_ch(&o->value, L'\r');
                    //string wlin = str_cpy(o->value);
                    string wlin = str_new(o->value.length + 2);
                    str_append_ch(&wlin, L'\r');
                    str_append(&wlin, o->value.c_str, o->value.length);
                    o->value = wlin;
                    //printf("{%ls}\n", o->value.c_str + 1);
                    /*char* lin = (char*)malloc((wlin.length + 1 + 16 + 1 - 1) * sizeof(char)); // len + null terminator + "return tostring(" + ")" - "/r"
                    lin[0] = 'r';
                    lin[1] = 'e';
                    lin[2] = 't';
                    lin[3] = 'u';
                    lin[4] = 'r';
                    lin[5] = 'n';
                    lin[6] = ' ';
                    lin[7] = 't';
                    lin[8] = 'o';
                    lin[9] = 's';
                    lin[10] = 't';
                    lin[11] = 'r';
                    lin[12] = 'i';
                    lin[13] = 'n';
                    lin[14] = 'g';
                    lin[15] = '(';
                    wcstombs(lin + 16, wlin.c_str, wlin.length);
                    lin[wlin.length + 16] = ')';
                    lin[wlin.length + 17] = '\0';
                    int r = luaL_loadstring(L, lin);
                    if (check_lua(L, r)) {
                        if (lua_pcall(L, 0, 1, 0) == LUA_OK) {
                            const char *lout = lua_tostring(L, -1);
                            size_t lout_len = strlen(lout);
                            string wlout = str_new(lout_len + 1);
                            mbstowcs(wlout.c_str, lout, lout_len);
                            wlout.c_str[lout_len] = L'\0';
                            wlout.length = lout_len;
                            free(lin);
                            //free(o->data);
                            o->value = wlout;

                            lua_pop(L, 1); // pop the result off the stack
                        } else {
                            throw_error("Lua pcall is not ok", E_EXIT);
                        }
                    } else {
                        throw_error("Lua status is not ok", E_EXIT);
                    }*/
                    
                    //lua_pop(L, 1);
                }
                list_add(args, o);
                list_remove(argsl, j);
                out->len += o->value.length + 1;
                break;
            }

            ln = ln->next;
        }
        if (!found) {
            // TODO: throw exception
            // FIXME: duplicate names are not handled properly, fix this!!!

            //printf("i: %zu\n", i);
            ht_template_arg arg = VECT_A(ht_template_arg, a)[i];
            if (!str_isnull(arg.default_value)) {
                if (flag_includes(arg.flags, T_ARG_NULLABLE)) {
                    ht_template_arg_value* o = (ht_template_arg_value*)malloc(sizeof(ht_template_arg_value));
                    o->name = str_cpy(arg.name); // ??? str_cpy?
                    //o->value = nullstr;
                    string v = str_new(2);
                    str_append_ch(&v, L'\b');
                    o->value = v;
                    out->len += o->value.length + 1;
                    //puts(VECT_A(ht_template_arg, a)[i].name.c_str);
                    //puts("1a");
                    list_add(args, o);
                    continue;
                } else {
                    ht_template_arg_value* o = (ht_template_arg_value*)malloc(sizeof(ht_template_arg_value));
                    o->name = str_cpy(arg.name); // ??? str_cpy?
                    o->value = str_cpy(arg.default_value);
                    out->len += o->value.length + 1;
                    //puts(VECT_A(ht_template_arg, a)[i].name.c_str);
                    //puts("1b");
                    list_add(args, o);
                    continue;
                }
            } else if (flag_includes(arg.flags, T_ARG_NULLABLE)) {
                ht_template_arg_value* o = (ht_template_arg_value*)malloc(sizeof(ht_template_arg_value));
                o->name = str_cpy(arg.name); // ??? str_cpy?
                //o->value = nullstr;
                string v = str_new(2);
                str_append_ch(&v, L'\b');
                o->value = v;
                out->len += o->value.length + 1;
                //puts(VECT_A(ht_template_arg, a)[i].name.c_str);
                //puts("2");
                list_add(args, o);
                continue;
            }

            throw_error("Widget argument(s) missing", E_EXIT);
            ln = argsl->head;
            for (size_t j = 0; j < argsl->size; j++) {
                ht_template_arg_value* o = (ht_template_arg_value*)ln->data;
                //free(o); duplicate keys cause crash? double-free?
            }
            list_delete(argsl);
            ln = args->head;
            for (size_t j = 0; j < args->size; j++) {
                ht_template_arg_value* o = (ht_template_arg_value*)ln->data;
                free(o);
            }
            out = NULL;
            return 0;
        }
    }

    /*for (size_t i = 0; i < args->size; i++) {
        printf("list debug (%zu)\n", i);
        ht_template_arg_value* o = (ht_template_arg_value*)list_get(args, i);
        str_debug(o->name);
        str_debug(o->value);
        puts("-----");
    }*/

    list_node* ln = argsl->head;
    for (size_t i = 0; i < argsl->size; i++) {
        ht_template_arg_value* o = (ht_template_arg_value*)ln->data;
        free(o);
    }
    out->txts = args;
    //*out = args;
    return 1;
}

txt_template_fill_data htmw_process_txt(htmw_context ctx) {
    string in = ctx.input;
    string ground = str_new(ctx.input.length + 1);

    list* fields_list = list_new(); // list >> field_location
    list* argsls = list_new();      // list >> list >> string
    list* inners_list = list_new(); // list >> string

    txt_template_fill_data out = (txt_template_fill_data){
        .args = NULL,
        .arg_shifts = NULL,
        .fields = NULL,
        .fields_count = 0,
        .ground = nullstr,
        .inner_shifts = NULL,
    };

    int comment_mode = 0;
    int php_mode = 0;
    size_t last_idx = 0;

    wchar_t c;

    size_t args_count = 0;
    size_t args_block_size = 0;
    size_t inners_block_size = 0;
    size_t difference = 0;

    //string name = str_new(128);
    for (size_t idx = 0; (c = in.c_str[idx]) != L'\0'; idx++) {
        //putwc(c, stdout);
        /*if (in.c_str[idx + 1] == L'\0') {
            //last_idx = idx;
            //printf("gwa gwa 1\n");
            //str_append(&ground, in.c_str + last_idx, idx - last_idx - 1);
            //printf("gwa gwa 2\n");
            //str_append_ch(&ground, c);
        } */if (comment_mode) {
            if (c == L'-') {
                if (!wcsncmp(in.c_str + idx, L"-->", 3)) {
                    comment_mode = 0;
                    idx += 2;
                } else continue;
            } else continue;
        }
        if (php_mode) {
            if (c == L'?') {
                if (!wcsncmp(in.c_str + idx, L"?>", 2)) {
                    php_mode = 0;
                    idx += 1;
                } else continue;
            } else continue;
        }

        if (c == L'<') {
            //in.c_str + idx + 1;

            // force native tags
            //printf("ctx.flags: %d\n", ctx.flags);
            //printf("idx: %zu\n", idx);
            /*if (in.c_str[idx + 1] == L'#') {
                if (flag_includes(ctx.flags, CTX_HEAD)) {
                    str_append_ch(&ground, L'<');
                    idx++;
                    continue;
                } else puts("n");
            } else if (in.c_str[idx + 1] == L'/' && in.c_str[idx + 2] == L'#') {
                if (flag_includes(ctx.flags, CTX_HEAD)) {
                    str_append_ch(&ground, L'<');
                    str_append_ch(&ground, L'/');
                    idx += 2;
                    continue;
                } else puts("n");
            }*/
            wchar_t c;
            size_t i;

            //int naming = 1;
            int ninner = 0;
            int dninner = 0;
            int aninner = 0;
            ht_template* t = NULL;
            string name_buffer = nullstr;
            string args_buffer = nullstr;
            string* inner = (string*)malloc(sizeof(string));
            *inner = nullstr;
            int in_args = 0;
            for (i = 0; (c = in.c_str[idx + 1 + i]) != L'\0'; i++) {
                //printf("__[%zu] => %lc\n", idx + i, c);
                if (c >= L'a' && c <= L'z' || c >= L'A' && c <= L'Z' || c >= L'0' && c <= L'9' || c == L'_'/* || c == L'-'*/) {
#if DEBUG_MODE
                    /*if (!wcsncmp(in.c_str + idx + 1, L"metas", 5)) {
                        printf("i: %zu\n", i);
                        puts("metafound!");
                    }*/
#endif
                    continue;
                } else if (c == L' ' || c == L'\n' || c == L'\t' || c == L'\r') {
#if DEBUG_MODE
                    /*putc('.', stdout);
                        printf("i: %zu\n", i);
                    if (!wcsncmp(in.c_str + idx + 1, L"metas", 5)) {
                        puts("post-metafound!");
                    }*/
#endif
                    if (!i) {
                        break;
                    } else {
                        name_buffer = (string){
                            .c_str = in.c_str + idx + 1,
                            .length = i,
                            .size = 0
                        };
                        u_map_node* mnt = (u_map_node*)ctx.templates->head;
                        for (size_t j = 0; j < ctx.templates->size; j++) {
                            ht_template* tt = (ht_template*)mnt->data;
#if DEBUG_MODE
                            //printf("\"%ls\"\n", name_buffer.c_str);
                            //printf("\"%ls\"\n\n", in.c_str + idx + 1);
                            //printf("nl: %zu\tns: %zu\n", name_buffer.length, name_buffer.size);
                            /*//if (!wcsncmp(name_buffer.c_str, L"metas", 5)) {
                                string _tmp_name = str_cpy(name_buffer);
                                printf("i: %zu\tlen: %zu\tnb: \"%ls\"\t\ttb: \"%ls\"\n", i, tt->name.length, _tmp_name.c_str, tt->name.c_str);
                            //}*/
#endif
                            if (i == tt->name.length && !wcsncmp(name_buffer.c_str, tt->name.c_str, i)) {
                                t = tt;
                                in_args = 1;
                                field_location* fl = (field_location*)malloc(sizeof(field_location));
                                fl->pos = idx - difference;
                                fl->idx = j;
                                list_add(fields_list, (void*)fl);
#if DEBUG_MODE
                                //printf("t1: %p\tpos: %zu\tname: %ls\n", t, fl->pos, t->name.c_str);
                                //printf("t1r_idx: %zu\n", fl->pos);
#endif
                                // check if ninner mode
                                //printf("ninner? %d\n", t->ninner);
                                if (t != NULL && t->ninner) {
                                    ninner = 1;
                                    aninner = 1;
                                    *inner = nullstr;
                                }
                                ///////////////////////
                                goto widget_name_to_arg_parse;
                            } else {
                                mnt = mnt->next;
                            }
                        }
                    }
                } else if (c == L'/') {
                    //putc(':', stdout);
                    if (in.c_str[idx + 2 + i] != L'>') break;
                    ninner = 1;
                    dninner = 1;
                    if (!i) {
                        break;
                    } else {
                        name_buffer = (string){
                            .c_str = in.c_str + idx + 1,
                            .length = i,
                            .size = 0
                        };
                        u_map_node* mnt = (u_map_node*)ctx.templates->head;
                        size_t j;
                        for (j = 0; j < ctx.templates->size; j++) {
                            ht_template* tt = (ht_template*)mnt->data;
                            if (i/* - 1*/ == tt->name.length && !wcsncmp(name_buffer.c_str, tt->name.c_str, i/* - 1*/)) {
                                t = tt;
                                field_location* fl = (field_location*)malloc(sizeof(field_location));
                                fl->pos = idx - difference;
                                fl->idx = j;
                                list_add(fields_list, (void*)fl);
#if DEBUG_MODE
                                //printf("t2: %p\tpos: %zu\tname: %ls\n", t, fl->pos, t->name.c_str);
#endif
                                goto widget_name_to_arg_parse;
                                //break;
                            } else {
                                mnt = mnt->next;
                            }
                        }
                        break;
                    }
                } else if (c == L'>') {
                    //putc(';', stdout);
                    name_buffer = (string){
                        .c_str = in.c_str + idx + 1,
                        .length = i,
                        .size = 0
                    };
                    u_map_node* mnt = (u_map_node*)ctx.templates->head;
                    size_t j;
#if DEBUG_MODE
                    if (!wcsncmp(name_buffer.c_str, L"test", 4)) {
                        puts("__test__");
                    }
#endif
                    for (j = 0; j < ctx.templates->size; j++) {
                        ht_template* tt = (ht_template*)mnt->data;
                        if (i/* - 1*/ == tt->name.length && !wcsncmp(name_buffer.c_str, tt->name.c_str, i/* - 1*/)) {
                            t = tt;
                            if (t != NULL) {
                                field_location* fl = (field_location*)malloc(sizeof(field_location));
                                fl->pos = idx - difference;
                                fl->idx = j;
                                list_add(fields_list, (void*)fl);
#if DEBUG_MODE
                                //printf("t3: %p\tpos: %zu\tname: %ls\n", t, fl->pos, t->name.c_str);
#endif
                            }
                            break;
                        } else {
                            mnt = mnt->next;
                        }
                    }
                    // check if ninner mode
                    if (t != NULL && t->ninner) {
                        ninner = 1;
                        aninner = 1;
                        *inner = nullstr;
                        goto widget_name_to_arg_parse;
                    }
                    ///////////////////////
                    inner->c_str = in.c_str + idx + 3 + i;
                    ;break;
                } else {
                    //putc('@', stdout);
                    //printf("c(%ld, '%lc')", (long int)c, c);
                    break;
                }
            }
        widget_name_to_arg_parse:;
            if (/*!in_args && */t == NULL) {
                // in case tag is not a widget
                str_append_ch(&ground, L'<');
                continue;
            }
            args_buffer.c_str = in.c_str + idx + 1 + i;
            size_t b = i;
            wchar_t str_bounds = L'\0';
            size_t nest = 0;
            wchar_t lua_str_mode = L'\0';
            unsigned char neutral_mode = 0;
            for (; (c = in.c_str[idx + 1 + i]) != L'\0'; i++) {
                switch (c) {
                case L'[':
                    if (str_bounds == L'}' && !wcsncmp(in.c_str + idx + 1 + i, L"[[", 2)) {
                        if (lua_str_mode == L'\0') {
                            lua_str_mode = L']';
                            i++;
                        }
                    } else if (neutral_mode == 3) {
                        neutral_mode = 0;
                    }
                    break;
                case L']':
                    if (str_bounds == L'}' && !wcsncmp(in.c_str + idx + 1 + i, L"]]", 2)) {
                        if (neutral_mode == 2) {
                            neutral_mode = 0;
                            i++;
                        } else if (lua_str_mode == L']' && !neutral_mode) {
                            lua_str_mode = L'\0';
                            i++;
                        }
                    } else if (neutral_mode == 3) {
                        neutral_mode = 0;
                    }
                    break;
                case L'-':
                    if (str_bounds == L'`' && neutral_mode) {
                        neutral_mode = 0;
                    } else if (str_bounds == L'}' && neutral_mode == 0) {
                        if (!wcsncmp(in.c_str + idx + 1 + i, L"--[[", 4)) {
                            neutral_mode = 2;
                            i += 3;
                        } else if (!wcsncmp(in.c_str + idx + 1 + i, L"--", 2)) {
                            neutral_mode = 1;
                            i++;
                        }
                    } else if (neutral_mode == 3) {
                        neutral_mode = 0;
                    }
                    break;
                case L'\\':
                    if (str_bounds == L'`') {
                        neutral_mode = !neutral_mode;
                    } else if (lua_str_mode != L'\0' && neutral_mode == 0 || neutral_mode == 3) {
                        neutral_mode = (!neutral_mode) * 3;
                    }
                    break;
                case L'}':
                    if (str_bounds == L'`' && neutral_mode) {
                        neutral_mode = 0;
                        break;
                    }
                    if (str_bounds != L'}') {
                        break;
                    }
                    if (!neutral_mode && lua_str_mode == L'\0') {
                        if (nest > 0) {
                            nest--;
                        } else {
                            str_bounds = L'\0';
                        }
                    } else if (neutral_mode == 3) {
                        neutral_mode = 0;
                    }
                    break;
                case L'{':
                    if (str_bounds == L'`' && neutral_mode) {
                        neutral_mode = 0;
                        break;
                    }
                    if (str_bounds == L'\0') {
                        str_bounds = L'}';
                        break;
                    }
                    if (str_bounds != L'}') {
                        break;
                    }
                    if (!neutral_mode && lua_str_mode == L'\0') {
                        nest++;
                    } else if (neutral_mode == 3) {
                        neutral_mode = 0;
                    }
                    break;
                case L'`': // NEW
                case L'\'':
                case L'"':
                    if (str_bounds == L'`' && neutral_mode) {
                        neutral_mode = 0;
                        break;
                    } else if (str_bounds == L'}' && !neutral_mode && c != L'`') {
                        //lua_str_mode = lua_str_mode == c ? L'\0' : c;
                        if (lua_str_mode == c) {
                            lua_str_mode = L'\0';
                        } else if (lua_str_mode == L'\0') {
                            lua_str_mode = c;
                        }
                        break;
                    } else if (neutral_mode == 3) {
                        neutral_mode = 0;
                    }
                    if (c == str_bounds) {
                        str_bounds = L'\0';
                    } else if (str_bounds == L'\0') {
                        str_bounds = c;
                    }
                    break;
                case L'/':
                    if (str_bounds == L'`' && neutral_mode) {
                        neutral_mode = 0;
                        break;
                    }
                    if (str_bounds == L'\0') {
                        dninner = 1;
                        goto widget_arg_parse_to_arg_process;
                    }
                    break;
                case L'>':
                    if (str_bounds == L'`' && neutral_mode) {
                        neutral_mode = 0;
                        break;
                    }
                    if (str_bounds == L'\0') {
                        if (!ninner) {
                            inner->c_str = in.c_str + idx + 2 + i;
                        }
                        goto widget_arg_parse_to_arg_process;
                    } else if (neutral_mode == 3) {
                        neutral_mode = 0;
                    }
                    break;
                case '\n':
                    if (str_bounds == L'`' && neutral_mode == 1) {
                        neutral_mode = 0;
                    } else if (str_bounds == L'}' && neutral_mode == 1) {
                        neutral_mode = 0;
                    } else if (neutral_mode == 3) {
                        neutral_mode = 0;
                    } else if (lua_str_mode == L'"' || lua_str_mode == L'\'') {
                        lua_str_mode = L'\0';
                    }
                    break;
                default:
                    if (neutral_mode == 3) {
                        neutral_mode = 0;
                    }
                    break;
                }
            }
            widget_arg_parse_to_arg_process:
            args_buffer.length = i - b;
            //printf(">>>|%ls\n", )
            //str_debug(args_buffer);
            txt_list_tot_len argsln;
            list* argsl = NULL;
            if (txt_parse_args(t, args_buffer, &argsln, ctx.L)) {
                argsl = argsln.txts;
                list_add(argsls, (void*)argsl);
            } else {
                // exception already handled (TEMPORARY)
            }
            args_count += argsl->size;
            args_block_size += argsln.len;
            //printf("str_isnull: %d\n", str_isnull(*inner));
            //str_debug(*inner);
            if (str_isnull(*inner)) {
                // if widget doesn't have a body or is a ninner e.g. <widget/>
                inners_block_size++;
                list_add(inners_list, inner);
                /*size_t d = 0;
                wchar_t c;
                //wchar_t 
                for (size_t i = 0; i < in.length; i++) {

                }*/
                /////printf("%zu + 3 - %d + %d\n", i, aninner, dninner * aninner);
                difference += i + 3 - aninner + (dninner * aninner);
                idx += i + 2 - aninner + (dninner * aninner);
                continue;
            }
            b = i;
            size_t nt_count = 0;
            for (; (c = in.c_str[idx + 1 + i]) != L'\0'; i++) {
                if (c == L'<') {
                    if (in.c_str[idx + i + 2] == L'/') {
                        wchar_t after = in.c_str[idx + 3 + i + t->name.length];
                        if (!wcsncmp(in.c_str + idx + 3 + i, t->name.c_str, t->name.length)) {
                            if (after == L'>') {
                                if (nt_count)
                                    nt_count--;
                                else {
                                    inner->length = i - b - 1;
                                    idx += 3 + i + 0 + t->name.length;
                                    difference += 3 + i + 1 + t->name.length;
                                    break;
                                }
                            } else if (after == L' ' || after == L'\n' || after == L'\t' || after == L'\r') {
                                wchar_t c;
                                for (size_t j = 0; (c = in.c_str[idx + 3 + i + t->name.length + j]) != L'\0'; j++) {
                                    if (c == L'>') {
                                        if (nt_count)
                                            nt_count--;
                                        else {
                                            inner->length = i - b - 1;
                                            idx += i + j + 3 + t->name.length + 0;
                                            difference += i + j + 3 + t->name.length + 1;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    } else if (!wcsncmp(in.c_str + idx + 2 + i, t->name.c_str, t->name.length)) {
                        wchar_t after = in.c_str[idx + 2 + i + t->name.length];
                        if (after == L'/') {
                            //nt_count++;
                        } else if (after == L' ' || after == L'\n' || after == L'\t' || after == L'>' || after == L'\r') {
                            //int is_ninner = 0;
                            int is_ninner = t->ninner;
                            for (size_t j = 0; (c = in.c_str[idx + 1 + i + j]) != L'>'; j++) {
                                if (c == L'/') {
                                    is_ninner = 1;
                                    break;
                                }
                            }
                            if (!is_ninner) {
                                nt_count++;
                            }
                        }
                    }
                }
            }
            //idx += inner.length + args_buffer.length;
            //printf("___%%zu->> '%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc\")<<<'\n", in.c_str[idx], in.c_str[idx + 1], in.c_str[idx + 2], in.c_str[idx + 3], in.c_str[idx + 4], in.c_str[idx + 5], in.c_str[idx + 6], in.c_str[idx + 7], in.c_str[idx + 8], in.c_str[idx + 9], in.c_str[idx + 10], in.c_str[idx + 11], in.c_str[idx + 12], in.c_str[idx + 13], in.c_str[idx + 14], in.c_str[idx + 15], in.c_str[idx + 16], in.c_str[idx + 17], in.c_str[idx + 18], in.c_str[idx + 19]);
            //printf("G___%%zu->> '%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc%lc\")<<<'\n", in.c_str[idx - 19], in.c_str[idx - 18], in.c_str[idx - 17], in.c_str[idx - 16], in.c_str[idx - 15], in.c_str[idx - 14], in.c_str[idx - 13], in.c_str[idx - 12], in.c_str[idx - 11], in.c_str[idx - 10], in.c_str[idx - 9], in.c_str[idx - 8], in.c_str[idx - 7], in.c_str[idx - 6], in.c_str[idx - 5], in.c_str[idx - 4], in.c_str[idx - 3], in.c_str[idx - 2], in.c_str[idx - 1], in.c_str[idx]);
            //printf("G___%%zu----('%lc')----\n", in.c_str[idx]);

#if DEBUG_MODE
            //str_debug(*inner);
#endif
            inners_block_size += inner->length + 1;
            list_add(inners_list, inner);
        } else {
            str_append_ch(&ground, c);
        }
    }

    //for (size_t i = 0; i < )

    // pack all the info
    out.ground = ground;
    out.args = (wchar_t*)malloc(args_block_size * sizeof(wchar_t));
    out.inners = (wchar_t*)malloc(inners_block_size * sizeof(wchar_t));
    out.arg_shifts = (size_t*)malloc(args_count * sizeof(size_t));
    out.inner_shifts = (size_t*)malloc(inners_list->size * sizeof(size_t));
    out.fields = (field_location*)malloc(fields_list->size * sizeof(field_location));
    out.fields_count = fields_list->size;
    out.args_count = 0;
    //out.

    //list_node* ln = fields_list->head;
    size_t s_idx = 0;
    size_t s_shifs_idx = 0;
    list_node* ln = argsls->head;
    { // pack args
        while (ln != NULL) {
            list* l = (list*)ln->data;
            out.args_count += l->size;
            {
                list_node* ln = l->head;
                while (ln != NULL) {
                    ht_template_arg_value* o = (ht_template_arg_value*)ln->data;
                    string s = o->value;
                    for (size_t i = 0; i < s.length; i++) {
                        out.args[s_idx++] = s.c_str[i];
                    }
                    out.args[s_idx++] = L'\0';
                    out.arg_shifts[s_shifs_idx++] = s_idx;
                    ln = ln->next;
                    if (flag_includes(o->flags, T_ARGV_STRLT) || flag_includes(o->flags, T_ARGV_VALUE)) {
                        str_delete(&o->value); // or `s` they share the same memory
                    }
                    free(o);
                    //printf("1) %p -> \"%ls\"\n", out.args + s_idx - 1, out.args[s_idx - 1]);
                    //str_delete(s); do NOT delete
                }
            }
            list_delete(l);
            ln = ln->next;
        }
    }
    list_delete(argsls);
    s_idx = 0;
    s_shifs_idx = 0;
    ln = inners_list->head;
    {
        while (ln != NULL) {
            string* s = (string*)ln->data;
            for (size_t i = 0; i < s->length; i++) {
                out.inners[s_idx++] = s->c_str[i];
            }
            out.inners[s_idx++] = L'\0';
            out.inner_shifts[s_shifs_idx++] = s_idx;
            ln = ln->next;
            free(s);
            //str_delete(s);
        }
    }
    list_delete(inners_list);
    s_idx = 0;
    ln = fields_list->head;
    {
        while (ln != NULL) {
            field_location* fl = (field_location*)ln->data;
            out.fields[s_idx++] = *fl;
            ln = ln->next;
            free(fl);
        }
    }
    list_delete(fields_list);
    // TODO: implement list deletion directly in the loops to avoid unnecessary iterations
    /*for (size_t i = 0; i < fields_list->size; i++) {
        
        ln = ln->next;
    }*/

    return out;
}

string htmw_compile_txt(htmw_context ctx, txt_template_fill_data* tf) {
    size_t difference = 0;
    string* fills = (string*)malloc(tf->fields_count * sizeof(string));
    ht_template** templates = (ht_template**)malloc(ctx.templates->size * sizeof(ht_template*));
    u_map_node* mnt = ctx.templates->head;
    for (size_t i = 0; i < ctx.templates->size; i++) {
        templates[i] = mnt->data;
        mnt = mnt->next;
    }
    size_t idx = 0;
    for (size_t i = 0; i < tf->fields_count; i++) {
        field_location fl = tf->fields[i];
        ht_template* t = templates[fl.idx];
        size_t template_args_count = t->args->size;
        //string* args = (string*)malloc(template_args_count * sizeof(string)); // using string struct
        wchar_t** args = (wchar_t**)malloc(template_args_count * sizeof(wchar_t*)); // using string struct
        for (size_t j = 0; j < template_args_count; j++) {
            //printf("->%ls\n", tf->args + (idx ? tf->arg_shifts[idx - 1] : 0));
            size_t arg_shift = idx ? tf->arg_shifts[idx - 1] : 0;
            /*args[j] = (string){  // using string struct
                .c_str = tf->args + arg_shift,
                .length = tf->arg_shifts[idx] - arg_shift - 1,
                .size = 0
            };*/
            args[j] = tf->args + arg_shift;
            //printf("len: %zu\n", args[j].length);
            //printf(">>>>>\n%ls\n", args[j]);
            idx++;
        }
        size_t inner_shift = i ? tf->inner_shifts[i - 1] : 0;
        string inner = (string){
            .c_str = tf->inners + inner_shift,
            .length = tf->inner_shifts[i] - inner_shift - 1,
            0
        };
        //str_debug(inner);
        fills[i] = process_template(ctx, t, args, inner);
        difference += fills[i].length;
    }
    size_t len = tf->ground.length + difference;
    string out = str_new(len + 1);
    size_t list_shift = 0;
    for (size_t i = 0; i < tf->ground.length + 1; i++) {
        field_location f = tf->fields[list_shift];
        if (list_shift < tf->fields_count && i == f.pos) {
            //printf(">%zu => %zu {%ls}\n", list_shift, fills[list_shift].length, fills[list_shift].c_str);
            //str_debug(fills[list_shift]);
            for (size_t j = 0; j < fills[list_shift].length; j++) {
                wchar_t c = fills[list_shift].c_str[j];
                if (c != L'\0')
                    str_append_ch(&out, c);
            }
            list_shift++;
            i--;
        } else {
            wchar_t c = tf->ground.c_str[i];
            if (c != L'\0')
                str_append_ch(&out, c);
        }
    }
    //str_debug(out);
    return out;
}

string htmw_postprocess(string s) {
    string out = str_new(s.length + 1);
    for (size_t i = 0; i < s.length; i++) {
        if (s.c_str[i + 1] == L'#') {
            str_append_ch(&out, L'<');
            i++;
        } else if (s.c_str[i + 1] == L'/' && s.c_str[i + 2] == L'#') {
            str_append_ch(&out, L'<');
            str_append_ch(&out, L'/');
            i += 2;
        } else {
            str_append_ch(&out, s.c_str[i]);
        }
    }
    return out;
}

typedef struct {
    char* origin_name;
    char* output_name;      // NULL if not recursive
    char* format;           // NULL if auto
    int replica_mode;       // boolean: if true a deep copy of all the files in the folder will be performed
} config_t;

int main(int argc, char** argv) {
    //setlocale(LC_ALL, "");
    setlocale(LC_ALL, "en_US.UTF-8");
    //printf("%f\n", 5.0f);
#if DEBUG_MODE
#ifdef _WIN32
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CRTDBG_CHECK_ALWAYS_DF);
#endif
#endif
    config_t config = (config_t){
        .origin_name = NULL,
        .output_name = NULL,
        .format = NULL,
        .replica_mode = 0
    };

    int show_ver = 0;

    for (int i = 1; i < argc; i++) {
        //printf("%d\t\"%s\"\n", i, argv[i]);
        //puts("-------");
        if (argv[i][0] == '-') {
            switch (argv[i][1])
            {
            case '-':
                if (!strcmp(argv[i] + 2, "format")) {
                    if (i + 1 >= argc) {
                        throw_error("Format config error", E_EXIT);
                    }
                    if (!strcmp(argv[i + 1], "auto")) {
                        config.format = NULL;
                        i++;
                    } else {
                        config.format = argv[++i];
                    }
                } else if (!strcmp(argv[i] + 2, "recursive")) {throw_error("Recursive/Replica mode is not supported yet", E_EXIT);
                    if (config.output_name) {
                        throw_error("Output mode already configured", E_EXIT);
                    }
                    if (i + 1 >= argc) {
                        throw_error("Output mode config error", E_EXIT);
                    }
                    config.output_name = argv[++i];
                } else if (!strcmp(argv[i] + 2, "replica")) {throw_error("Recursive/Replica mode is not supported yet", E_EXIT);
                    config.replica_mode = 1;
                    if (config.output_name) {
                        throw_error("Output mode already configured", E_EXIT);
                    }
                    if (i + 1 >= argc) {
                        throw_error("Output mode config error", E_EXIT);
                    }
                    config.output_name = argv[++i];
                }
                break;
            case 'f':
                if (i + 1 >= argc) {
                    throw_error("Format config error", E_EXIT);
                }
                if (!strcmp(argv[i + 1], "auto")) {
                    config.format = NULL;
                    i++;
                } else {
                    config.format = argv[++i];
                }
                break;
            case 'R':
                config.replica_mode = 1;
            case 'r':throw_error("Recursive/Replica mode is not supported yet", E_EXIT);
                if (config.output_name) {
                    throw_error("Output mode already configured", E_EXIT);
                }
                if (i + 1 >= argc) {
                    throw_error("Output mode config error", E_EXIT);
                }
                config.output_name = argv[++i];
                break;
            case 'v':
                printf("HTMW vT.5\nWoxell\n\n");
                show_ver = 1;
                break;
            default:
                break;
            }
        } else {
            if (config.origin_name == NULL) {
                config.origin_name = argv[i];
            } else {
                throw_error("Input file(s) already specified", E_EXIT);
            }
        }
    }

    if (config.origin_name == NULL) {
        if (show_ver) return 0;
        throw_error("No input file(s)", E_EXIT);
    }

    char* in_name = (char*)malloc((strlen(config.origin_name) + 6) * sizeof(char));
    sprintf(in_name, "%s.%s", config.origin_name, "htmw");

    char* out_name = (char*)malloc((strlen(config.origin_name) + (config.format ? strlen(config.format) : 4) + 2) * sizeof(char));
    sprintf(out_name, "%s.%s", config.origin_name, (config.format ? config.format : "html"));

    string in = read_file(in_name, 0);
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    htmw_context ctx = htmw_preprocess(L, in, NULL);
    stack* ctx_stack = stack_new();

    stack_push(ctx_stack, (void*)&ctx);

    txt_template_fill_data tfd = htmw_process_txt(ctx);
    //str_debug(tfd.ground);
    //printf("_%zu\n", tfd.fields_count);

    string preout = htmw_compile_txt(ctx, &tfd);

    string out = htmw_postprocess(preout);
    //str_delete(preout);

    //printf("%ls\n", o.c_str);
    lua_close(L);
    FILE* out_stream = fopen(out_name, "w");
    fprintf(out_stream, "%ls", out.c_str);
    fclose(out_stream);
    free(in_name);
    free(out_name);
#if DEBUG_MODE
    //printf("%ls\n", ctx.input.c_str);
#endif
}