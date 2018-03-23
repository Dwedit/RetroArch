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

bool RunFrameSecondary();
bool DeserializeSecondary(void *buffer, int size);
void DestroySecondary();

extern "C"
{
	extern struct retro_callbacks retro_ctx;
	extern struct retro_core_t current_core;
}

#if HAVE_DYNAMIC
extern "C"
{
	extern bool InputIsDirty;
}
#endif

typedef bool(*LoadStateFunction)(const void*, size_t);

function_t originalRetroDeinit = NULL;
function_t originalRetroUnload = NULL;
function_t originalRetroReset = NULL;
LoadStateFunction originalRetroDeserialize = NULL;

void DeinitHook();
void UnloadHook();
void ResetHook();
bool LoadStateHook(const void *buf, size_t size);

void AddHooks()
{
	if (originalRetroDeinit == NULL)
	{
		originalRetroDeinit = current_core.retro_deinit;
		current_core.retro_deinit = DeinitHook;
	}
	if (originalRetroUnload == NULL)
	{
		originalRetroUnload = current_core.retro_unload_game;
		current_core.retro_unload_game = UnloadHook;
	}
	if (originalRetroReset == NULL)
	{
		originalRetroReset = current_core.retro_reset;
		current_core.retro_reset = ResetHook;
	}
	if (originalRetroDeserialize == NULL)
	{
		originalRetroDeserialize = current_core.retro_unserialize;
		current_core.retro_unserialize = LoadStateHook;
	}
}

void RemoveHooks()
{
	if (originalRetroDeinit != NULL)
	{
		current_core.retro_deinit = originalRetroDeinit;
		originalRetroDeinit = NULL;
	}
	if (originalRetroUnload != NULL)
	{
		current_core.retro_unload_game = originalRetroUnload;
		originalRetroUnload = NULL;
	}
	if (originalRetroReset != NULL)
	{
		current_core.retro_reset = originalRetroReset;
		originalRetroReset = NULL;
	}
	if (originalRetroDeserialize != NULL)
	{
		current_core.retro_unserialize = originalRetroDeserialize;
		originalRetroDeserialize = NULL;
	}
}

class RunAheadContext
{
	size_t saveStateSize;
	vector<byte> saveStateData;
	retro_ctx_serialize_info_t serial_info;
	bool videoDriverIsActive;
	bool runAheadAvailable;
	bool secondaryCoreAvailable;
	uint64_t lastFrameCount;

public:
	bool forceInputDirty;
	RunAheadContext()
	{
		saveStateSize = 0xFFFFFFFF;
		serial_info.data = NULL;
		serial_info.data_const = NULL;
		serial_info.size = 0;
		videoDriverIsActive = true;
		runAheadAvailable = true;
		secondaryCoreAvailable = true;
		forceInputDirty = true;
		lastFrameCount = 0;
	}

	void RunAhead(int runAheadCount, bool useSecondary)
	{
		bool okay;
		if (runAheadCount <= 0 || !runAheadAvailable)
		{
			core_run();
			forceInputDirty = true;
			return;
		}

		if (saveStateSize == 0xFFFFFFFF)
		{
			if (!Create())
			{
				//runloop_msg_queue_push("RunAhead has been disabled because the core does not support savestates", 1, 180, true);
				core_run();
				forceInputDirty = true;
				return;
			}
		}

		//Hack: If we were in the GUI changing any settings, force a resync.
		uint64_t frameCount;
		bool isAlive, isFocused;
		video_driver_get_status(&frameCount, &isAlive, &isFocused);
		if (frameCount != lastFrameCount + 1)
		{
			forceInputDirty = true;
		}
		lastFrameCount = frameCount;

		int runAhead = runAheadCount;
		int frameNumber = 0;
		
		if (!useSecondary || !HAVE_DYNAMIC || !secondaryCoreAvailable)
		{
			forceInputDirty = true;
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
					if (!SaveState())
					{
						//runloop_msg_queue_push("RunAhead has been disabled due to save state failure", 1, 180, true);
						return;
					}
				}
				if (lastFrame)
				{
					if (!LoadState())
					{
						//runloop_msg_queue_push("RunAhead has been disabled due to load state failure", 1, 180, true);
						return;
					}
				}
			}
		}
		else
		{
#if HAVE_DYNAMIC
			//run main core with video suspended
			SuspendVideo();
			core_run();
			ResumeVideo();

			bool inputDirty = InputIsDirty || forceInputDirty;

			if (inputDirty)
			{
				InputIsDirty = false;
				if (!SaveState())
				{
					return;
				}
				if (!LoadStateSecondary())
				{
					//runloop_msg_queue_push("Could not create a secondary core. RunAhead will only use the main core now.", 1, 180, true);
					return;
				}
				for (int frameCount = 0; frameCount < runAheadCount - 1; frameCount++)
				{
					SuspendVideo();
					SuspendAudio();
					okay = RunSecondary();
					ResumeAudio();
					ResumeVideo();
					if (!okay)
					{
						//runloop_msg_queue_push("Could not create a secondary core. RunAhead will only use the main core now.", 1, 180, true);
						return;
					}
				}
			}
			SuspendAudio();
			okay = RunSecondary();
			ResumeAudio();
			if (!okay)
			{
				//runloop_msg_queue_push("Could not create a secondary core. RunAhead will only use the main core now.", 1, 180, true);
				return;
			}
#endif
		}
		forceInputDirty = false;
	}

	void RunAheadError()
	{
		runAheadAvailable = false;

		RemoveHooks();

		saveStateSize = 0;
		saveStateData.clear();
		serial_info.data = NULL;
		serial_info.data_const = NULL;
		serial_info.size = 0;
	}

	bool Create()
	{
		//get savestate size and allocate buffer
		retro_ctx_size_info_t info;
		core_serialize_size(&info);

		saveStateSize = info.size;
		saveStateData.clear();
		saveStateData.resize(saveStateSize);

		//prevent assert errors when accessing address of element 0 for a 0 size vector
		if (saveStateData.size() > 0)
		{
			serial_info.data = &saveStateData[0];
			serial_info.data_const = &saveStateData[0];
			serial_info.size = saveStateData.size();
		}
		else
		{
			serial_info.data = NULL;
			serial_info.data_const = NULL;
			serial_info.size = 0;
		}


		videoDriverIsActive = video_driver_is_active();

		if (saveStateSize == 0)
		{
			RunAheadError();
			return false;
		}

		AddHooks();

		forceInputDirty = true;
		return true;
	}
	bool SaveState()
	{
		bool okay = core_serialize(&serial_info);
		if (!okay)
		{
			RunAheadError();
		}
		return okay;
	}
	bool LoadState()
	{
		bool okay = core_unserialize(&serial_info);
		if (!okay)
		{
			RunAheadError();
		}
		return okay;
	}
	bool LoadStateSecondary()
	{
		bool okay = true;
		if (saveStateData.size() == 0)
		{
			okay = false;
		}
		okay = okay && DeserializeSecondary(&saveStateData[0], (int)saveStateData.size());
		if (!okay)
		{
			secondaryCoreAvailable = false;
		}
		return okay;
	}
	bool RunSecondary()
	{
		bool okay = RunFrameSecondary();
		if (!okay)
		{
			secondaryCoreAvailable = false;
		}
		return okay;
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
		serial_info.size = 0;

		runAheadAvailable = true;
		secondaryCoreAvailable = true;
	}
};

RunAheadContext runAheadContext;

extern "C"
{
	void RunAhead(int runAheadCount, bool useSecondary)
	{
		runAheadContext.RunAhead(runAheadCount, useSecondary);
	}
	void RunAhead_Destroy()
	{
		runAheadContext.Destroy();
	}
}

void DeinitHook()
{
	RemoveHooks();
	runAheadContext.Destroy();
	DestroySecondary();
	if (current_core.retro_deinit)
	{
		current_core.retro_deinit();
	}
}

void UnloadHook()
{
	RemoveHooks();
	runAheadContext.Destroy();
	DestroySecondary();
	if (current_core.retro_unload_game)
	{
		current_core.retro_unload_game();
	}
}

void ResetHook()
{
	runAheadContext.forceInputDirty = true;
	if (originalRetroReset)
	{
		originalRetroReset();
	}
}

bool LoadStateHook(const void *buf, size_t size)
{
	runAheadContext.forceInputDirty = true;
	if (originalRetroDeserialize)
	{
		return originalRetroDeserialize(buf, size);
	}
	return false;
}
