#if HAVE_DYNAMIC

#include <string.h>
#include <malloc.h>
#include <time.h>

#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0500 || defined(_XBOX)
#ifndef LEGACY_WIN32
#define LEGACY_WIN32
#endif
#endif

#include "mem_util.h"
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

static int port_map[16];

typedef struct retro_core_t _retro_core_t;
typedef struct retro_callbacks retro_callbacks_t;

static char *secondary_library_path;
static dylib_t secondary_module;
static _retro_core_t secondary_core;
static struct retro_callbacks secondary_callbacks;

extern retro_ctx_load_content_info_t *load_content_info;
extern enum rarch_core_type last_core_type;
extern struct retro_callbacks retro_ctx;

static char* get_temp_directory_alloc(void);
static char* copy_core_to_temp_file(void);
static void* read_file_data_alloc(const char *fileName, int *size);
static bool write_file_data(const char *fileName, const void *data, int dataSize);
static bool write_file_with_random_name(char **tempDllPath, const char *retroarchTempPath, const void* data, int dataSize);
static void* InputListElementConstructor(void);
static void secondary_core_clear(void);
static bool secondary_core_create(void);
bool secondary_core_run_no_input_polling(void);
bool secondary_core_deserialize(const void *buffer, int size);
void secondary_core_destroy(void);
void set_last_core_type(enum rarch_core_type type);
void remember_controller_port_device(long port, long device);
void clear_controller_port_map(void);

static void free_file(FILE **file_p);

char* get_temp_directory_alloc(void)
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

char* copy_core_to_temp_file(void)
{
	bool okay;
	const char *corePath = NULL;  /* ptr to static buffer, do not need to free this */
	const char *coreBaseName = NULL;  /* ptr to static buffer, do not need to free this */
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
		/* try other file names */
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
	/* 256MB file size limit for DLL files */
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
success:
	free_file(&f);
	return true;
failed:
	free_file(&f);
	return false;
}

bool write_file_with_random_name(char **tempDllPath, const char *retroarchTempPath, const void* data, int dataSize)
{
   int extLen;
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
   extLen = strlen(ext);
   if (extLen > 0)
   {
      strcat_alloc(&ext, ".");
      memmove(ext + 1, ext, extLen);
      ext[0] = '.';
      extLen++;
   }


	/* try up to 30 'random' filenames before giving up */
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

void secondary_core_clear(void)
{
	secondary_library_path = NULL;
	secondary_module = NULL;
	memset(&secondary_core, 0, sizeof(struct retro_core_t));
}

bool secondary_core_create(void)
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
	/* Load Core */
	if (init_libretro_sym_custom(CORE_TYPE_PLAIN, &secondary_core, secondary_library_path, &secondary_module))
	{
      secondary_core.symbols_inited = true;

      core_set_default_callbacks(&secondary_callbacks);
      secondary_core.retro_set_video_refresh(secondary_callbacks.frame_cb);
      secondary_core.retro_set_audio_sample(secondary_callbacks.sample_cb);
      secondary_core.retro_set_audio_sample_batch(secondary_callbacks.sample_batch_cb);
      secondary_core.retro_set_input_state(secondary_callbacks.state_cb);
      secondary_core.retro_set_input_poll(secondary_callbacks.poll_cb);
      secondary_core.retro_set_environment(rarch_environment_cb);

		secondary_core.retro_init();
		
		content_get_status(&contentless, &is_inited);
		secondary_core.inited = is_inited;

		/* Load Content */
		if (load_content_info == NULL || load_content_info->special != NULL)
		{
			/* disabled due to crashes */
			return false;
#if 0
			secondary_core.game_loaded = secondary_core.retro_load_game_special(loadContentInfo.special->id, loadContentInfo.info, loadContentInfo.content->size);
			if (!secondary_core.game_loaded)
			{
				secondary_core_destroy();
				return false;
			}
#endif
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
		clear_controller_port_map();
	}
	else
	{
		return false;
	}
	return true;
}

bool secondary_core_run_no_input_polling(void)
{
   bool okay;
   if (secondary_module == NULL)
   {
      okay = secondary_core_create();
      if (!okay)
      {
         return false;
      }
   }
	secondary_core.retro_run();
   return true;
}

bool secondary_core_deserialize(const void *buffer, int size)
{
   bool okay;
   if (secondary_module == NULL)
   {
      okay = secondary_core_create();
      if (!okay)
      {
         secondary_core_destroy();
         return false;
      }
   }
   return secondary_core.retro_unserialize(buffer, size);
}

void secondary_core_destroy(void)
{
	if (secondary_module != NULL)
	{
		/* unload game from core */
		if (secondary_core.retro_unload_game != NULL)
		{
			secondary_core.retro_unload_game();
		}
		/* deinit */
		if (secondary_core.retro_deinit != NULL)
		{
			secondary_core.retro_deinit();
		}
		memset(&secondary_core, 0, sizeof(struct retro_core_t));

		dylib_close(secondary_module);
		secondary_module = NULL;
		unlink_utf8(secondary_library_path);
		free_str(&secondary_library_path);
	}
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

void clear_controller_port_map(void)
{
	int port;
	for (port = 0; port < 16; port++)
	{
		port_map[port] = -1;
	}
}

static void free_file(FILE **file_p)
{
   bool result;
   if (file_p == NULL)
   {
      return;
   }
   if (*file_p == NULL)
   {
      return;
   }
   result = fclose(*file_p) != 0;
   *file_p = NULL;
   return;
}


#else
#include "boolean.h"
#include "core.h"
bool secondary_core_run_no_input_polling(void)
{
	return false;
}
bool secondary_core_deserialize(const void *buffer, int size)
{
	return false;
}
void secondary_core_destroy(void)
{
	/* do nothing */
}
void remember_controller_port_device(long port, long device)
{
	/* do nothing */
}
#endif

