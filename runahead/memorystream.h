#ifndef __MEMORYSTREAM_H__
#define __MEMORYSTREAM_H__

#ifdef WIN32
#define STDCALL __stdcall
#else
#define STDCALL
#endif

typedef struct tagIMemoryStream IMemoryStream;

typedef struct tagIMemory_vtable
{
   int (STDCALL *AddRef)(IMemoryStream *obj);
   int (STDCALL *Release)(IMemoryStream *obj);
   int (STDCALL *Read)(IMemoryStream *obj, unsigned char *data, int size);
   int (STDCALL *Write)(IMemoryStream *obj, const unsigned char *data, int size);
   int (STDCALL *GetPosition)(const IMemoryStream *obj);
   void (STDCALL *SetPosition)(IMemoryStream *obj, int position);
   int (STDCALL *GetLength)(const IMemoryStream *obj);
   void (STDCALL *SetLength)(IMemoryStream *obj, int length);
   unsigned char *(STDCALL *GetBuffer)(IMemoryStream *obj);
   unsigned const char *(STDCALL *GetBufferConst)(const IMemoryStream *obj);
   int (STDCALL *GetCapacity)(const IMemoryStream *obj);
   void (STDCALL *SetCapacity)(IMemoryStream *obj, int capacity);
} IMemoryStream_vtable;

struct tagIMemoryStream
{
   const IMemoryStream_vtable *vtable;
};

extern IMemoryStream *CreateIMemoryStream(void);

#endif
