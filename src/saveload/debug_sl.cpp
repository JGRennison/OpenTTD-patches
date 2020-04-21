/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file debug_sl.cpp Code handling saving and loading of debugging information */

#include "../stdafx.h"

#include "../debug.h"
#include "saveload.h"
#include "saveload_buffer.h"
#include "../fios.h"

#include "../safeguards.h"

static void Save_DBGL()
{
	if (_savegame_DBGL_data != nullptr) {
		size_t length = strlen(_savegame_DBGL_data);
		SlSetLength(length);
		MemoryDumper::GetCurrent()->CopyBytes(reinterpret_cast<const byte *>(_savegame_DBGL_data), length);
	} else {
		SlSetLength(0);
	}
}

static void Load_DBGL()
{
	size_t length = SlGetFieldLength();
	if (length) {
		_loadgame_DBGL_data.resize(length);
		ReadBuffer::GetCurrent()->CopyBytes(reinterpret_cast<byte *>(const_cast<char *>(_loadgame_DBGL_data.data())), length);
	}
}

static void Check_DBGL()
{
	if (!_load_check_data.want_debug_data) {
		SlSkipBytes(SlGetFieldLength());
		return;
	}
	size_t length = SlGetFieldLength();
	if (length) {
		_load_check_data.debug_log_data.resize(length);
		ReadBuffer::GetCurrent()->CopyBytes(reinterpret_cast<byte *>(const_cast<char *>(_load_check_data.debug_log_data.data())), length);
	}
}

static void Save_DBGC()
{
	extern std::string _config_file_text;
	const char header[] = "*** openttd.cfg start ***\n";
	const char footer[] = "*** openttd.cfg end ***\n";
	if (_save_DBGC_data) {
		SlSetLength(lengthof(header) + _config_file_text.size() + lengthof(footer) - 2);
		MemoryDumper::GetCurrent()->CopyBytes(reinterpret_cast<const byte *>(header), lengthof(header) - 1);
		MemoryDumper::GetCurrent()->CopyBytes(reinterpret_cast<const byte *>(_config_file_text.data()), _config_file_text.size());
		MemoryDumper::GetCurrent()->CopyBytes(reinterpret_cast<const byte *>(footer), lengthof(footer) - 1);
	} else {
		SlSetLength(0);
	}
}

static void Load_DBGC()
{
	size_t length = SlGetFieldLength();
	if (length) {
		_loadgame_DBGC_data.resize(length);
		ReadBuffer::GetCurrent()->CopyBytes(reinterpret_cast<byte *>(const_cast<char *>(_loadgame_DBGC_data.data())), length);
	}
}

static void Check_DBGC()
{
	if (!_load_check_data.want_debug_data) {
		SlSkipBytes(SlGetFieldLength());
		return;
	}
	size_t length = SlGetFieldLength();
	if (length) {
		_load_check_data.debug_config_data.resize(length);
		ReadBuffer::GetCurrent()->CopyBytes(reinterpret_cast<byte *>(const_cast<char *>(_load_check_data.debug_config_data.data())), length);
	}
}

extern const ChunkHandler _debug_chunk_handlers[] = {
	{ 'DBGL', Save_DBGL, Load_DBGL, nullptr, Check_DBGL, CH_RIFF},
	{ 'DBGC', Save_DBGC, Load_DBGC, nullptr, Check_DBGC, CH_RIFF | CH_LAST},
};
