#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <boolean.h>

#include "dirty_input.h"
#include "mylist.h"
#include "secondary_core.h"
#include "run_ahead.h"

#include "../core.h"
#include "../dynamic.h"
#include "../audio/audio_driver.h"
#include "../gfx/video_driver.h"
#include "../configuration.h"
#include "../retroarch.h"

#include "MemoryStream.c"

static bool runahead_create(void);
static bool runahead_save_state(int slotNumber);
static bool runahead_load_state(int slotNumber);
static bool runahead_load_state_secondary(int slotNumber);
static bool runahead_run_secondary(void);
static void runahead_suspend_audio(void);
static void runahead_resume_audio(void);
static void runahead_suspend_video(void);
static void runahead_resume_video(void);
static void set_fast_savestate(void);
static void unset_fast_savestate(void);
static void set_hard_disable_audio(void);
static void unset_hard_disable_audio(void);
static bool runahead_save_state_list_move_to_front(int slotNumber);

static bool debug_runahead_save_state(int slotNumber);
static bool debug_runahead_load_state(int slotNumber);
static bool debug_runahead_save_state_secondary(int slotNumber);
static bool debug_runahead_load_state_secondary(int slotNumber);
static void debug_runahead_set_capture_video(int slotNumber);
static void debug_runahead_set_capture_audio(int slotNumber);
static void debug_runahead_replay_video(int slotNumber);
static void debug_runahead_replay_audio(int slotNumber);
static void debug_runahead_destroy();
static bool debug_runahead_swap_capture(int slot1, int slot2);
static bool debug_runahead_swap_save_state(int slot1, int slot2);
static void debug_runahead_set_capture(int slotNumber);
static void debug_runahead_replay(int slotNumber);

static bool debug_runahead_compare_state(int slot1, int slot2);
static bool debug_runahead_compare_capture(int slot1, int slot2);

static void debug_runahead_copy_sram_if_needed(void);

static MyList *debug_runahead_video_capture_list;
static MyList *debug_runahead_audio_capture_list;

typedef struct tagRASurface
{
   void *data;
   unsigned int width, height;
   size_t pitch;
   size_t size;
} RASurface;

static void* RASurface_constructor(void);
static void RASurface_destructor(void*);
static bool RASurface_equals(RASurface *surfaceA, RASurface *surfaceB);
static void RASurface_assign(RASurface *destSurface, RASurface *srcSurface);

static int core_serialize_compare(const void* data1, const void* data2, size_t size);

/*
typedef struct tagRAMem
{
   void *data;
   size_t size;
} RAMem;

static void* RAMem_constructor(void);
static void RAMem_destructor(void*);
static bool RAMem_equals(RAMem *memA, RAMem *memB);
*/




static bool core_run_use_last_input(void);

static size_t runahead_save_state_size = 0;
static bool runahead_save_state_size_known = false;

static bool runahead_have_copied_sram = false;

/* Save State List for Run Ahead */
static MyList *runahead_save_state_list;
static MyList *debug_runahead_save_state_list;

#ifdef WIN32
#ifdef _MSC_VER
#define BREAKPOINT() if (IsDebuggerPresent()) { __debugbreak(); }
#else
#define BREAKPOINT() if (IsDebuggerPresent()) { DebugBreak(); }
#endif
#else
#define BREAKPOINT()
#endif


static void *runahead_save_state_alloc(void)
{
   retro_ctx_serialize_info_t *savestate = (retro_ctx_serialize_info_t*)
      malloc(sizeof(retro_ctx_serialize_info_t));

   if (!savestate)
      return NULL;

   savestate->data          = NULL;
   savestate->data_const    = NULL;
   savestate->size          = 0;

   if (runahead_save_state_size > 0 && runahead_save_state_size_known)
   {
      savestate->data       = malloc(runahead_save_state_size);
      savestate->data_const = savestate->data;
      savestate->size       = runahead_save_state_size;
   }

   return savestate;
}

static void runahead_save_state_free(void *state)
{
   retro_ctx_serialize_info_t *savestate = (retro_ctx_serialize_info_t*)state;
   if (!savestate)
      return;
   free(savestate->data);
   free(savestate);
}

static void runahead_save_state_list_init(size_t saveStateSize)
{
   runahead_save_state_size = saveStateSize;
   runahead_save_state_size_known = true;
   mylist_create(&runahead_save_state_list, 16, runahead_save_state_alloc, runahead_save_state_free);
   mylist_create(&debug_runahead_save_state_list, 16, runahead_save_state_alloc, runahead_save_state_free);
}

static void runahead_save_state_list_destroy(void)
{
   mylist_destroy(&runahead_save_state_list);
   mylist_destroy(&debug_runahead_save_state_list);
}

static bool runahead_save_state_list_move_to_front(int slotNumber)
{
   int i;
   void *element;

   if (runahead_save_state_list == NULL || runahead_save_state_list->data == NULL) return false;
   if (slotNumber <= 0) return false;
   if (slotNumber >= runahead_save_state_list->size) return false;

   element = runahead_save_state_list->data[slotNumber];
   for (i = slotNumber; i > 0; i--)
   {
      runahead_save_state_list->data[i] = runahead_save_state_list->data[i - 1];
   }
   runahead_save_state_list->data[0] = element;
   return true;
}

/* Hooks - Hooks to cleanup, and add dirty input hooks */

static function_t originalRetroDeinit = NULL;
static function_t originalRetroUnload = NULL;

extern struct retro_core_t current_core;
extern struct retro_callbacks retro_ctx;

static void remove_hooks(void)
{
   if (originalRetroDeinit)
   {
      current_core.retro_deinit = originalRetroDeinit;
      originalRetroDeinit       = NULL;
   }

   if (originalRetroUnload)
   {
      current_core.retro_unload_game = originalRetroUnload;
      originalRetroUnload            = NULL;
   }
   remove_input_state_hook();
}

static void unload_hook(void)
{
   remove_hooks();
   runahead_destroy();
   secondary_core_destroy();
   if (current_core.retro_unload_game)
      current_core.retro_unload_game();
}


static void deinit_hook(void)
{
   remove_hooks();
   runahead_destroy();
   secondary_core_destroy();
   if (current_core.retro_deinit)
      current_core.retro_deinit();
}

static void add_hooks(void)
{
   if (!originalRetroDeinit)
   {
      originalRetroDeinit       = current_core.retro_deinit;
      current_core.retro_deinit = deinit_hook;
   }

   if (!originalRetroUnload)
   {
      originalRetroUnload = current_core.retro_unload_game;
      current_core.retro_unload_game = unload_hook;
   }
   add_input_state_hook();
}


/* Runahead Code */

static bool runahead_video_driver_is_active   = true;
static bool runahead_available                = true;
static bool runahead_secondary_core_available = true;
static bool runahead_force_input_dirty        = true;
static uint64_t runahead_last_frame_count     = 0;

static void runahead_clear_variables(void)
{
   runahead_save_state_size          = 0;
   runahead_save_state_size_known    = false;
   runahead_video_driver_is_active   = true;
   runahead_available                = true;
   runahead_secondary_core_available = true;
   runahead_force_input_dirty        = true;
   runahead_last_frame_count         = 0;
   runahead_have_copied_sram = false;
}

static uint64_t runahead_get_frame_count()
{
   bool is_alive, is_focused = false;
   uint64_t frame_count = 0;
   video_driver_get_status(&frame_count, &is_alive, &is_focused);
   return frame_count;
}


static void runahead_check_for_gui(void)
{
   /* Hack: If we were in the GUI, force a resync. */
   uint64_t frame_count = runahead_get_frame_count();

   if (frame_count != runahead_last_frame_count + 1)
      runahead_force_input_dirty = true;

   runahead_last_frame_count = frame_count;
}

void run_ahead(int runahead_count, bool useSecondary)
{
   int frame_number        = 0;
   bool last_frame         = false;
   bool suspended_frame    = false;
#if defined(HAVE_DYNAMIC) || defined(HAVE_DYLIB)
   const bool have_dynamic = true;
#else
   const bool have_dynamic = false;
#endif
   settings_t *settings = config_get_ptr();
   if (runahead_count < 0 || !runahead_available)
   {
      core_run();
      runahead_force_input_dirty = true;
      return;
   }

   if (!runahead_save_state_size_known)
   {
      if (!runahead_create())
      {
         if (!settings->bools.run_ahead_hide_warnings)
         {
            runloop_msg_queue_push(msg_hash_to_str(MSG_RUNAHEAD_CORE_DOES_NOT_SUPPORT_SAVESTATES), 0, 2 * 60, true);
         }
         core_run();
         runahead_force_input_dirty = true;
         return;
      }
   }

   runahead_check_for_gui();

   if (settings->bools.run_ahead_debug_mode)
   {
      if (!useSecondary)
      {
         /*
         Modes:
         single instance:
           "runahead 0":
             run  (no capturing)
             debug save 0
             save, load
             debug save 1
             compare debug savestate 0 and debug savestate 1
           "runahead 1+":
             save
             run (capture 0), replay capture 0
             debug save 0
             if "runahead 2+"
               swap Capture 3 and Capture 2
               swap debug saves 3 and debug save 2
               run (capture 2)
               debug save 2
             load
             run (capture 1)
             debug save 1
             compare debug savestates 0, 1
             compare capture 0, 1
             compare debug savestates 0, 3 (if it exists)
             compare capture 0, 3 (if it exists)
         two instance:
            "runahead 0":
              save
              load
              run (capture 0)
              replay capture 0
              debug save 0
              secondary run (capture 1)
              secondary deubug save 1
              compare debug savestates 0, 1
              compare capture 0, 1
            "runahead 1":
              run (capture 0)
              replay capture 0
              debug save 0
              save
              if "runahead 2+"
                swap capture 3, capture 2
                swap debug save 3, debug save 2
                run (capture 2)
                debug save 2
              load
              secondary run (capture 1)
              secondary debug save 1
              compare debug savestates 0,1
              compare capture 0,1
              compare debug savestates 0,3 (if it exists)
              compare capture 0,3 (if it exists)
         */

         if (runahead_count == 0)
         {
            core_run();
            debug_runahead_save_state(0);
            debug_runahead_load_state(0);
            debug_runahead_save_state(1);
            debug_runahead_compare_state(0, 1);
         }
         else
         {
            runahead_save_state(0);
            debug_runahead_set_capture(0);
            core_run();
            debug_runahead_save_state(0);
            debug_runahead_replay(0);
            if (runahead_count >= 2)
            {
               if (debug_runahead_save_state_list->size < 3)
               {
                  mylist_resize(debug_runahead_save_state_list, 3, true);
                  mylist_resize(debug_runahead_audio_capture_list, 3, true);
                  mylist_resize(debug_runahead_video_capture_list, 3, true);
               }
               else
               {
                  if (debug_runahead_save_state_list->size < 4)
                  {
                     mylist_resize(debug_runahead_save_state_list, 4, true);
                     mylist_resize(debug_runahead_audio_capture_list, 4, true);
                     mylist_resize(debug_runahead_video_capture_list, 4, true);
                  }
                  debug_runahead_swap_capture(3, 2);
                  debug_runahead_swap_save_state(3, 2);
               }
               debug_runahead_set_capture(2);
               core_run_use_last_input();
               debug_runahead_save_state(2);
            }
            runahead_load_state(0);
            debug_runahead_set_capture(1);
            core_run_use_last_input();
            debug_runahead_set_capture(-1);
            debug_runahead_save_state(1);
            debug_runahead_compare_state(0, 1);
            debug_runahead_compare_capture(0, 1);
            if (!input_is_dirty)
            {
               debug_runahead_compare_state(0, 3);
               debug_runahead_compare_capture(0, 3);
            }
         }
         /*


         bool useLastInput = false;
         if (runahead_count > 0)
         {
            runahead_save_state(0);

            //runahead_suspend_audio(); runahead_suspend_video();
            debug_runahead_set_capture(0);
            core_run();
            debug_runahead_set_capture(-1);
            //runahead_resume_audio(); runahead_resume_video();
            useLastInput = true;


            debug_runahead_save_state(0);


         }

         debug_runahead_set_capture_video(0);
         debug_runahead_set_capture_audio(0);
         if (!useLastInput) core_run(); else core_run_use_last_input();
         debug_runahead_set_capture_video(-1);
         debug_runahead_set_capture_audio(-1);
         debug_runahead_replay_audio(0);
         debug_runahead_replay_video(0);



         if (runahead_count > 0)
         {
            runahead_load_state(0);
         }
         


         if (DELETEME)
         {
            runahead_save_state(0);
            debug_runahead_set_capture_audio(0);
            debug_runahead_set_capture_video(0);
            core_run_use_last_input();
            debug_runahead_set_capture_audio(-1);
            debug_runahead_set_capture_video(-1);
            debug_runahead_replay_video(0);
            debug_runahead_replay_audio(0);
            runahead_load_state(0);
         }
         else
         {
            debug_runahead_set_capture_audio(0);
            debug_runahead_set_capture_video(0);
            core_run();
            debug_runahead_set_capture_audio(-1);
            debug_runahead_set_capture_video(-1);
            debug_runahead_replay_video(0);
            debug_runahead_replay_audio(0);
            runahead_save_state(0);
            runahead_load_state(0);
         }
         debug_runahead_save_state_2();
         */
      }
      else
      {
         secondary_core_ensure_exists();
         debug_runahead_copy_sram_if_needed();

         if (runahead_count == 0)
         {
            runahead_save_state(0);
            runahead_load_state(0);
            debug_runahead_set_capture(0);
            core_run();
            debug_runahead_replay(0);
            debug_runahead_save_state(0);
            debug_runahead_set_capture(1);
            secondary_core_run_use_last_input();
            debug_runahead_set_capture(-1);
            debug_runahead_save_state_secondary(1);
            debug_runahead_compare_state(0, 1);
            debug_runahead_compare_capture(0, 1);
         }
         else
         {
            debug_runahead_set_capture(0);
            core_run();
            debug_runahead_replay(0);
            debug_runahead_save_state(0);
            if (runahead_count >= 2)
            {
               if (debug_runahead_save_state_list->size < 3)
               {
                  mylist_resize(debug_runahead_save_state_list, 3, true);
                  mylist_resize(debug_runahead_audio_capture_list, 3, true);
                  mylist_resize(debug_runahead_video_capture_list, 3, true);
               }
               else
               {
                  if (debug_runahead_save_state_list->size < 4)
                  {
                     mylist_resize(debug_runahead_save_state_list, 4, true);
                     mylist_resize(debug_runahead_audio_capture_list, 4, true);
                     mylist_resize(debug_runahead_video_capture_list, 4, true);
                  }
                  debug_runahead_swap_capture(3, 2);
                  debug_runahead_swap_save_state(3, 2);
               }
               debug_runahead_set_capture(2);
               core_run_use_last_input();
               debug_runahead_save_state(2);
            }
            debug_runahead_load_state(0);
            debug_runahead_set_capture(1);
            secondary_core_run_use_last_input();
            debug_runahead_set_capture(-1);
            debug_runahead_save_state_secondary(1);
            debug_runahead_compare_state(0, 1);
            debug_runahead_compare_capture(0, 1);
            if (!input_is_dirty)
            {
               debug_runahead_compare_state(0, 3);
               debug_runahead_compare_capture(0, 3);
            }
         }



         //if (runahead_count > 0) { runahead_suspend_audio(); runahead_suspend_video(); }
         //core_run();
         //if (runahead_count > 0) { runahead_resume_audio(); runahead_resume_video(); }

         //runahead_save_state(0);
         //if (runahead_count > 0)
         //{
         //   core_run_use_last_input();
         //}
         //runahead_load_state(0);

         //runahead_suspend_audio();
         //runahead_suspend_video();
         //runahead_run_secondary();
         //runahead_resume_audio();
         //runahead_resume_video();
         //debug_runahead_save_state_secondary(0);
      }

      //retro_ctx_serialize_info_t *mem1 = runahead_save_state_list->data[0];
      //retro_ctx_serialize_info_t *mem2 = runahead_save_state_list->data[1];
      //if (0 != memcmp(mem1->data, mem2->data, runahead_save_state_size))
      //{
      //   //BREAKPOINT();
      //   for (int offset = 0; offset < runahead_save_state_size; offset++)
      //   {
      //      if (((char*)mem1->data)[offset] != ((char*)(mem2->data))[offset])
      //      {
      //         int dummy = 0;
      //      }
      //   }
      //   int dummy2 = 0;
      //}
      input_is_dirty = false;
      return;
   }
   if (!useSecondary || !have_dynamic || !runahead_secondary_core_available)
   {
      /* TODO: multiple savestates for higher performance 
       * when not using secondary core */
      for (frame_number = 0; frame_number <= runahead_count; frame_number++)
      {
         last_frame      = frame_number == runahead_count;
         suspended_frame = !last_frame;

         if (suspended_frame)
         {
            runahead_suspend_audio();
            runahead_suspend_video();
         }

         if (frame_number == 0)
            core_run();
         else
            core_run_use_last_input();

         if (suspended_frame)
         {
            runahead_resume_video();
            runahead_resume_audio();
         }

         if (frame_number == 0)
         {
            if (!runahead_save_state(0))
            {
               runloop_msg_queue_push(msg_hash_to_str(MSG_RUNAHEAD_FAILED_TO_SAVE_STATE), 0, 3 * 60, true);
               return;
            }
         }

         if (last_frame)
         {
            if (!runahead_load_state(0))
            {
               runloop_msg_queue_push(msg_hash_to_str(MSG_RUNAHEAD_FAILED_TO_LOAD_STATE), 0, 3 * 60, true);
               return;
            }
         }
      }
   }
   else
   {
#if HAVE_DYNAMIC
      if (!secondary_core_ensure_exists())
      {
         runahead_secondary_core_available = false;
         runloop_msg_queue_push(msg_hash_to_str(MSG_RUNAHEAD_FAILED_TO_CREATE_SECONDARY_INSTANCE), 0, 3 * 60, true);
         core_run();
         runahead_force_input_dirty = true;
         return;
      }

      /* run main core with video suspended */
      runahead_suspend_video();
      core_run();
      runahead_resume_video();

      if (input_is_dirty || runahead_force_input_dirty)
      {
         input_is_dirty       = false;

         if (!runahead_save_state(0))
         {
            runloop_msg_queue_push(msg_hash_to_str(MSG_RUNAHEAD_FAILED_TO_SAVE_STATE), 0, 3 * 60, true);
            return;
         }

         if (!runahead_load_state_secondary(0))
         {
            runloop_msg_queue_push(msg_hash_to_str(MSG_RUNAHEAD_FAILED_TO_LOAD_STATE), 0, 3 * 60, true);
            return;
         }

         for (frame_number = 0; frame_number < runahead_count - 1; frame_number++)
         {
            runahead_suspend_video();
            runahead_suspend_audio();
            set_hard_disable_audio();
            runahead_run_secondary();
            unset_hard_disable_audio();
            runahead_resume_audio();
            runahead_resume_video();
         }
      }
      runahead_suspend_audio();
      set_hard_disable_audio();
      runahead_run_secondary();
      unset_hard_disable_audio();
      runahead_resume_audio();
#endif
   }
   runahead_force_input_dirty = false;
}

static void runahead_error(void)
{
   runahead_available = false;
   runahead_save_state_list_destroy();
   remove_hooks();
   runahead_save_state_size = 0;
   runahead_save_state_size_known = true;
}

static bool runahead_create(void)
{
   /* get savestate size and allocate buffer */
   retro_ctx_size_info_t info;
   set_fast_savestate();
   core_serialize_size(&info);
   unset_fast_savestate();

   runahead_save_state_list_init(info.size);
   runahead_video_driver_is_active = video_driver_is_active();

   if (runahead_save_state_size == 0 || !runahead_save_state_size_known)
   {
      runahead_error();
      return false;
   }

   add_hooks();
   runahead_force_input_dirty = true;
   //mylist_resize(runahead_save_state_list, 1, true);
   mylist_resize(runahead_save_state_list, 2, true);
   runahead_have_copied_sram = false;
   return true;
}

static bool runahead_save_state(int slotNumber)
{
   bool okay = false;
   retro_ctx_serialize_info_t *serialize_info;
   if (runahead_save_state_list == NULL || runahead_save_state_list->data == NULL) return false;
   if (slotNumber < 0)
   if (slotNumber >= runahead_save_state_list->size)
   {
      mylist_resize(runahead_save_state_list, slotNumber + 1, true);
   }
   serialize_info = (retro_ctx_serialize_info_t*)runahead_save_state_list->data[slotNumber];

   set_fast_savestate();
   okay = core_serialize(serialize_info);
   unset_fast_savestate();
   if (!okay)
   {
      runahead_error();
      return false;
   }
   return true;
}

static bool debug_runahead_save_state(int slotNumber)
{
   bool okay = false;
   retro_ctx_serialize_info_t *serialize_info;
   if (debug_runahead_save_state_list == NULL || debug_runahead_save_state_list->data == NULL) return false;
   if (slotNumber < 0) return false;
   if (slotNumber >= debug_runahead_save_state_list->size)
   {
      mylist_resize(debug_runahead_save_state_list, slotNumber + 1, true);
   }
   serialize_info = (retro_ctx_serialize_info_t*)debug_runahead_save_state_list->data[slotNumber];
   set_fast_savestate();
   okay = core_serialize(serialize_info);
   unset_fast_savestate();
   if (!okay)
   {
      runahead_error();
      return false;
   }
   return true;
}

static bool runahead_load_state(int slotNumber)
{
   bool okay;
   retro_ctx_serialize_info_t *serialize_info;
   bool last_dirty;

   if (runahead_save_state_list == NULL || runahead_save_state_list->data == NULL) return false;
   if (slotNumber < 0 || slotNumber >= runahead_save_state_list->size) return false;

   serialize_info = (retro_ctx_serialize_info_t*)runahead_save_state_list->data[slotNumber];
   if (serialize_info == NULL || serialize_info->data_const == NULL) return false;

   last_dirty = input_is_dirty;
   set_fast_savestate();
   /* calling core_unserialize has side effects with
   * netplay (it triggers transmitting your save state)
   call retro_unserialize directly from the core instead */
   okay = current_core.retro_unserialize(serialize_info->data_const, serialize_info->size);
   unset_fast_savestate();
   input_is_dirty = last_dirty;

   if (!okay)
      runahead_error();

   return okay;
}

static bool debug_runahead_load_state(int slotNumber)
{
   bool okay;
   retro_ctx_serialize_info_t *serialize_info;
   bool last_dirty;

   if (debug_runahead_save_state_list == NULL || debug_runahead_save_state_list->data == NULL) return false;
   if (slotNumber < 0 || slotNumber >= debug_runahead_save_state_list->size) return false;

   serialize_info = (retro_ctx_serialize_info_t*)debug_runahead_save_state_list->data[slotNumber];
   if (serialize_info == NULL || serialize_info->data_const == NULL) return false;

   last_dirty = input_is_dirty;
   set_fast_savestate();
   /* calling core_unserialize has side effects with
   * netplay (it triggers transmitting your save state)
   call retro_unserialize directly from the core instead */
   okay = current_core.retro_unserialize(serialize_info->data_const, serialize_info->size);
   unset_fast_savestate();
   input_is_dirty = last_dirty;

   if (!okay)
      runahead_error();

   return okay;
}

static bool debug_runahead_save_state_secondary(int slotNumber)
{
   bool okay = false;
   retro_ctx_serialize_info_t *serialize_info;
   if (debug_runahead_save_state_list == NULL || debug_runahead_save_state_list->data == NULL) return false;
   if (slotNumber < 0) return false;
   if (slotNumber >= debug_runahead_save_state_list->size)
   {
      mylist_resize(debug_runahead_save_state_list, slotNumber + 1, true);
   }
   serialize_info = (retro_ctx_serialize_info_t*)debug_runahead_save_state_list->data[slotNumber];
   set_fast_savestate();
   okay = secondary_core_serialize(serialize_info->data, serialize_info->size);
   unset_fast_savestate();
   if (!okay)
   {
      runahead_error();
      return false;
   }
   return true;
}

static bool debug_runahead_load_state_secondary(int slotNumber)
{
   bool okay;
   retro_ctx_serialize_info_t *serialize_info;
   bool last_dirty;

   if (debug_runahead_save_state_list == NULL || debug_runahead_save_state_list->data == NULL) return false;
   if (slotNumber < 0 || slotNumber >= debug_runahead_save_state_list->size) return false;
   serialize_info = (retro_ctx_serialize_info_t*)debug_runahead_save_state_list->data[slotNumber];
   set_fast_savestate();
   okay = secondary_core_deserialize(serialize_info->data_const, (int)serialize_info->size);
   unset_fast_savestate();

   if (!okay)
   {
      runahead_secondary_core_available = false;
      runahead_error();
      return false;
   }

   return true;
}


static bool runahead_load_state_secondary(int slotNumber)
{
   bool okay;
   retro_ctx_serialize_info_t *serialize_info;
   bool last_dirty;

   if (runahead_save_state_list == NULL || runahead_save_state_list->data == NULL) return false;
   if (slotNumber < 0 || slotNumber >= runahead_save_state_list->size) return false;
   serialize_info = (retro_ctx_serialize_info_t*) runahead_save_state_list->data[slotNumber];
   if (serialize_info == NULL || serialize_info->data_const == NULL) return false;

   last_dirty = input_is_dirty;
   set_fast_savestate();
   okay = secondary_core_deserialize(
         serialize_info->data_const, (int)serialize_info->size);
   unset_fast_savestate();
   input_is_dirty = last_dirty;

   if (!okay)
   {
      runahead_secondary_core_available = false;
      runahead_error();
      return false;
   }

   return true;
}

static bool runahead_run_secondary(void)
{
   if (!secondary_core_run_use_last_input())
   {
      runahead_secondary_core_available = false;
      return false;
   }
   return true;
}

static void runahead_suspend_audio(void)
{
   audio_driver_suspend();
}

static void runahead_resume_audio(void)
{
   audio_driver_resume();
}

static void runahead_suspend_video(void)
{
   video_driver_unset_active();
}

static void runahead_resume_video(void)
{
   if (runahead_video_driver_is_active)
      video_driver_set_active();
   else
      video_driver_unset_active();
}

void runahead_destroy(void)
{
   runahead_save_state_list_destroy();
   debug_runahead_destroy();
   remove_hooks();
   runahead_clear_variables();
}

static bool request_fast_savestate;
static bool hard_disable_audio;


bool want_fast_savestate(void)
{
   return request_fast_savestate;
}

static void set_fast_savestate(void)
{
   request_fast_savestate = true;
}

static void unset_fast_savestate(void)
{
   request_fast_savestate = false;
}

bool get_hard_disable_audio(void)
{
   return hard_disable_audio;
}

static void set_hard_disable_audio(void)
{
   hard_disable_audio = true;
}

static void unset_hard_disable_audio(void)
{
   hard_disable_audio = false;
}

static void runahead_input_poll_null(void)
{
}

static bool core_run_use_last_input(void)
{
   extern struct retro_callbacks retro_ctx;
   extern struct retro_core_t current_core;

   retro_input_poll_t old_poll_function = retro_ctx.poll_cb;
   retro_input_state_t old_input_function = retro_ctx.state_cb;

   retro_ctx.poll_cb = runahead_input_poll_null;
   retro_ctx.state_cb = input_state_get_last;

   current_core.retro_set_input_poll(retro_ctx.poll_cb);
   current_core.retro_set_input_state(retro_ctx.state_cb);

   current_core.retro_run();

   retro_ctx.poll_cb = old_poll_function;
   retro_ctx.state_cb = old_input_function;

   current_core.retro_set_input_poll(retro_ctx.poll_cb);
   current_core.retro_set_input_state(retro_ctx.state_cb);

   return true;
}

static int debug_runahead_current_video_slot;
static int debug_runahead_current_audio_slot;
static retro_video_refresh_t debug_runahead_last_video_callback;
static retro_video_refresh_t debug_runahead_last_video_callback_secondary;
static retro_audio_sample_batch_t debug_runahead_last_audio_callback;
static retro_audio_sample_batch_t debug_runahead_last_audio_callback_secondary;

static void RETRO_CALLCONV debug_runahead_capture_video_callback(const void *data, unsigned width, unsigned height, size_t pitch)
{
   RASurface *surface = NULL;
   RASurface srcSurface;
   srcSurface.data = data;
   srcSurface.width = width;
   srcSurface.height = height;
   srcSurface.pitch = pitch;
   srcSurface.size = srcSurface.pitch * srcSurface.height;

   if (debug_runahead_video_capture_list == NULL)
   {
      mylist_create(&debug_runahead_video_capture_list, 16, RASurface_constructor, RASurface_destructor);
   }
   if (debug_runahead_current_video_slot >= debug_runahead_video_capture_list->size)
   {
      mylist_resize(debug_runahead_video_capture_list, debug_runahead_current_video_slot + 1, true);
   }
   surface = (RASurface*)debug_runahead_video_capture_list->data[debug_runahead_current_video_slot];
   if (surface->size != srcSurface.size)
   {
      free(surface->data);
      surface->size = srcSurface.size;
      surface->height = srcSurface.height;
      surface->width = srcSurface.width;
      surface->pitch = srcSurface.pitch;
      surface->data = malloc(surface->size);
   }
   RASurface_assign(surface, &srcSurface);
}

static size_t RETRO_CALLCONV debug_runahead_capture_audio_callback(const int16_t *data, size_t frames)
{
   IMemoryStream *mem = NULL;
   size_t srcSize = sizeof(int16_t) * 2 * frames;

   if (debug_runahead_audio_capture_list == NULL)
   {
      mylist_create(&debug_runahead_audio_capture_list, 16, CMemoryStream_constructor_for_mylist, CMemoryStream_destructor_for_mylist);
   }
   if (debug_runahead_current_audio_slot >= debug_runahead_audio_capture_list->size)
   {
      mylist_resize(debug_runahead_audio_capture_list, debug_runahead_current_audio_slot + 1, true);
   }
   if (srcSize == 0) return 0;
   if (data[0] != 0)
   {
      int dummy = 0;
   }
   mem = (IMemoryStream*)debug_runahead_audio_capture_list->data[debug_runahead_current_audio_slot];
   mem->vtable->Write(mem, (const unsigned char*)data, srcSize);
   return frames;
}

static void debug_runahead_set_capture_video(int slotNumber)
{
   debug_runahead_current_video_slot = slotNumber;
   if (slotNumber < 0)
   {
      if (debug_runahead_last_video_callback != NULL && retro_ctx.frame_cb == debug_runahead_capture_video_callback)
      {
         retro_ctx.frame_cb = debug_runahead_last_video_callback;
         current_core.retro_set_video_refresh(retro_ctx.frame_cb);
         debug_runahead_last_video_callback = NULL;
      }
      if (secondary_module != NULL && debug_runahead_last_video_callback_secondary != NULL && secondary_callbacks.frame_cb == debug_runahead_capture_video_callback)
      {
         secondary_callbacks.frame_cb = debug_runahead_last_video_callback_secondary;
         secondary_core.retro_set_video_refresh(secondary_callbacks.frame_cb);
         debug_runahead_last_video_callback_secondary = NULL;
      }
   }
   else
   {
      if (debug_runahead_last_video_callback == NULL && retro_ctx.frame_cb != debug_runahead_capture_video_callback)
      {
         debug_runahead_last_video_callback = retro_ctx.frame_cb;
         retro_ctx.frame_cb = debug_runahead_capture_video_callback;
         current_core.retro_set_video_refresh(retro_ctx.frame_cb);
      }
      if (secondary_module != NULL && debug_runahead_last_video_callback_secondary == NULL && secondary_callbacks.frame_cb != debug_runahead_capture_video_callback)
      {
         debug_runahead_last_video_callback_secondary = secondary_callbacks.frame_cb;
         secondary_callbacks.frame_cb = debug_runahead_capture_video_callback;
         secondary_core.retro_set_video_refresh(secondary_callbacks.frame_cb);
      }
   }
}

static void debug_runahead_set_capture_audio(int slotNumber)
{
   debug_runahead_current_audio_slot = slotNumber;
   if (slotNumber < 0)
   {
      if (debug_runahead_last_audio_callback != NULL && retro_ctx.sample_batch_cb == debug_runahead_capture_audio_callback)
      {
         retro_ctx.sample_batch_cb = debug_runahead_last_audio_callback;
         current_core.retro_set_audio_sample_batch(retro_ctx.sample_batch_cb);
         debug_runahead_last_audio_callback = NULL;
      }
      if (secondary_module != NULL && debug_runahead_last_audio_callback_secondary != NULL && secondary_callbacks.sample_batch_cb == debug_runahead_capture_audio_callback)
      {
         secondary_callbacks.sample_batch_cb = debug_runahead_last_audio_callback_secondary;
         secondary_core.retro_set_audio_sample_batch(secondary_callbacks.sample_batch_cb);
         debug_runahead_last_audio_callback_secondary = NULL;
      }
   }
   else
   {
      if (debug_runahead_last_audio_callback == NULL && retro_ctx.sample_batch_cb != debug_runahead_capture_audio_callback)
      {
         debug_runahead_last_audio_callback = retro_ctx.sample_batch_cb;
         retro_ctx.sample_batch_cb = debug_runahead_capture_audio_callback;
         current_core.retro_set_audio_sample_batch(retro_ctx.sample_batch_cb);
      }
      if (secondary_module != NULL && debug_runahead_last_audio_callback_secondary == NULL && secondary_callbacks.sample_batch_cb != debug_runahead_capture_audio_callback)
      {
         debug_runahead_last_audio_callback_secondary = secondary_callbacks.sample_batch_cb;
         secondary_callbacks.sample_batch_cb = debug_runahead_capture_audio_callback;
         secondary_core.retro_set_audio_sample_batch(secondary_callbacks.sample_batch_cb);
      }
      /* clear the buffer */
      debug_runahead_capture_audio_callback(NULL, 0);
      IMemoryStream *stream = (IMemoryStream*)debug_runahead_audio_capture_list->data[debug_runahead_current_audio_slot];
      stream->vtable->SetPosition(stream, 0);
      stream->vtable->SetLength(stream, 0);
   }
}

static void debug_runahead_replay_video(int slotNumber)
{
   RASurface *surface = NULL;
   if (debug_runahead_video_capture_list == NULL) return;
   if (debug_runahead_video_capture_list->data == NULL) return;
   if (slotNumber < 0) return;
   if (slotNumber >= debug_runahead_video_capture_list->size) return;
   surface = (RASurface*)debug_runahead_video_capture_list->data[slotNumber];
   if (surface == NULL) return;
   retro_ctx.frame_cb(surface->data, surface->width, surface->height, surface->pitch);
}

static void debug_runahead_replay_audio(int slotNumber)
{
   IMemoryStream *mem = NULL;
   if (debug_runahead_audio_capture_list == NULL) return;
   if (debug_runahead_audio_capture_list->data == NULL) return;
   if (slotNumber < 0) return;
   if (slotNumber >= debug_runahead_audio_capture_list->size) return;
   mem = (IMemoryStream*)debug_runahead_audio_capture_list->data[slotNumber];
   if (mem == NULL) return;
   retro_ctx.sample_batch_cb((const int16_t*)mem->vtable->GetBuffer(mem), mem->vtable->GetLength(mem)/2/sizeof(int16_t));
}

static void* RASurface_constructor(void)
{
   RASurface *surface = (RASurface*)calloc(1, sizeof(RASurface));
   return surface;
}
static void RASurface_destructor(void* surface_p)
{
   RASurface *surface = (RASurface*)surface_p;
   if (surface == NULL) return;
   free(surface->data);
   free(surface);
}

static bool RASurface_equals(RASurface *surfaceA, RASurface *surfaceB)
{
   if (surfaceA->width != surfaceB->width) return false;
   if (surfaceA->height != surfaceB->height) return false;
   if (surfaceA->pitch != surfaceB->pitch) return false;
   if (surfaceA->data == NULL || surfaceB->data == NULL) return false;
   int y, h;
   h = surfaceA->height;
   int bytepp = surfaceA->pitch / surfaceA->width;
   int stride2 = bytepp * surfaceA->width;

   for (y = 0; y < h; y++)
   {
      const void *surfaceALine = (const unsigned char*)surfaceA->data + surfaceA->pitch * y;
      const void *surfaceBLine = (const unsigned char*)surfaceB->data + surfaceB->pitch * y;
      if (0 != memcmp(surfaceALine, surfaceBLine, stride2))
      {
         return false;
      }
   }
   return true;
}
static void RASurface_assign(RASurface *destSurface, RASurface *srcSurface)
{
   if (destSurface->width != srcSurface->width) return;
   if (destSurface->height != srcSurface->height) return;
   if (destSurface->pitch != srcSurface->pitch) return;
   if (destSurface->data == NULL || srcSurface->data == NULL) return;
   int y, h;
   h = destSurface->height;
   int bytepp = destSurface->pitch / destSurface->width;
   int stride2 = bytepp * destSurface->width;

   for (y = 0; y < h; y++)
   {
      void *destLine = (unsigned char*)destSurface->data + destSurface->pitch * y;
      const void *srcLine = (const unsigned char*)srcSurface->data + srcSurface->pitch * y;

      memcpy(destLine, srcLine, stride2);
   }
}

static void debug_runahead_destroy()
{
   debug_runahead_set_capture_video(-1);
   debug_runahead_set_capture_audio(-1);

   mylist_destroy(&debug_runahead_video_capture_list);
   mylist_destroy(&debug_runahead_audio_capture_list);
   mylist_destroy(&debug_runahead_save_state_list);

   debug_runahead_last_video_callback = NULL;
   debug_runahead_last_audio_callback = NULL;
   debug_runahead_last_video_callback_secondary = NULL;
   debug_runahead_last_audio_callback_secondary = NULL;
}

static bool debug_runahead_swap_capture(int slot1, int slot2)
{
   RASurface *tempSurface;
   IMemoryStream *tempAudio;
   if (debug_runahead_video_capture_list == NULL || debug_runahead_video_capture_list->data == NULL) return false;
   if (debug_runahead_audio_capture_list == NULL || debug_runahead_audio_capture_list->data == NULL) return false;
   if (slot1 < 0 || slot1 >= debug_runahead_video_capture_list->size || slot1 >= debug_runahead_audio_capture_list->size) return false;
   if (slot2 < 0 || slot2 >= debug_runahead_video_capture_list->size || slot2 >= debug_runahead_audio_capture_list->size) return false;

   tempSurface = (RASurface*) debug_runahead_video_capture_list->data[slot1];
   tempAudio = (IMemoryStream*) debug_runahead_audio_capture_list->data[slot1];
   debug_runahead_video_capture_list->data[slot1] = debug_runahead_video_capture_list->data[slot2];
   debug_runahead_audio_capture_list->data[slot1] = debug_runahead_audio_capture_list->data[slot2];
   debug_runahead_video_capture_list->data[slot2] = tempSurface;
   debug_runahead_audio_capture_list->data[slot2] = tempAudio;
   return true;
}

static bool debug_runahead_swap_save_state(int slot1, int slot2)
{
   void *temp;
   if (debug_runahead_save_state_list == NULL || debug_runahead_save_state_list->data == NULL) return false;
   if (slot1 < 0 || slot1 >= debug_runahead_save_state_list->size ||
       slot2 < 0 || slot2 >= debug_runahead_save_state_list->size) return false;

   temp = debug_runahead_save_state_list->data[slot1];
   debug_runahead_save_state_list->data[slot1] = debug_runahead_save_state_list->data[slot2];
   debug_runahead_save_state_list->data[slot2] = temp;
   return true;
}


static void debug_runahead_set_capture(int slotNumber)
{
   debug_runahead_set_capture_audio(slotNumber);
   debug_runahead_set_capture_video(slotNumber);
}

static void debug_runahead_replay(int slotNumber)
{
   debug_runahead_set_capture(-1);
   debug_runahead_replay_audio(slotNumber);
   debug_runahead_replay_video(slotNumber);
}

static bool debug_runahead_compare_state(int slot1, int slot2)
{
   bool isEqual = true;
   retro_ctx_serialize_info_t *state1, *state2;
      if (debug_runahead_save_state_list == NULL || debug_runahead_save_state_list->data == NULL) return false;
   if (slot1 < 0 || slot2 < 0 ||
      slot1 >= debug_runahead_save_state_list->size ||
      slot2 >= debug_runahead_save_state_list->size) return false;
   state1 = (retro_ctx_serialize_info_t*)debug_runahead_save_state_list->data[slot1];
   state2 = (retro_ctx_serialize_info_t*)debug_runahead_save_state_list->data[slot2];
   /* compare them */
   if (0 != memcmp(state1->data, state2->data, state1->size))
   {
      //for (int i = 0; i < state1->size; i++)
      //{
      //   if (((uint8_t*)state1->data)[i] != ((uint8_t*)state2->data)[i])
      //   {
      //      int dummy = 0;
      //   }
      //}
      core_serialize_compare(state1->data, state2->data, state1->size);
      isEqual = false;
   }
   return isEqual;
}

static bool debug_runahead_compare_capture(int slot1, int slot2)
{
   bool isEqual = true;
   IMemoryStream *audio1, *audio2;
   RASurface *video1, *video2;
   if (debug_runahead_audio_capture_list == NULL || debug_runahead_audio_capture_list->data == NULL || 
      debug_runahead_video_capture_list == NULL || debug_runahead_video_capture_list->data == NULL) return false;
   if (slot1 < 0 || slot2 < 0 ||
      slot1 >= debug_runahead_audio_capture_list->size ||
      slot2 >= debug_runahead_audio_capture_list->size ||
      slot1 >= debug_runahead_video_capture_list->size ||
      slot2 >= debug_runahead_video_capture_list->size) return false;
   audio1 = (IMemoryStream*)debug_runahead_audio_capture_list->data[slot1];
   audio2 = (IMemoryStream*)debug_runahead_audio_capture_list->data[slot2];
   video1 = (RASurface*)debug_runahead_video_capture_list->data[slot1];
   video2 = (RASurface*)debug_runahead_video_capture_list->data[slot2];
   /* compare them */

   if (audio1->vtable->GetLength(audio1) != audio2->vtable->GetLength(audio2) || 0 != memcmp(audio1->vtable->GetBuffer(audio1), audio2->vtable->GetBuffer(audio2), audio1->vtable->GetLength(audio1)))
   {
      isEqual = false;
   }
   if (!RASurface_equals(video1, video2))
   {
      isEqual = false;
   }
   return isEqual;
}

static void debug_runahead_copy_sram_if_needed(void)
{
   if (!runahead_have_copied_sram)
   {
      retro_ctx_memory_info_t mem_ctx;
      runahead_have_copied_sram = true;
      void *sram2 = secondary_core_get_sram_ptr();
      mem_ctx.id = RETRO_MEMORY_SAVE_RAM;
      mem_ctx.data = NULL;
      mem_ctx.size = 0;
      core_get_memory(&mem_ctx);

      if (sram2 != NULL && mem_ctx.data != NULL)
      {
         memcpy(sram2, mem_ctx.data, mem_ctx.size);
      }
   }

}

//   mylist_destroy(&debug_runahead_save_state_list);

#include <dynamic/dylib.h>

typedef int(*retro_serialize_compare_func)(const void* data1, const void* data2, size_t size);

static int core_serialize_compare(const void* data1, const void* data2, size_t size)
{
   extern dylib_t lib_handle;
   function_t func = dylib_proc(lib_handle, "retro_serialize_compare");
   if (func != NULL)
   {
      frontend_driver_attach_console();
      retro_serialize_compare_func func2 = (retro_serialize_compare_func)func;
      func2(data1, data2, size);
   }
   return 0;
}
