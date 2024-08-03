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
#include "../load_check.h"

#include "../safeguards.h"

static void Load_DBGL()
{
	size_t length = SlGetFieldLength();
	if (length) {
		_loadgame_DBGL_data.resize(length);
		ReadBuffer::GetCurrent()->CopyBytes(reinterpret_cast<uint8_t *>(_loadgame_DBGL_data.data()), length);
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
		ReadBuffer::GetCurrent()->CopyBytes(reinterpret_cast<uint8_t *>(_load_check_data.debug_log_data.data()), length);
	}
}

static void Load_DBGC()
{
	size_t length = SlGetFieldLength();
	if (length) {
		_loadgame_DBGC_data.resize(length);
		ReadBuffer::GetCurrent()->CopyBytes(reinterpret_cast<uint8_t *>(_loadgame_DBGC_data.data()), length);
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
		ReadBuffer::GetCurrent()->CopyBytes(reinterpret_cast<uint8_t *>(_load_check_data.debug_config_data.data()), length);
	}
}

static void Save_DBGD()
{
	std::vector<NamedSaveLoad> nsl;
	if (_save_DBGC_data) {
		extern std::string _config_file_text;
		nsl.push_back(NSLT("config", SLEG_SSTR(_config_file_text, SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)));
	}
	if (_savegame_DBGL_data != nullptr) {
		nsl.push_back(NSLT("log", SLEG_STR(_savegame_DBGL_data, SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)));
	}
	SlSaveTableObjectChunk(nsl);
}

static void Load_DBGD()
{
	if (!SlIsTableChunk()) {
		SlSkipChunkContents();
		return;
	}

	static const NamedSaveLoad nsl[] = {
		NSLT("config", SLEG_SSTR(_loadgame_DBGC_data, SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
		NSLT("log",    SLEG_SSTR(_loadgame_DBGL_data, SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
	};
	SlLoadTableOrRiffFiltered(nsl);
}

static void Check_DBGD()
{
	if (!SlIsTableChunk() || !_load_check_data.want_debug_data) {
		SlSkipChunkContents();
		return;
	}

	static const NamedSaveLoad nsl[] = {
		NSLT("config", SLEG_SSTR(_load_check_data.debug_config_data, SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
		NSLT("log",    SLEG_SSTR(_load_check_data.debug_log_data, SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
	};
	SlLoadTableOrRiffFiltered(nsl);
}

extern const ChunkHandler debug_chunk_handlers[] = {
	{ 'DBGL',   nullptr, Load_DBGL, nullptr, Check_DBGL, CH_READONLY },
	{ 'DBGC',   nullptr, Load_DBGC, nullptr, Check_DBGC, CH_READONLY },
	{ 'DBGD', Save_DBGD, Load_DBGD, nullptr, Check_DBGD, CH_TABLE },
};

extern const ChunkHandlerTable _debug_chunk_handlers(debug_chunk_handlers);
