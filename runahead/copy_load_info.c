#include "libretro.h"
#include "mem_util.h"
#include "core.h"
#include <string.h>
#include <malloc.h>
#include "lists/string_list.h"

retro_ctx_load_content_info_t *load_content_info;
enum rarch_core_type last_core_type;

static void free_retro_game_info(struct retro_game_info *dest)
{
	if (dest == NULL) return;
	FREE(dest->path);
	FREE(dest->data);
	FREE(dest->meta);
}

static struct retro_game_info* clone_retro_game_info(const struct retro_game_info *src)
{
	struct retro_game_info *dest;
	if (src == NULL) return NULL;
	dest = (struct retro_game_info*)malloc_zero(sizeof(struct retro_game_info));
	dest->path = strcpy_alloc(src->path);
	dest->data = memcpy_alloc(src->data, src->size);
	dest->size = src->size;
	dest->meta = strcpy_alloc(src->meta);
	return dest;
}

static void free_string_list(struct string_list *dest)
{
	int i;
	if (dest == NULL) return;
	for (i = 0; i < dest->size; i++)
	{
		FREE(dest->elems[i].data);
	}
	FREE(dest->elems);
}

static struct string_list* clone_string_list(const struct string_list *src)
{
	int i;
	struct string_list *dest;
	if (src == NULL) return NULL;
	dest = (struct string_list*)malloc_zero(sizeof(struct string_list));
	dest->size = src->size;
	dest->cap = src->cap;
	dest->elems = (struct string_list_elem*)malloc_zero(sizeof(struct string_list_elem) * dest->size);
	for (i = 0; i < src->size; i++)
	{
		dest->elems[i].data = strcpy_alloc(src->elems[i].data);
		dest->elems[i].attr = src->elems[i].attr;
	}
	return dest;
}

#if 0
/* for cloning the Special field, however, attempting to use this feature crashes retroarch */
static void free_retro_subsystem_memory_info(struct retro_subsystem_memory_info *dest)
{
   if (dest == NULL) return;
   FREE(dest->extension);
}

static void clone_retro_subsystem_memory_info(struct retro_subsystem_memory_info* dest, const struct retro_subsystem_memory_info *src)
{
   dest->extension = strcpy_alloc(src->extension);
   dest->type = src->type;
}

static void free_retro_subsystem_rom_info(struct retro_subsystem_rom_info *dest)
{
   int i;
   if (dest == NULL) return;
   FREE(dest->desc);
   FREE(dest->valid_extensions);
   for (i = 0; i < dest->num_memory; i++)
   {
      free_retro_subsystem_memory_info((struct retro_subsystem_memory_info*)&dest->memory[i]);
   }
   FREE(dest->memory);
}

static void clone_retro_subsystem_rom_info(struct retro_subsystem_rom_info *dest, const struct retro_subsystem_rom_info *src)
{
   int i;
   retro_subsystem_memory_info *memory;
   dest->need_fullpath = src->need_fullpath;
   dest->block_extract = src->block_extract;
   dest->required = src->required;
   dest->num_memory = src->num_memory;
   dest->desc = strcpy_alloc(src->desc);
   dest->valid_extensions = strcpy_alloc(src->valid_extensions);
   memory = (struct retro_subsystem_memory_info*)malloc_zero(dest->num_memory * sizeof(struct retro_subsystem_memory_info));
   dest->memory = memory;
   for (i = 0; i < dest->num_memory; i++)
   {
      clone_retro_subsystem_memory_info(&memory[i], &src->memory[i]);
   }
}

static void free_retro_subsystem_info(struct retro_subsystem_info *dest)
{
   int i;
   if (dest == NULL) return;
   FREE(dest->desc);
   FREE(dest->ident);
   for (i = 0; i < dest->num_roms; i++)
   {
      free_retro_subsystem_rom_info((struct retro_subsystem_rom_info*)&dest->roms[i]);
   }
   FREE(dest->roms);
}

static retro_subsystem_info* clone_retro_subsystem_info(struct const retro_subsystem_info *src)
{
   int i;
   retro_subsystem_info *dest;
   retro_subsystem_rom_info *roms;
   if (src == NULL) return NULL;
   dest = (struct retro_subsystem_info*)malloc_zero(sizeof(struct retro_subsystem_info));
   dest->desc = strcpy_alloc(src->desc);
   dest->ident = strcpy_alloc(src->ident);
   dest->num_roms = src->num_roms;
   dest->id = src->id;

   roms = (struct retro_subsystem_rom_info*)malloc_zero(src->num_roms * sizeof(struct retro_subsystem_rom_info));
   dest->roms = roms;
   for (i = 0; i < src->num_roms; i++)
   {
      clone_retro_subsystem_rom_info(&roms[i], &src->roms[i]);
   }
   return dest;
}
#endif

static void free_retro_ctx_load_content_info(struct retro_ctx_load_content_info *dest)
{
	if (dest == NULL) return;
	free_retro_game_info(dest->info);
	free_string_list((struct string_list*)dest->content);
	FREE(dest->info);
	FREE(dest->content);
#if 0
   free_retro_subsystem_info((retro_subsystem_info*)dest->special);
   FREE(dest->special);
#endif
}

static struct retro_ctx_load_content_info* clone_retro_ctx_load_content_info(const struct retro_ctx_load_content_info *src)
{
	struct retro_ctx_load_content_info *dest;
	if (src == NULL || src->special != NULL) return NULL;	/* refuse to deal with the Special field */

	dest = (struct retro_ctx_load_content_info*)malloc_zero(sizeof(struct retro_ctx_load_content_info));
	dest->info = clone_retro_game_info(src->info);
	dest->content = clone_string_list(src->content);
	dest->special = NULL;
#if 0
   dest->special = clone_retro_subsystem_info(src->special);
#endif
	return dest;
}

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
