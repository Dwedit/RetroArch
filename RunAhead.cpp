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
extern bool InputIsDirty;
#endif

function_t originalRetroDeinit = NULL;
function_t originalRetroUnload = NULL;
function_t originalRetroReset = NULL;

void DeinitHook();
void UnloadHook();
void ResetHook();

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

void AddResetHook()
{
	if (originalRetroReset == NULL)
	{
		originalRetroReset = current_core.retro_reset;
		current_core.retro_reset = ResetHook;
	}
}

void RemoveResetHook()
{
	if (originalRetroReset != NULL)
	{
		current_core.retro_reset = originalRetroReset;
		originalRetroReset = NULL;
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
	}

	void RunAhead(int runAheadCount, bool useSecondary)
	{
		bool okay;
		if (runAheadCount <= 0 || !runAheadAvailable)
		{
			core_run();
			return;
		}

		if (saveStateSize == 0xFFFFFFFF)
		{
			if (!Create())
			{
				//runloop_msg_queue_push("RunAhead has been disabled because the core does not support savestates", 1, 180, true);
				core_run();
				return;
			}
		}

		int runAhead = runAheadCount;
		int frameNumber = 0;
		
		if (!useSecondary || !HAVE_DYNAMIC || !secondaryCoreAvailable)
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

		RemoveResetHook();
		RemoveDestroyHook();

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

		AddResetHook();
		AddDestroyHook();

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
	RemoveResetHook();
	RemoveDestroyHook();
	runAheadContext.Destroy();
	DestroySecondary();
	if (current_core.retro_deinit)
	{
		current_core.retro_deinit();
	}
}

void UnloadHook()
{
	RemoveResetHook();
	RemoveDestroyHook();
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
