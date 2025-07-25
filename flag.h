/*
 * flag.h by Woxell
 * This header includes types and functions to check binary flags
 */

#ifndef FLAG_H
#define FLAG_H
#ifdef __cplusplus
extern "C" {
#endif

#define flag_includes(f, required) ((required) == ((f) & (required)))
#define flag_excludes(f, excluded) ((excluded) != ((f) & (excluded)))
#define flag_check(f, required, excluded) ((required) == ((f) & (required)) && (excluded) != ((f) & (excluded)))

typedef unsigned flag_t;

#ifdef __cplusplus
}
#endif
#endif
