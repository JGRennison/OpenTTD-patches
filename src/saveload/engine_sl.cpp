/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file engine_sl.cpp Code handling saving and loading of engines */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/engine_sl_compat.h"

#include "../engine_base.h"
#include "../engine_override.h"
#include "../string_func.h"
#include <vector>

#include "../safeguards.h"

Engine *GetTempDataEngine(EngineID index);

namespace upstream_sl {

static const SaveLoad _engine_desc[] = {
	 SLE_CONDVAR(Engine, intro_date,          SLE_FILE_U16 | SLE_VAR_I32,  SL_MIN_VERSION,  SLV_31),
	 SLE_CONDVAR(Engine, intro_date,          SLE_INT32,                  SLV_31, SL_MAX_VERSION),
	 SLE_CONDVAR(Engine, age,                 SLE_FILE_U16 | SLE_VAR_I32,  SL_MIN_VERSION,  SLV_31),
	 SLE_CONDVAR(Engine, age,                 SLE_INT32,                  SLV_31, SL_MAX_VERSION),
	     SLE_VAR(Engine, reliability,         SLE_UINT16),
	     SLE_VAR(Engine, reliability_spd_dec, SLE_UINT16),
	     SLE_VAR(Engine, reliability_start,   SLE_UINT16),
	     SLE_VAR(Engine, reliability_max,     SLE_UINT16),
	     SLE_VAR(Engine, reliability_final,   SLE_UINT16),
	     SLE_VAR(Engine, duration_phase_1,    SLE_UINT16),
	     SLE_VAR(Engine, duration_phase_2,    SLE_UINT16),
	     SLE_VAR(Engine, duration_phase_3,    SLE_UINT16),
	     SLE_VAR(Engine, flags,               SLE_UINT8),
	 SLE_CONDVAR(Engine, preview_asked,       SLE_UINT16,                SLV_179, SL_MAX_VERSION),
	 SLE_CONDVAR(Engine, preview_company,     SLE_UINT8,                 SLV_179, SL_MAX_VERSION),
	     SLE_VAR(Engine, preview_wait,        SLE_UINT8),
	 SLE_CONDVAR(Engine, company_avail,       SLE_FILE_U8  | SLE_VAR_U16,  SL_MIN_VERSION, SLV_104),
	 SLE_CONDVAR(Engine, company_avail,       SLE_UINT16,                SLV_104, SL_MAX_VERSION),
	 SLE_CONDVAR(Engine, company_hidden,      SLE_UINT16,                SLV_193, SL_MAX_VERSION),
	 SLE_CONDSTR(Engine, name,                SLE_STR, 0,                SLV_84, SL_MAX_VERSION),
};

struct ENGNChunkHandler : ChunkHandler {
	ENGNChunkHandler() : ChunkHandler('ENGN', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_engine_desc);

		for (Engine *e : Engine::Iterate()) {
			SlSetArrayIndex(e->index);
			SlObject(e, _engine_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_engine_desc, _engine_sl_compat);

		/* As engine data is loaded before engines are initialized we need to load
		 * this information into a temporary array. This is then copied into the
		 * engine pool after processing NewGRFs by CopyTempEngineData(). */
		int index;
		while ((index = SlIterateArray()) != -1) {
			Engine *e = GetTempDataEngine(index);
			SlObject(e, slt);

			if (IsSavegameVersionBefore(SLV_179)) {
				/* preview_company_rank was replaced with preview_company and preview_asked.
				 * Just cancel any previews. */
				e->flags &= ~4; // ENGINE_OFFER_WINDOW_OPEN
				e->preview_company = INVALID_COMPANY;
				e->preview_asked = MAX_UVALUE(CompanyMask);
			}
		}
	}
};

/** Save and load the mapping between the engine id in the pool, and the grf file it came from. */
static const SaveLoad _engine_id_mapping_desc[] = {
	SLE_VAR(EngineIDMapping, grfid,         SLE_UINT32),
	SLE_VAR(EngineIDMapping, internal_id,   SLE_UINT16),
	SLE_VAR(EngineIDMapping, type,          SLE_UINT8),
	SLE_VAR(EngineIDMapping, substitute_id, SLE_UINT8),
};

struct EIDSChunkHandler : ChunkHandler {
	EIDSChunkHandler() : ChunkHandler('EIDS', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_engine_id_mapping_desc);

		uint index = 0;
		for (EngineIDMapping &eid : _engine_mngr) {
			SlSetArrayIndex(index);
			SlObject(&eid, _engine_id_mapping_desc);
			index++;
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_engine_id_mapping_desc, _engine_id_mapping_sl_compat);

		_engine_mngr.clear();

		while (SlIterateArray() != -1) {
			EngineIDMapping *eid = &_engine_mngr.emplace_back();
			SlObject(eid, slt);
		}
	}
};

static const EIDSChunkHandler EIDS;
static const ENGNChunkHandler ENGN;
static const ChunkHandlerRef engine_chunk_handlers[] = {
	EIDS,
	ENGN,
};

extern const ChunkHandlerTable _engine_chunk_handlers(engine_chunk_handlers);

}
