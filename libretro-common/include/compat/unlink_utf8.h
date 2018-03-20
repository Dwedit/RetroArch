#ifndef __UNLINK_UTF8_H
#define __UNLINK_UTF8_H

#include <stdbool.h>

#ifdef _WIN32

#if __cplusplus
extern "C"
{
#endif

bool unlink_utf8(const char * filename);

#if __cplusplus
}
#endif

#else
#include <unistd.h>
#define unlink_utf8 unlink
#endif
#endif
