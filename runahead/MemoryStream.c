#include "MemoryStream.h"
#include <string.h>
#include <malloc.h>

typedef struct tagCMemoryStream CMemoryStream;

struct tagCMemoryStream
{
   const IMemoryStream_vtable *vtable;
   int refCount;
   void *data;
   int position;
   int size;
   int capacity;
};

static int STDCALL CMemoryStream_AddRef(IMemoryStream *obj_p);
static int STDCALL CMemoryStream_Release(IMemoryStream *obj_p);
static int STDCALL CMemoryStream_Read(IMemoryStream *obj_p, unsigned char *data, int size);
static int STDCALL CMemoryStream_Write(IMemoryStream *obj_p, const unsigned char *data, int size);
static int STDCALL CMemoryStream_GetPosition(const IMemoryStream *obj_p);
static void STDCALL CMemoryStream_SetPosition(IMemoryStream *obj_p, int position);
static unsigned char *STDCALL CMemoryStream_GetBuffer(IMemoryStream *obj_p);
static const unsigned char *STDCALL CMemoryStream_GetBufferConst(const IMemoryStream *obj_p);
static int STDCALL CMemoryStream_GetLength(const IMemoryStream *obj_p);
static void STDCALL CMemoryStream_SetLength(IMemoryStream *obj_p, int length);
static int STDCALL CMemoryStream_GetCapacity(const IMemoryStream *obj_p);
static void STDCALL CMemoryStream_SetCapacity(IMemoryStream *obj_p, int capacity);

static const IMemoryStream_vtable CMemoryStream_vtable =
{
   CMemoryStream_AddRef,
   CMemoryStream_Release,
   CMemoryStream_Read,
   CMemoryStream_Write,
   CMemoryStream_GetPosition,
   CMemoryStream_SetPosition,
   CMemoryStream_GetLength,
   CMemoryStream_SetLength,
   CMemoryStream_GetBuffer,
   CMemoryStream_GetBufferConst,
   CMemoryStream_GetCapacity,
   CMemoryStream_SetCapacity
};

static int STDCALL CMemoryStream_AddRef(IMemoryStream *obj_p)
{
   CMemoryStream *obj = (CMemoryStream*)obj_p;
   return ++obj->refCount;
}

static int STDCALL CMemoryStream_Release(IMemoryStream *obj_p)
{
   CMemoryStream *obj = (CMemoryStream*)obj_p;
   int newRefCount = --obj->refCount;
   if (newRefCount == 0)
   {
      free(obj->data);
      free(obj);
   }
   return newRefCount;
}

static int STDCALL CMemoryStream_Read(IMemoryStream *obj_p, unsigned char *data, int size)
{
   CMemoryStream *obj = (CMemoryStream*)obj_p;
   int remaining = obj->size - obj->position;
   if (size > remaining)
   {
      size = remaining;
   }
   if (size > 0)
   {
      memcpy(data, (unsigned char*)obj->data + obj->position, size);
   }
   obj->position += size;
   return size;
}

static int STDCALL CMemoryStream_Write(IMemoryStream *obj_p, const unsigned char *data, int size)
{
   CMemoryStream *obj = (CMemoryStream*)obj_p;
   int remaining = obj->size - obj->position;
   if (size <= 0) return 0;
   if (size > remaining)
   {
      CMemoryStream_SetLength(obj_p, obj->position + size);
   }
   memcpy((const char*)obj->data + obj->position, data, size);
   obj->position += size;
   return size;
}

static int STDCALL CMemoryStream_GetPosition(const IMemoryStream *obj_p)
{
   CMemoryStream *obj = (CMemoryStream*)obj_p;
   return obj->position;
}

static void STDCALL CMemoryStream_SetPosition(IMemoryStream *obj_p, int position)
{
   CMemoryStream *obj = (CMemoryStream*)obj_p;
   if (position < 0) position = 0;
   if (position > obj->size)
   {
      CMemoryStream_SetLength(obj_p, position);
      position = obj->size;
   }
   obj->position = position;
}

static unsigned char *STDCALL CMemoryStream_GetBuffer(IMemoryStream *obj_p)
{
   CMemoryStream *obj = (CMemoryStream*)obj_p;
   return (unsigned char*)obj->data;
}

static const unsigned char *STDCALL CMemoryStream_GetBufferConst(const IMemoryStream *obj_p)
{
   const CMemoryStream *obj = (const CMemoryStream*)obj_p;
   return (const unsigned char*)obj->data;
}

static int STDCALL CMemoryStream_GetLength(const IMemoryStream *obj_p)
{
   const CMemoryStream *obj = (const CMemoryStream*)obj_p;
   return obj->size;
}

static void STDCALL CMemoryStream_SetLength(IMemoryStream *obj_p, int length)
{
   CMemoryStream *obj = (CMemoryStream*)obj_p;
   int newCapacity;
   if (length < 0) length = 0;
   if (length == obj->size) return;
   newCapacity = obj->capacity;
   while (newCapacity < length)
   {
      newCapacity *= 4;
      if (newCapacity == 0) newCapacity = 4096;
   }
   if (newCapacity != obj->capacity)
   {
      CMemoryStream_SetCapacity(obj_p, newCapacity);
   }
   obj->size = length;
}

static int STDCALL CMemoryStream_GetCapacity(const IMemoryStream *obj_p)
{
   const CMemoryStream *obj = (const CMemoryStream*)obj_p;
   return obj->capacity;
}

static void STDCALL CMemoryStream_SetCapacity(IMemoryStream *obj_p, int capacity)
{
   CMemoryStream *obj = (CMemoryStream*)obj_p;
   if (capacity < 0) capacity = 0;
   if (capacity <= 0) return;
   if (capacity < obj->size) capacity = obj->size;
   if (capacity == obj->capacity) return;
   obj->data = (unsigned char*)realloc(obj->data, capacity);
   obj->capacity = capacity;
}

static IMemoryStream *CMemoryStream_Constructor(void)
{
   CMemoryStream *obj_p = (CMemoryStream*)calloc(1, sizeof(CMemoryStream));
   obj_p->vtable = &CMemoryStream_vtable;
   obj_p->refCount = 1;
   return (IMemoryStream*)obj_p;
}

IMemoryStream *CreateIMemoryStream(void)
{
   return CMemoryStream_Constructor();
}

static void CMemoryStream_destructor_for_mylist(void *obj_p)
{
   IMemoryStream* obj = (IMemoryStream*)obj_p;
   obj->vtable->Release(obj);
}

static void *CMemoryStream_constructor_for_mylist(void)
{
   return (void*)CreateIMemoryStream();
}

