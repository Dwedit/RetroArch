#include <vector>
#include <string>
#include <string.h>
#include <malloc.h>
#include <map>
#include <tuple>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using std::vector;
using std::string;
using std::map;
using std::tuple;

#include "compat/fopen_utf8.h"
#include "compat/unlink_utf8.h"
#include "dynamic/dylib.h"
#include "dynamic.h"
#include "core.h"
#include "file/file_path.h"
#include "paths.h"
#include "content.h"

typedef unsigned char byte;

#include "LoadContentCpp.hpp"

RetroCtxLoadContentInfo loadContentInfo;
enum rarch_core_type lastCoreType;

string CopyCoreToTempFile();

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

retro_input_state_t originalInputStateCallback;
map<tuple<unsigned, unsigned, unsigned, unsigned>, int16_t> inputStateMap;

bool InputIsDirty;

static int16_t input_state_getlast(unsigned port, unsigned device,
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

int16_t input_state_with_logging(unsigned port, unsigned device,
	unsigned index, unsigned id)
{
	if (originalInputStateCallback != NULL)
	{
		int16_t result = originalInputStateCallback(port, device, index, id);
		int16_t lastInput = input_state_getlast(port, device, index, id);
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
		retro_ctx.state_cb = input_state_with_logging;
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

	void Create()
	{
		libraryPath = CopyCoreToTempFile();
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

			//run load game
			if (loadContentInfo.special)
			{
				core.game_loaded = core.retro_load_game_special(loadContentInfo.special->id, loadContentInfo.info, loadContentInfo.content->size);
			}
			else if (loadContentInfo.Content.Elems.size () > 0 && !loadContentInfo.Content.Elems[0].Data.empty())
			{
				core.game_loaded = core.retro_load_game(&loadContentInfo.Info);
			}
			else if (contentless)
			{
				core.game_loaded = core.retro_load_game(NULL);
			}
			else
			{
				core.game_loaded = false;
			}

			core.inited = true;
		}

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

vector<byte> ReadFileData(string fileName)
{
	vector<byte> data;
	FILE *f = fopen_utf8(fileName.c_str(), "rb");
	if (f == NULL) return data;
	fseek(f, 0, SEEK_END);
	long fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	data.resize(fileSize);
	fread(&data[0], 1, data.size(), f);
	fclose(f);
	return data;
}

bool WriteFileData(string fileName, const vector<byte> &data)
{
	FILE *f = fopen_utf8(fileName.c_str(), "wb");
	if (f == NULL) return false;
	fwrite(&data[0], 1, data.size(), f);
	fclose(f);
	return true;
}

string CopyCoreToTempFile()
{
	//proof of concept, no error checking yet
	string corePath = path_get(RARCH_PATH_CORE);
	string coreBaseName = path_basename(corePath.c_str());
	string tempPath = GetTempDirectory();
	string retroarchTempPath = tempPath + path_default_slash() + "retroarch_temp" + path_default_slash();
	path_mkdir(retroarchTempPath.c_str());
	
	//fixme - pick a unique name instead of this
	string tempDllPath = retroarchTempPath + coreBaseName;
	WriteFileData(tempDllPath, ReadFileData(corePath));
	return tempDllPath;
}

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
}

SecondaryCoreContext secondaryCore;

void RunFrameSecondary()
{
	if (secondaryCore.module == NULL)
	{
		secondaryCore.Create();
	}
	secondaryCore.RunFrame();
}

void DeserializeSecondary(void *buffer, int size)
{
	if (secondaryCore.module == NULL)
	{
		secondaryCore.Create();
	}
	secondaryCore.Deserialize(buffer, size);
}

void DestroySecondary()
{
	if (secondaryCore.module != NULL)
	{
		secondaryCore.Destroy();
	}
}
