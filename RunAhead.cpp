#include "core.h"
#include "dynamic.h"
#include "audio/audio_driver.h"
#include "gfx/video_driver.h"
#include <vector>
#include <string>
#include <string.h>
using std::vector;
using std::string;

typedef unsigned char byte;

void RunFrameSecondary();
void DeserializeSecondary(void *buffer, int size);
void DestroySecondary();

extern "C"
{
	extern struct retro_callbacks retro_ctx;
	extern struct retro_core_t current_core;
}

extern bool InputIsDirty;

function_t originalRetroDeinit = NULL;
function_t originalRetroUnload = NULL;

void DeinitHook();
void UnloadHook();

void AddDestroyHook()
{
	if (originalRetroDeinit == NULL)
	{
		originalRetroDeinit = current_core.retro_deinit;
		originalRetroUnload = current_core.retro_unload_game;
		current_core.retro_deinit = DeinitHook;
		current_core.retro_unload_game = UnloadHook;
	}
}

void RemoveDestroyHook()
{
	if (originalRetroDeinit != NULL)
	{
		current_core.retro_deinit = originalRetroDeinit;
		current_core.retro_unload_game = originalRetroUnload;
		originalRetroDeinit = NULL;
		originalRetroUnload = NULL;
	}
}


class RunAheadContext
{
	size_t saveStateSize;
	vector<byte> saveStateData;
	retro_ctx_serialize_info_t serial_info;
	bool videoDriverIsActive;
	bool forceInputDirty = false;
public:
	RunAheadContext()
	{
		saveStateSize = 0xFFFFFFFF;
		serial_info.data = NULL;
		serial_info.data_const = NULL;
		serial_info.size = 0;
	}

	void RunAhead(int runAheadCount, bool useSecondary)
	{
		if (runAheadCount <= 0)
		{
			core_run();
			return;
		}

		if (saveStateSize == 0xFFFFFFFF)
		{
			Create();
		}

		int runAhead = runAheadCount;
		int frameNumber = 0;
		
		if (!useSecondary)
		{
			for (frameNumber = 0; frameNumber <= runAheadCount; frameNumber++)
			{
				bool lastFrame = frameNumber == runAheadCount;
				bool suspendedFrame = !lastFrame;
				if (suspendedFrame)
				{
					SuspendAudio();
					SuspendVideo();
				}
				core_run();
				if (suspendedFrame)
				{
					ResumeVideo();
					ResumeAudio();
				}
				if (frameNumber == 0)
				{
					SaveState();
				}
				if (lastFrame)
				{
					LoadState();
				}
			}
		}
		else
		{
			//run main core with video suspended
			SuspendVideo();
			core_run();
			ResumeVideo();

			bool inputDirty = InputIsDirty || forceInputDirty;

			if (inputDirty)
			{
				InputIsDirty = false;
				SaveState();
				LoadStateSecondary();
				for (int frameCount = 0; frameCount < runAheadCount - 1; frameCount++)
				{
					SuspendVideo();
					RunFrameSecondary();
					ResumeVideo();
				}
			}
			RunFrameSecondary();
		}
		forceInputDirty = false;
	}

	void Create()
	{
		//get savestate size and allocate buffer
		retro_ctx_size_info_t info;
		core_serialize_size(&info);

		saveStateSize = info.size;
		saveStateData.clear();
		saveStateData.resize(saveStateSize);
		
		serial_info.data = &saveStateData[0];
		serial_info.data_const = &saveStateData[0];
		serial_info.size = saveStateData.size();

		videoDriverIsActive = video_driver_is_active();

		AddDestroyHook();

		forceInputDirty = true;
	}
	void SaveState()
	{
		core_serialize(&serial_info);
	}
	void LoadState()
	{
		core_unserialize(&serial_info);
	}
	void LoadStateSecondary()
	{
		DeserializeSecondary(&saveStateData[0], (int)saveStateData.size());
	}
	void SuspendAudio()
	{
		audio_driver_suspend();
	}
	void ResumeAudio()
	{
		audio_driver_resume();
	}
	void SuspendVideo()
	{
		video_driver_unset_active();
	}
	void ResumeVideo()
	{
		if (videoDriverIsActive)
		{
			video_driver_set_active();
		}
		else
		{
			video_driver_unset_active();
		}
	}

	void Destroy()
	{
		saveStateSize = 0xFFFFFFFF;
		saveStateData.clear();
		serial_info.data = NULL;
		serial_info.data_const = NULL;
		serial_info.size = NULL;
	}
};

RunAheadContext runAheadContext;

extern "C"
{
	void RunAhead(int runAheadCount, bool useSecondary)
	{
		runAheadContext.RunAhead(runAheadCount, useSecondary);
	}
}

void DeinitHook()
{
	RemoveDestroyHook();
	runAheadContext.Destroy();
	DestroySecondary();
	current_core.retro_deinit();
}

void UnloadHook()
{
	RemoveDestroyHook();
	runAheadContext.Destroy();
	DestroySecondary();
	current_core.retro_unload_game();
}
