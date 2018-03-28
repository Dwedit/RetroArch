#include "mem_util.h"

void *malloc_zero(size_t size)
{
	void *ptr;
	ptr = malloc(size);
	memset(ptr, 0, size);
	return ptr;
}

void free_str(char **str_p)
{
	free_ptr((void**)str_p);
}

void free_ptr(void **data_p)
{
	if (data_p == NULL)
	{
		return;
	}
	if (*data_p == NULL)
	{
		return;
	}
	free(*data_p);
	*data_p = NULL;
}

void *memcpy_alloc(const void *src, size_t size)
{
	void *result;
	result = malloc(size);
	memcpy(result, src, size);
	return result;
}

char *strcpy_alloc(const char *sourceStr)
{
	size_t len;
	char *result;
	if (sourceStr == NULL)
	{
		len = 0;
	}
	else
	{
		len = strlen(sourceStr);
	}
	if (len == 0)
	{
		return NULL;
	}
	result = (char*)malloc(len + 1);
	strcpy(result, sourceStr);
	return result;
}

char *strcpy_alloc_force(const char *sourceStr)
{
	char *result;
	result = strcpy_alloc(sourceStr);
	if (result == NULL)
	{
		result = (char*)malloc_zero(1);
	}
	return result;
}

void strcat_alloc(char ** destStr_p, const char *appendStr)
{
	size_t len1, len2, newLen;
	char *destStr;

	destStr = *destStr_p;

	if (destStr == NULL)
	{
		destStr = strcpy_alloc_force(appendStr);
		*destStr_p = destStr;
		return;
	}

	if (appendStr == NULL)
	{
		return;
	}

	len1 = strlen(destStr);
	len2 = strlen(appendStr);
	newLen = len1 + len2 + 1;
	destStr = (char*)realloc(destStr, newLen);
	*destStr_p = destStr;
	strcpy(destStr + len1, appendStr);
}
