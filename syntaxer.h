#ifndef SYNTAXER_H
#define SYNTAXER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "flag.h"

#define SYNTXF_EXE_BREAK                (1 << 0)
#define SYNTXF_EXE_ESC_CHAR             (1 << 1)
#define SYNTXF_EXE_ESC_RAW              (1 << 2)
//#define SYNTXF_EXE_ESC_BREAK            (1 << 3)

#define SYNTXF_DEF_N_ML                 (1 << 0)
#define SYNTXF_DEF_N_NO_ESC_RAW         (1 << 1)
#define SYNTXF_DEF_N_ESC_RAW            (1 << 2)

#define SYNTXF_DEF_NB_ESC_IMMUNE        (1 << 0)

typedef struct syntaxer_neutralizer_break_s {
    //struct syntaxer_neutralizer_s neutralizer;
    wchar_t* esc_seq;
    flag_t flags;
} syntaxer_neutralizer_break;


typedef struct syntaxer_neutralizer_s {
    wchar_t* begin;
    wchar_t* end;
    wchar_t* esc_char;
    wchar_t* esc_raw;                // even if multiline is false, if there's the escape sequence before the newline, it will continue
    flag_t flags;                       // multiline | put `\n` if new line without escape raw | put `\n` if escape raw
} syntaxer_neutralizer;

size_t syntaxer_select_until(syntaxer_neutralizer* neutralizers, size_t neutralizers_count, const wchar_t* s, wchar_t* begin_sequence, wchar_t* end_sequence, size_t max_len);
void syntaxer_sort(syntaxer_neutralizer* neutralizers, size_t neutralizers_count);

#ifdef __cplusplus
}
#endif
#endif