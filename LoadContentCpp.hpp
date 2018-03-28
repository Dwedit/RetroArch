#pragma once
//This exists to allow Load Content to be called a second time using the same information
#include "core.h"
#include "lists/string_list.h"
#include <vector>
#include <string>
using std::vector;
using std::string;
typedef unsigned char byte;

struct RetroGameInfo : public retro_game_info
{
	string Path;
	vector<byte> Data;
	string Meta;

	RetroGameInfo()
	{
		Clear();
	}

	void Clear()
	{
		Path.clear();
		Data.clear();
		Meta.clear();
		path = NULL;
		data = NULL;
		size = 0;
		meta = NULL;
	}

	void Assign(const retro_game_info &info)
	{
		if (info.path != NULL)
		{
			Path = info.path;
			path = Path.c_str();
		}
		if (info.data != NULL && info.size > 0)
		{
			size = info.size;
			Data.resize(info.size);
			data = NULL;
			if (Data.size() > 0) data = &Data[0];
			memcpy(&Data[0], info.data, Data.size());
		}
		if (info.meta != NULL)
		{
			Meta = info.meta;
			meta = Meta.c_str();
		}
	}
};

struct StringListElem : public string_list_elem
{
	string Data;

	StringListElem()
	{
		data = NULL;
		attr.p = NULL;
	}

	void Assign(const string_list_elem &elem)
	{
		if (elem.data != NULL)
		{
			Data = elem.data;
			data = NULL;
			if (Data.size() > 0) data = &Data[0];
		}
		attr = elem.attr;
	}
};

struct StringList : public string_list
{
	vector<StringListElem> Elems;

	StringList()
	{
		Clear();
	}

	void Clear()
	{
		Elems.clear();
		elems = NULL;
		size = 0;
		cap = 0;
	}

	void Assign(const string_list &stringList)
	{
		if (stringList.size > 0)
		{
			Elems.resize(stringList.size);
			size = Elems.size();
			elems = NULL;
			if (Elems.size() > 0) elems = &Elems[0];
			for (int i = 0; i < size; i++)
			{
				Elems[i].Assign(stringList.elems[i]);
			}
		}
		cap = stringList.cap;
	}
};

struct RetroSubsystemMemoryInfo : public retro_subsystem_memory_info
{
	string Extension;
	RetroSubsystemMemoryInfo()
	{
		type = 0;
	}

	void Assign(const retro_subsystem_memory_info &info)
	{
		if (info.extension != NULL)
		{
			Extension = info.extension;
			extension = Extension.c_str();
		}
		type = info.type;
	}
};

struct RetroSubsystemRomInfo : public retro_subsystem_rom_info
{
	string Desc;
	string ValidExtensions;
	vector<RetroSubsystemMemoryInfo> Memory;

	RetroSubsystemRomInfo()
	{
		block_extract = false;
		desc = NULL;
		memory = NULL;
		need_fullpath = false;
		num_memory = 0;
		required = false;
		valid_extensions = NULL;
	}
	void Assign(const retro_subsystem_rom_info &info)
	{
		block_extract = info.block_extract;
		if (info.desc != NULL)
		{
			Desc = info.desc;
			desc = Desc.c_str();
		}
		if (info.memory != NULL && info.num_memory > 0)
		{
			Memory.resize(num_memory);
			memory = NULL;
			if (Memory.size() > 0) memory = &Memory[0];
			num_memory = Memory.size();
			for (int i = 0; i < num_memory; i++)
			{
				Memory[i].Assign(info.memory[i]);
			}
		}
		need_fullpath = info.need_fullpath;
		required = info.required;
		if (info.valid_extensions != NULL)
		{
			ValidExtensions = info.valid_extensions;
			valid_extensions = ValidExtensions.c_str();
		}
	}
};

struct RetroSubsystemInfo : public retro_subsystem_info
{
	string Desc;
	string Ident;
	vector<RetroSubsystemRomInfo> Roms;
	RetroSubsystemInfo()
	{
		Clear();
	}
	void Clear()
	{
		Desc.clear();
		Ident.clear();
		desc = NULL;
		id = 0;
		ident = NULL;
		num_roms = 0;
		roms = NULL;
	}

	void Assign(const retro_subsystem_info &info)
	{
		Clear();

		if (info.desc != NULL)
		{
			Desc = info.desc;
			desc = Desc.c_str();
		}
		this->id = info.id;
		if (info.ident != NULL)
		{
			Ident = info.ident;
			ident = Ident.c_str();
		}
		if (info.roms != NULL && info.num_roms > 0)
		{
			Roms.resize(info.num_roms);
			roms = NULL;
			if (Roms.size() > 0) roms = &Roms[0];
			num_roms = Roms.size();
			for (int i = 0; i < num_roms; i++)
			{
				Roms[i].Assign(info.roms[i]);
			}
		}
	}
};

struct RetroCtxLoadContentInfo : public retro_ctx_load_content_info
{
	RetroGameInfo Info;
	StringList Content;
	RetroSubsystemInfo Special;

	RetroCtxLoadContentInfo()
	{
		Clear();
	}

	void Clear()
	{
		Info.Clear();
		info = NULL;

		Content.Clear();
		content = NULL;

		Special.Clear();
		special = NULL;
	}

	void Assign(const retro_ctx_load_content_info &ctx)
	{
		Clear();

		if (ctx.info != NULL)
		{
			Info.Assign(*ctx.info);
			info = &Info;
		}
		//TESTING
		//this->content = ctx.content;
		//this->special = ctx.special;
		
		if (ctx.content != NULL)
		{
			Content.Assign(*ctx.content);
			content = &Content;
		}
		if (ctx.special != NULL)
		{
			Special.Assign(*ctx.special);
			special = &Special;
		}
		
	}
};
