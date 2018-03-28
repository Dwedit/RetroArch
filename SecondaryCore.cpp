#if HAVE_DYNAMIC

#include <vector>
#include <string>
#include <string.h>
#include <malloc.h>
#include <map>
#include <tuple>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0500 || defined(_XBOX)
#ifndef LEGACY_WIN32
#define LEGACY_WIN32
#endif
#endif

#include "boolean.h"
#include "encodings/utf.h"
#include "compat/fopen_utf8.h"
#include "compat/unlink_utf8.h"
#include "dynamic/dylib.h"
#include "dynamic.h"
#include "core.h"
#include "file/file_path.h"
#include "paths.h"
#include "content.h"

extern bool input_is_dirty;

extern int port_map[16];

typedef void* (*constructor_t)(void);
typedef void(*destructor_t)(void*);

typedef struct MyList_t
{
	void **data;
	int capacity;
	int size;
	constructor_t Constructor;
	destructor_t Destructor;
} MyList;

typedef struct InputListElement_t
{
	unsigned port;
	unsigned device;
	unsigned index;
	int16_t state[36];
} InputListElement;

extern retro_ctx_load_content_info *load_content_info;
extern enum rarch_core_type last_core_type;

extern char *secondary_library_path;
extern dylib_t secondary_module;
extern retro_core_t secondary_core;
extern MyList *inputStateList;

void free_str(char **str_p);
void free_ptr(void **data_p);
bool free_file(FILE **file_p);
char *strcpy_alloc(const char *sourceStr);
void strcat_alloc(char ** destStr_p, const char *appendStr);
char* get_temp_directory_alloc();
char* copy_core_to_temp_file();
void* read_file_data_alloc(const char *fileName, int *size);
bool write_file_data(const char *fileName, const void *data, int dataSize);
bool write_file_with_random_name(char **tempDllPath, const char *retroarchTempPath, const void* data, int dataSize);
void *mylist_add_element(MyList *list);
void mylist_resize(MyList *list, int newSize);
void mylist_create(MyList **list_p, int initialCapacity, constructor_t constructor, destructor_t destructor);
void mylist_destroy(MyList **list_p);
void* InputListElementConstructor();
void input_state_destory();
void input_state_setlast(unsigned port, unsigned device, unsigned index, unsigned id, int16_t value);
int16_t input_state_getlast(unsigned port, unsigned device, unsigned index, unsigned id);
int16_t input_state_with_logging(unsigned port, unsigned device, unsigned index, unsigned id);
void add_input_state_hook();
void remove_input_state_hook();
void secondary_core_clear();
bool secondary_core_create();
void secondary_core_run_no_input_polling();
bool secondary_core_deserialize(const void *buffer, int size);
void secondary_core_destroy();
void set_last_core_type(enum rarch_core_type type);
void RememberControllerPortDevice(long port, long device);
void clear_port_map();

//using std::vector;
//using std::string;
//using std::map;
//using std::tuple;

//typedef unsigned char byte;

//#include "LoadContentCpp.hpp"


//RetroCtxLoadContentInfo loadContentInfo;

string CopyCoreToTempFile(bool &okay);

extern "C"
{
	extern retro_core_t current_core;
	extern retro_callbacks retro_ctx;
}

/*
TODO: Implement handlers for these:
RETRO_API void retro_set_environment(retro_environment_t);
RETRO_API void retro_set_video_refresh(retro_video_refresh_t);
RETRO_API void retro_set_audio_sample(retro_audio_sample_t);
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
RETRO_API void retro_set_input_poll(retro_input_poll_t);
RETRO_API void retro_set_input_state(retro_input_state_t);

TODO: Call these:
RETRO_API void retro_init(void);
RETRO_API void retro_deinit(void);
RETRO_API void retro_run(void);

RETRO_API bool retro_unserialize(const void *data, size_t size);
RETRO_API bool retro_load_game(const struct retro_game_info *game);
RETRO_API void retro_unload_game(void);
*/

retro_input_state_t input_state_callback_original;
/*
map<tuple<unsigned, unsigned, unsigned, unsigned>, int16_t> inputStateMap;
map<long, long> ControllerPortDeviceMap;

extern "C"
{
	bool InputIsDirty;
}

static int16_t InputStateGetLast(unsigned port, unsigned device,
	unsigned index, unsigned id)
{
	auto key = std::make_tuple(port, device, index, id);
	auto findResult = inputStateMap.find(key);
	if (findResult != inputStateMap.end())
	{
		return findResult->second;
	}
	return -1;
}

int16_t InputStateWithLogging(unsigned port, unsigned device,
	unsigned index, unsigned id)
{
	if (originalInputStateCallback != NULL)
	{
		int16_t result = originalInputStateCallback(port, device, index, id);
		int16_t lastInput = InputStateGetLast(port, device, index, id);
		if (result != lastInput)
		{
			InputIsDirty = true;
		}
		auto key = std::make_tuple(port, device, index, id);
		inputStateMap[key] = result;
		return result;
	}
	return 0;
}

void AddInputStateHook()
{
	if (originalInputStateCallback == NULL)
	{
		originalInputStateCallback = retro_ctx.state_cb;
		retro_ctx.state_cb = InputStateWithLogging;
		current_core.retro_set_input_state(retro_ctx.state_cb);
	}
}

void RemoveInputStateHook()
{
	if (originalInputStateCallback != NULL)
	{
		retro_ctx.state_cb = originalInputStateCallback;
		current_core.retro_set_input_state(retro_ctx.state_cb);
		originalInputStateCallback = NULL;
		inputStateMap.clear();
	}
}

class SecondaryCoreContext
{
public:
	string libraryPath;
	dylib_t module;
	retro_callbacks callbacks;
	retro_core_t core;


	SecondaryCoreContext()
	{
		module = NULL;
		memset(&core, 0, sizeof(retro_core_t));
		memset(&callbacks, 0, sizeof(retro_callbacks));
	}

	static void audio_sample_dummy(int16_t left, int16_t right)
	{

	}

	static size_t audio_sample_batch_dummy(const int16_t *data,	size_t frames)
	{
		return frames;
	}

	static int16_t input_state_dummy(unsigned port, unsigned device,
		unsigned index, unsigned id)
	{
		auto key = std::make_tuple(port, device, index, id);
		auto findResult = inputStateMap.find(key);
		if (findResult != inputStateMap.end())
		{
			return findResult->second;
		}
		return 0;
	}

	static void input_poll_dummy()
	{

	}

		
	retro_audio_sample_t sample_cb;
	retro_audio_sample_batch_t sample_batch_cb;
	retro_input_state_t state_cb;
	retro_input_poll_t poll_cb;

	bool Create()
	{
		bool okay;
		libraryPath = CopyCoreToTempFile(okay);
		if (!okay)
		{
			libraryPath = "";
			return false;
		}
		if (init_libretro_sym_custom(lastCoreType, &core, libraryPath.c_str(), &module))
		{
			AddInputStateHook();

			core.symbols_inited = true;
			memset(&callbacks, 0, sizeof(retro_callbacks));
			callbacks.frame_cb = retro_ctx.frame_cb;
			callbacks.poll_cb = input_poll_dummy;
			callbacks.sample_batch_cb = audio_sample_batch_dummy;
			callbacks.sample_cb = audio_sample_dummy;
			callbacks.state_cb = input_state_dummy;
			
			//memcpy(&callbacks, &retro_ctx, sizeof(retro_callbacks));

			//set the callbacks
			core.retro_set_audio_sample(callbacks.sample_cb);
			core.retro_set_audio_sample_batch(callbacks.sample_batch_cb);
			core.retro_set_video_refresh(callbacks.frame_cb);
			core.retro_set_input_state(callbacks.state_cb);
			core.retro_set_input_poll(callbacks.poll_cb);
			core.retro_set_environment(rarch_environment_cb);
			//run init
			core.retro_init();

			bool contentless, is_inited;
			::content_get_status(&contentless, &is_inited);
			core.inited = is_inited;
			//run load game
			if (loadContentInfo.special)
			{
				//feature disabled due to crashes
				return false;
				//core.game_loaded = core.retro_load_game_special(loadContentInfo.special->id, loadContentInfo.info, loadContentInfo.content->size);
				//if (!core.game_loaded)
				//{
				//	Destroy();
				//	return false;
				//}
			}
			else if (loadContentInfo.Content.Elems.size() > 0 && !loadContentInfo.Content.Elems[0].Data.empty())
			{
				core.game_loaded = core.retro_load_game(&loadContentInfo.Info);
				if (!core.game_loaded)
				{
					Destroy();
					return false;
				}
			}
			else if (contentless)
			{
				core.game_loaded = core.retro_load_game(NULL);
				if (!core.game_loaded)
				{
					Destroy();
					return false;
				}
			}
			else
			{
				core.game_loaded = false;
			}
			if (!core.inited)
			{
				Destroy();
				return false;
			}

			for (auto iterator = ControllerPortDeviceMap.begin(); iterator != ControllerPortDeviceMap.end(); iterator++)
			{
				long port = iterator->first;
				long device = iterator->second;
				core.retro_set_controller_port_device(port, device);
			}
			ControllerPortDeviceMap.clear();


		}
		else
		{
			return false;
		}
		return true;
	}

	void RunFrame()
	{
		core.retro_run();
	}

	bool Deserialize(const void *buffer, int size)
	{
		return core.retro_unserialize(buffer, size);
	}

	void Destroy()
	{
		if (module != NULL)
		{
			//unload game from core
			if (core.retro_unload_game != NULL)
			{
				core.retro_unload_game();
			}
			//deinit
			if (core.retro_deinit != NULL)
			{
				core.retro_deinit();
			}
			memset(&core, 0, sizeof(retro_core_t));
			memset(&callbacks, 0, sizeof(retro_callbacks_t));
			
			dylib_close(module);
			module = NULL;
			unlink_utf8(libraryPath.c_str());
			libraryPath = "";
		}
		RemoveInputStateHook();
	}
};

string GetTempDirectory()
{
	string tempPath;
#if _WIN32
	wchar_t buf[MAX_PATH];
	DWORD pathLen = GetTempPathW(MAX_PATH, &buf[0]);
	int pathByteLen = WideCharToMultiByte(CP_UTF8, 0, &buf[0], pathLen, NULL, 0, NULL, NULL);
	tempPath.resize(pathByteLen);
	WideCharToMultiByte(CP_UTF8, 0, &buf[0], pathLen, &tempPath[0], pathByteLen, NULL, NULL);
#else
	tempPath = getenv("TMPDIR");
#endif
	return tempPath;
}

vector<byte> ReadFileData(string fileName, bool &okay)
{
	okay = true;
	vector<byte> data;
	FILE *f = fopen_utf8(fileName.c_str(), "rb");
	if (f == NULL)
	{
		okay = false;
		return data;
	}
#if _WIN32
	int64_t fileSizeLong;
	fseek(f, 0, SEEK_END);
	fileSizeLong = _ftelli64(f);
	fseek(f, 0, SEEK_SET);
#else
	off64_t fileSizeLong;
	fseek(f, 0, SEEK_END);
	fileSizeLong = ftello64(f);
	fseek(f, 0, SEEK_SET);
#endif
	//256MB file size limit for DLL files
	if (fileSizeLong < 0 || fileSizeLong > 256 * 1024 * 1024)
	{
		okay = false;
		return data;
	}
	int fileSize = (int)fileSizeLong;
	data.resize(fileSize);
	size_t bytesRead = fread(&data[0], 1, data.size(), f);
	fclose(f);
	if ((int)bytesRead != (int)fileSize)
	{
		okay = false;
	}
	return data;
}

bool WriteFileData(string fileName, const vector<byte> &data)
{
	bool okay = true;
	FILE *f = fopen_utf8(fileName.c_str(), "wb");
	if (f == NULL) return false;
	size_t bytesWritten = fwrite(&data[0], 1, data.size(), f);
	if (bytesWritten != data.size())
	{
		okay = false;
	}
	fclose(f);
	return okay;
}

bool WriteFileWithRandomName(string &tempDllPath, const string &retroarchTempPath, const string &ext, const vector<byte> &data)
{
	bool okay = true;
	//try another name
	int maxAttempts = 30;
	string prefix = "tmp";
	char numberBuf[32];

	time_t timeValue = time(NULL);
	unsigned int numberValue = (unsigned int)timeValue;

	//try up to 30 'random' filenames before giving up
	for (int i = 0; i < 30; i++)
	{
		numberValue = numberValue * 214013 + 2531011;
		int number = (numberValue >> 16) % 100000;
		sprintf(numberBuf, "%05d", number);
		tempDllPath = retroarchTempPath + prefix + numberBuf + ext;
		okay = WriteFileData(tempDllPath, data);
		if (okay)
		{
			break;
		}
	}
	return okay;
}

string CopyCoreToTempFile(bool &okay)
{
	string corePath = path_get(RARCH_PATH_CORE);
	string coreBaseName = path_basename(corePath.c_str());
	if (coreBaseName.length() == 0)
	{
		okay = false;
		return "";
	}
	string tempPath = GetTempDirectory();
	if (tempPath.length() == 0)
	{
		okay = false;
		return "";
	}
	string retroarchTempPath = tempPath + path_default_slash() + "retroarch_temp" + path_default_slash();
	bool dirCreated = path_mkdir(retroarchTempPath.c_str());

	if (!dirCreated)
	{
		okay = false;
		return "";
	}

	vector<byte> dllFileData = ReadFileData(corePath, okay);
	if (!okay)
	{
		return "";
	}
	string tempDllPath = retroarchTempPath + coreBaseName;
	okay = WriteFileData(tempDllPath, dllFileData);
	if (!okay)
	{
		string ext = path_get_extension(coreBaseName.c_str());
		if (ext.length() > 0) ext = "." + ext;
		okay = WriteFileWithRandomName(tempDllPath, retroarchTempPath, ext, dllFileData);
		if (!okay)
		{
			return "";
		}
	}
	return tempDllPath;
}

SecondaryCoreContext secondaryCore;

extern "C"
{
	void SetLoadContentInfo(const retro_ctx_load_content_info_t *ctx)
	{
		loadContentInfo.Clear();
		if (ctx != NULL)
		{
			loadContentInfo.Assign(*ctx);
		}
	}
	void SetLastCoreType(enum rarch_core_type type)
	{
		lastCoreType = type;
	}
	void RememberControllerPortDevice(long port, long device)
	{
		ControllerPortDeviceMap[port] = device;
		if (secondaryCore.module != NULL && secondaryCore.core.retro_set_controller_port_device != NULL)
		{
			secondaryCore.core.retro_set_controller_port_device(port, device);
		}
	}
}


bool RunFrameSecondary()
{
	if (secondaryCore.module == NULL)
	{
		if (!secondaryCore.Create())
		{
			DestroySecondary();
			return false;
		}
	}
	secondaryCore.RunFrame();
	return true;
}

bool DeserializeSecondary(void *buffer, int size)
{
	if (secondaryCore.module == NULL)
	{
		if (!secondaryCore.Create())
		{
			DestroySecondary();
			return false;
		}
	}
	if (!secondaryCore.Deserialize(buffer, size))
	{
		DestroySecondary();
		return false;
	}
	return true;
}

void DestroySecondary()
{
	secondaryCore.Destroy();
	ControllerPortDeviceMap.clear();
}
*/
#else
extern "C"
{
	void SetLoadContentInfo(const retro_ctx_load_content_info_t *ctx)
	{
		//do nothing
	}
	void SetLastCoreType(enum rarch_core_type type)
	{
		//do nothing
	}
	bool RunFrameSecondary()
	{
		return false;
	}
	bool DeserializeSecondary(void *buffer, int size)
	{
		return false;
	}
	void DestroySecondary()
	{
		//do nothing
	}
	void RememberControllerPortDevice(long port, long device)
	{

	}

}
#endif

#define FREE(xxxx) if ((xxxx) != NULL) { free((void*)(xxxx)); } (xxxx) = NULL

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

void *malloc_zero(size_t size)
{
	void *ptr;
	ptr = malloc(size);
	memset(ptr, 0, size);
	return ptr;
}

bool free_file(FILE **file_p)
{
	bool result;
	if (file_p == NULL)
	{
		return true;
	}
	if (*file_p == NULL)
	{
		return true;
	}
	result = fclose(*file_p) != 0;
	*file_p = NULL;
	return result;
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
	int len;
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
	int len1, len2, newLen;
	char *destStr;
	char *reallocResult;

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

char* get_temp_directory_alloc()
{
#ifdef _WIN32
#ifdef LEGACY_WIN32
	DWORD pathLength;
	char *path;

	pathLength = GetTempPath(0, NULL) + 1;
	path = (char*)malloc(pathLength * sizeof(char));
	path[pathLength - 1] = 0;
	GetTempPath(pathLength, path);
	return path;
#else
	DWORD pathLength;
	wchar_t *wideStr;
	char *path;

	pathLength = GetTempPathW(0, NULL) + 1;
	wideStr = (wchar_t*)malloc(pathLength * sizeof(wchar_t));
	wideStr[pathLength - 1] = 0;
	GetTempPathW(pathLength, wideStr);

	path = utf16_to_utf8_string_alloc(wideStr);
	free(wideStr);
	return path;
#endif
#else
	char *path;
	path = strcpy_alloc_force(getenv("TMPDIR");
	return path;
#endif
}

char* copy_core_to_temp_file()
{
	bool okay;
	const char *corePath = NULL;  //ptr to static buffer, do not need to free this
	const char *coreBaseName = NULL;  //ptr to static buffer, do not need to free this
	char *tempDirectory = NULL;
	char *retroarchTempPath = NULL;
	char *tempDllPath = NULL;
	void *dllFileData = NULL;
	int dllFileSize = 0;

	corePath = path_get(RARCH_PATH_CORE);
	coreBaseName = path_basename(corePath);

	if (strlen(coreBaseName) == 0)
	{
		goto failed;
	}

	tempDirectory = get_temp_directory_alloc();
	if (tempDirectory == NULL)
	{
		goto failed;
	}

	strcat_alloc(&retroarchTempPath, tempDirectory);
	strcat_alloc(&retroarchTempPath, path_default_slash());
	strcat_alloc(&retroarchTempPath, "retroarch_temp");
	strcat_alloc(&retroarchTempPath, path_default_slash());

	okay = path_mkdir(retroarchTempPath);
	if (!okay)
	{
		goto failed;
	}

	dllFileData = read_file_data_alloc(corePath, &dllFileSize);
	if (dllFileData == NULL)
	{
		goto failed;
	}
	strcat_alloc(&tempDllPath, retroarchTempPath);
	strcat_alloc(&tempDllPath, coreBaseName);
	okay = write_file_data(tempDllPath, dllFileData, dllFileSize);
	if (!okay)
	{
		//try other file names
		okay = write_file_with_random_name(&tempDllPath, retroarchTempPath, dllFileData, dllFileSize);
		if (!okay)
		{
			goto failed;
		}
	}
success:
	free_str(&tempDirectory);
	free_str(&retroarchTempPath);
	free_ptr(&dllFileData);
	return tempDllPath;

failed:
	free_str(&tempDirectory);
	free_str(&retroarchTempPath);
	free_str(&tempDllPath);
	free_ptr(&dllFileData);
	return NULL;
}

void* read_file_data_alloc(const char *fileName, int *size)
{
	void *data = NULL;
	FILE *f = NULL;
	int fileSize = 0;
	size_t bytesRead = 0;
#ifdef _WIN32
	int64_t fileSizeLong = 0;
#else
	off64_t fileSizeLong = 0;
#endif
	f = fopen_utf8(fileName, "rb");
	if (f == NULL)
	{
		goto failed;
	}
	fseek(f, 0, SEEK_END);
#ifdef _WIN32
	fileSizeLong = _ftelli64(f);
#else
	fileSizeLong = ftello64(f);
#endif
	fseek(f, 0, SEEK_SET);
	//256MB file size limit for DLL files
	if (fileSizeLong < 0 || fileSizeLong > 256 * 1024 * 1024)
	{
		goto failed;
	}
	fileSize = (int)fileSizeLong;
	data = malloc(fileSize);
	if (data == NULL)
	{
		goto failed;
	}
	bytesRead = fread(data, 1, fileSize, f);
	if ((int)bytesRead != (int)fileSize)
	{
		goto failed;
	}
success:
	free_file(&f);
	if (size != NULL) *size = fileSize;
	return data;
failed:
	free_ptr(&data);
	free_file(&f);
	if (size != NULL) *size = 0;
	return NULL;
}

bool write_file_data(const char *fileName, const void *data, int dataSize)
{
	bool okay = false;
	FILE *f = NULL;
	size_t bytesWritten = 0;

	f = fopen_utf8(fileName, "wb");
	if (f == NULL) goto failed;
	bytesWritten = fwrite(data, 1, dataSize, f);
	if (bytesWritten != dataSize)
	{
		goto failed;
	}
	okay = fflush(f) != 0;
	if (!okay)
	{
		goto failed;
	}
	okay = fclose(f) != 0;
	f = NULL;
	if (!okay)
	{
		goto failed;
	}
success:
	free_file(&f);
	return true;
failed:
	free_file(&f);
	return false;
}

bool write_file_with_random_name(char **tempDllPath, const char *retroarchTempPath, const void* data, int dataSize)
{
	char *ext = NULL;
	bool okay = false;
	const int maxAttempts = 30;
	const char *prefix = "tmp";
	char numberBuf[32];
	time_t timeValue = time(NULL);
	unsigned int numberValue = (unsigned int)timeValue;
	int number = 0;
	int i;

	ext = strcpy_alloc_force(path_get_extension(*tempDllPath));

	//try up to 30 'random' filenames before giving up
	for (i = 0; i < 30; i++)
	{
		numberValue = numberValue * 214013 + 2531011;
		number = (numberValue >> 14) % 100000;
		sprintf(numberBuf, "%05d", number);
		free_str(tempDllPath);
		strcat_alloc(tempDllPath, retroarchTempPath);
		strcat_alloc(tempDllPath, prefix);
		strcat_alloc(tempDllPath, numberBuf);
		strcat_alloc(tempDllPath, ext);
		okay = write_file_data(*tempDllPath, data, dataSize);
		if (okay)
		{
			break;
		}
	}
success:
	free_str(&ext);
	return true;
failed:
	free_str(&ext);
	return false;
}

void mylist_resize(MyList *list, int newSize)
{
	int newCapacity;
	int oldSize;
	int i;
	void *element;
	void **newData;
	if (newSize < 0) newSize = 0;
	if (list == NULL) return;
	newCapacity = newSize;
	oldSize = list->size;
	if (newSize > list->capacity)
	{
		if (newCapacity < list->capacity * 2)
		{
			newCapacity = list->capacity * 2;
		}
		//try to realloc
		list->data = (void**)realloc((void*)list->data, newCapacity * sizeof(void*));
		for (i = list->capacity; i < newCapacity; i++)
		{
			list->data[i] = NULL;
		}
		list->capacity = newCapacity;
	}
	if (newSize <= list->size)
	{
		for (i = newSize; i < list->size; i++)
		{
			element = list->data[i];
			if (element != NULL)
			{
				list->Destructor(element);
				list->data[i] = NULL;
			}
		}
		list->size = NULL;
	}
	else
	{
		for (i = list->size; i < newSize; i++)
		{
			list->data[i] = list->Constructor();
		}
	}
}

void *mylist_add_element(MyList *list)
{
	int oldSize;
	if (list == NULL) return NULL;
	oldSize = list->size;
	mylist_resize(list, oldSize + 1);
	return list->data[oldSize];
}

void mylist_create(MyList **list_p, int initialCapacity, constructor_t constructor, destructor_t destructor)
{
	MyList *list;
	if (list_p == NULL) return;
	if (initialCapacity < 0) initialCapacity = 0;
	list = *list_p;
	if (list != NULL)
	{
		mylist_destroy(list_p);
	}
	list = (MyList*)malloc(sizeof(MyList));
	*list_p = list;
	list->size = 0;
	list->Constructor = constructor;
	list->Destructor = destructor;
	if (initialCapacity > 0)
	{
		list->data = (void**)malloc_zero(initialCapacity * sizeof(void*));
		list->capacity = initialCapacity;
	}
	else
	{
		list->data = NULL;
		list->capacity = 0;
	}
}

void mylist_destroy(MyList **list_p)
{
	int i;
	if (list_p == NULL) return;
	MyList *list;
	list = *list_p;
	if (list != NULL)
	{
		mylist_resize(list, 0);
		free(list->data);
		free(list);
		*list_p = NULL;
	}
}

void* InputListElementConstructor()
{
	void *ptr;
	const int size = sizeof(InputListElement);
	ptr = malloc_zero(size);
	return ptr;
}

MyList *inputStateList;

void input_state_destory()
{
	mylist_destroy(&inputStateList);
}

void input_state_setlast(unsigned port, unsigned device, unsigned index, unsigned id, int16_t value)
{
	int i;
	InputListElement *element;

	if (inputStateList == NULL)
	{
		mylist_create(&inputStateList, 16, InputListElementConstructor, free);
	}

	//find list item
	for (i = 0; i < inputStateList->size; i++)
	{
		element = (InputListElement*)inputStateList->data[i];
		if (element->port == port && element->device == device && element->index == index)
		{
			element->state[id] = value;
			return;
		}
	}
	element = (InputListElement*)mylist_add_element(inputStateList);
	element->port = port;
	element->device = device;
	element->index = index;
	element->state[id] = value;
}

static int16_t input_state_getlast(unsigned port, unsigned device, unsigned index, unsigned id)
{
	int i;
	InputListElement *element;
	if (inputStateList == NULL)
	{
		return 0;
	}
	//find list item
	for (i = 0; i < inputStateList->size; i++)
	{
		element = (InputListElement*)inputStateList->data[i];
		if (element->port == port && element->device == device && element->index == index)
		{
			return element->state[id];
		}
	}
	return 0;
}

int16_t input_state_with_logging(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if (input_state_callback_original != NULL)
	{
		int16_t result = input_state_callback_original(port, device, index, id);
		int16_t lastInput = input_state_getlast(port, device, index, id);
		if (result != lastInput)
		{
			input_is_dirty = true;
		}
		input_state_setlast(port, device, index, id, result);
		return result;
	}
	return 0;
}

void add_input_state_hook()
{
	if (input_state_callback_original == NULL)
	{
		input_state_callback_original = retro_ctx.state_cb;
		retro_ctx.state_cb = input_state_with_logging;
		current_core.retro_set_input_state(retro_ctx.state_cb);
	}
}

void remove_input_state_hook()
{
	if (input_state_callback_original != NULL)
	{
		retro_ctx.state_cb = input_state_callback_original;
		current_core.retro_set_input_state(retro_ctx.state_cb);
		input_state_callback_original = NULL;
		input_state_destory();
	}
}

char *secondary_library_path;
dylib_t secondary_module;
retro_core_t secondary_core;

void secondary_core_clear()
{
	secondary_library_path = NULL;
	secondary_module = NULL;
	memset(&secondary_core, 0, sizeof(retro_core_t));
}

bool secondary_core_create()
{
	long port, device;
	bool contentless, is_inited;

	if (last_core_type != CORE_TYPE_PLAIN || load_content_info == NULL || load_content_info->special != NULL)
	{
		return false;
	}

	free_str(&secondary_library_path);
	secondary_library_path = copy_core_to_temp_file();
	if (secondary_library_path == NULL)
	{
		return false;
	}
	//Load Core
	if (init_libretro_sym_custom(CORE_TYPE_PLAIN, &secondary_core, secondary_library_path, &secondary_module))
	{
		add_input_state_hook();
		secondary_core.symbols_inited = true;
		secondary_core.retro_init();
		
		content_get_status(&contentless, &is_inited);
		secondary_core.inited = is_inited;

		//Load Content
		if (load_content_info == NULL || load_content_info->special != NULL)
		{
			//disabled due to crashes
			return false;
			//secondary_core.game_loaded = secondary_core.retro_load_game_special(loadContentInfo.special->id, loadContentInfo.info, loadContentInfo.content->size);
			//if (!secondary_core.game_loaded)
			//{
			//	secondary_core_destroy();
			//	return false;
			//}
		}
		else if (load_content_info->content->size > 0 && load_content_info->content->elems[0].data != NULL)
		{
			secondary_core.game_loaded = secondary_core.retro_load_game(load_content_info->info);
			if (!secondary_core.game_loaded)
			{
				secondary_core_destroy();
				return false;
			}
		}
		else if (contentless)
		{
			secondary_core.game_loaded = secondary_core.retro_load_game(NULL);
			if (!secondary_core.game_loaded)
			{
				secondary_core_destroy();
				return false;
			}
		}
		else
		{
			secondary_core.game_loaded = false;
		}
		if (!secondary_core.inited)
		{
			secondary_core_destroy();
			return false;
		}

		for (port = 0; port < 16; port++)
		{
			device = port_map[port];
			if (device >= 0)
			{
				secondary_core.retro_set_controller_port_device(port, device);
			}
		}
		clear_port_map();
	}
	else
	{
		return false;
	}
	return true;
}

void secondary_core_run_no_input_polling()
{
	secondary_core.retro_run();
}

bool secondary_core_deserialize(const void *buffer, int size)
{
	return secondary_core.retro_unserialize(buffer, size);
}

void secondary_core_destroy()
{
	if (secondary_module != NULL)
	{
		//unload game from core
		if (secondary_core.retro_unload_game != NULL)
		{
			secondary_core.retro_unload_game();
		}
		//deinit
		if (secondary_core.retro_deinit != NULL)
		{
			secondary_core.retro_deinit();
		}
		memset(&secondary_core, 0, sizeof(retro_core_t));

		dylib_close(secondary_module);
		secondary_module = NULL;
		unlink_utf8(secondary_library_path);
		free_str(&secondary_library_path);
	}
	remove_input_state_hook();
}

void free_retro_game_info(retro_game_info *dest)
{
	if (dest == NULL) return;
	FREE(dest->path);
	FREE(dest->data);
	FREE(dest->meta);
}

retro_game_info* clone_retro_game_info(const retro_game_info *src)
{
	retro_game_info *dest;
	if (src == NULL) return NULL;
	dest = (retro_game_info*)malloc_zero(sizeof(retro_game_info));
	dest->path = strcpy_alloc(src->path);
	dest->data = memcpy_alloc(src->data, src->size);
	dest->size = src->size;
	dest->meta = strcpy_alloc(src->meta);
	return dest;
}

void free_string_list(string_list *dest)
{
	int i;
	if (dest == NULL) return;
	for (i = 0; i < dest->size; i++)
	{
		FREE(dest->elems[i].data);
	}
	FREE(dest->elems);
}

string_list* clone_string_list(const string_list *src)
{
	int i;
	string_list *dest;
	if (src == NULL) return NULL;
	dest = (string_list*)malloc_zero(sizeof(string_list));
	dest->size = src->size;
	dest->cap = src->cap;
	dest->elems = (string_list_elem*)malloc_zero(sizeof(string_list_elem) * dest->size);
	for (i = 0; i < src->size; i++)
	{
		dest->elems[i].data = strcpy_alloc(src->elems[i].data);
		dest->elems[i].attr = src->elems[i].attr;
	}
	return dest;
}
/*
void free_retro_subsystem_memory_info(retro_subsystem_memory_info *dest)
{
	if (dest == NULL) return;
	FREE(dest->extension);
}

void clone_retro_subsystem_memory_info(retro_subsystem_memory_info* dest, const retro_subsystem_memory_info *src)
{
	dest->extension = strcpy_alloc(src->extension);
	dest->type = src->type;
}

void free_retro_subsystem_rom_info(retro_subsystem_rom_info *dest)
{
	int i;
	if (dest == NULL) return;
	FREE(dest->desc);
	FREE(dest->valid_extensions);
	for (i = 0; i < dest->num_memory; i++)
	{
		free_retro_subsystem_memory_info((retro_subsystem_memory_info*)&dest->memory[i]);
	}
	FREE(dest->memory);
}

void clone_retro_subsystem_rom_info(retro_subsystem_rom_info *dest, const retro_subsystem_rom_info *src)
{
	int i;
	retro_subsystem_memory_info *memory;
	dest->need_fullpath = src->need_fullpath;
	dest->block_extract = src->block_extract;
	dest->required = src->required;
	dest->num_memory = src->num_memory;
	dest->desc = strcpy_alloc(src->desc);
	dest->valid_extensions = strcpy_alloc(src->valid_extensions);
	memory = (retro_subsystem_memory_info*)malloc_zero(dest->num_memory * sizeof(retro_subsystem_memory_info));
	dest->memory = memory;
	for (i = 0; i < dest->num_memory; i++)
	{
		clone_retro_subsystem_memory_info(&memory[i], &src->memory[i]);
	}
}

void free_retro_subsystem_info(retro_subsystem_info *dest)
{
	int i;
	if (dest == NULL) return;
	FREE(dest->desc);
	FREE(dest->ident);
	for (i = 0; i < dest->num_roms; i++)
	{
		free_retro_subsystem_rom_info((retro_subsystem_rom_info*)&dest->roms[i]);
	}
	FREE(dest->roms);
}

retro_subsystem_info* clone_retro_subsystem_info(const retro_subsystem_info *src)
{
	int i;
	retro_subsystem_info *dest;
	retro_subsystem_rom_info *roms;
	if (src == NULL) return NULL;
	dest = (retro_subsystem_info*)malloc_zero(sizeof(retro_subsystem_info));
	dest->desc = strcpy_alloc(src->desc);
	dest->ident = strcpy_alloc(src->ident);
	dest->num_roms = src->num_roms;
	dest->id = src->id;

	roms = (retro_subsystem_rom_info*)malloc_zero(src->num_roms * sizeof(retro_subsystem_rom_info));
	dest->roms = roms;
	for (i = 0; i < src->num_roms; i++)
	{
		clone_retro_subsystem_rom_info(&roms[i], &src->roms[i]);
	}
	return dest;
}

*/

void free_retro_ctx_load_content_info(retro_ctx_load_content_info *dest)
{
	if (dest == NULL) return;
	free_retro_game_info(dest->info);
	free_string_list((string_list*)dest->content);
	//free_retro_subsystem_info((retro_subsystem_info*)dest->special);
	FREE(dest->info);
	FREE(dest->content);
	//FREE(dest->special);
}

retro_ctx_load_content_info* clone_retro_ctx_load_content_info(const retro_ctx_load_content_info *src)
{
	retro_ctx_load_content_info *dest;
	if (src->special != NULL) return NULL;	//refuse to deal with the Special field

	dest = (retro_ctx_load_content_info*)malloc_zero(sizeof(retro_ctx_load_content_info));
	dest->info = clone_retro_game_info(src->info);
	dest->content = clone_string_list(src->content);
	dest->special = NULL;
	//dest->special = clone_retro_subsystem_info(src->special);
	return dest;
}

int port_map[16];

void set_load_content_info(const retro_ctx_load_content_info_t *ctx)
{
	free_retro_ctx_load_content_info(load_content_info);
	free(load_content_info);
	load_content_info = clone_retro_ctx_load_content_info(ctx);
}

void set_last_core_type(enum rarch_core_type type)
{
	last_core_type = type;
}

void remember_controller_port_device(long port, long device)
{
	if (port >= 0 && port < 16)
	{
		port_map[port] = device;
	}
	if (secondary_module != NULL && secondary_core.retro_set_controller_port_device != NULL)
	{
		secondary_core.retro_set_controller_port_device(port, device);
	}
}

void clear_port_map()
{
	int port;
	for (port = 0; port < 16; port++)
	{
		port_map[port] = -1;
	}
}

bool input_is_dirty;
