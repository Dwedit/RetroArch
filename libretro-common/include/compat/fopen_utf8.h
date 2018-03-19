#ifndef __FOPEN_UTF8_H
#define __FOPEN_UTF8_H

#include <stdio.h>

#ifdef _WIN32
/* defined to error rather than fopen_utf8, to make it clear to everyone reading the code that not worrying about utf16 is fine */
/* TODO: enable */
/* #define fopen (use fopen_utf8 instead) */

#if __cplusplus
extern "C"
{
#endif
FILE* fopen_utf8(const char * filename, const char * mode);
bool unlink_utf8(const char * filename);
bool mkdir_utf8(const char * filename);

#if __cplusplus
}
#endif




#else
#define fopen_utf8 fopen
#define unlink_utf8 unlink
#define mkdir_utf8 mkdir

#endif
#endif
