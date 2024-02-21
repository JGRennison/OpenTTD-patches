/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file engine_sl.cpp Code handling saving and loading of engines */

#include "../stdafx.h"
#include "saveload_internal.h"
#include "../engine_base.h"
#include "../engine_func.h"
#include "../string_func.h"
#include <vector>

#include "../safeguards.h"

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

	SLE_CONDNULL(1,                                                        SL_MIN_VERSION, SLV_121),
	     SLE_VAR(Engine, flags,               SLE_UINT8),
	SLE_CONDNULL(1,                                                        SL_MIN_VERSION, SLV_179), // old preview_company_rank
	 SLE_CONDVAR(Engine, preview_asked,       SLE_UINT16,                SLV_179, SL_MAX_VERSION),
	 SLE_CONDVAR(Engine, preview_company,     SLE_UINT8,                 SLV_179, SL_MAX_VERSION),
	     SLE_VAR(Engine, preview_wait,        SLE_UINT8),
	SLE_CONDNULL(1,                                                        SL_MIN_VERSION,  SLV_45),
	 SLE_CONDVAR(Engine, company_avail,       SLE_FILE_U8  | SLE_VAR_U16,  SL_MIN_VERSION, SLV_104),
	 SLE_CONDVAR(Engine, company_avail,       SLE_UINT16,                SLV_104, SL_MAX_VERSION),
	 SLE_CONDVAR(Engine, company_hidden,      SLE_UINT16,                SLV_193, SL_MAX_VERSION),
	 SLE_CONDSTR(Engine, name,                SLE_STR, 0,                 SLV_84, SL_MAX_VERSION),

	SLE_CONDNULL(16,                                                       SLV_2, SLV_144), // old reserved space
};

static std::vector<Engine*> _temp_engine;

/**
 * Allocate an Engine structure, but not using the pools.
 * The allocated Engine must be freed using FreeEngine;
 * @return Allocated engine.
 */
static Engine* CallocEngine()
{
	uint8_t *zero = CallocT<uint8_t>(sizeof(Engine));
	Engine *engine = new (zero) Engine();
	return engine;
}

/**
 * Deallocate an Engine constructed by CallocEngine.
 * @param e Engine to free.
 */
static void FreeEngine(Engine *e)
{
	if (e != nullptr) {
		e->~Engine();
		free(e);
	}
}

Engine *GetTempDataEngine(EngineID index)
{
	if (index < _temp_engine.size()) {
		return _temp_engine[index];
	} else if (index == _temp_engine.size()) {
		_temp_engine.push_back(CallocEngine());
		return _temp_engine[index];
	} else {
		NOT_REACHED();
	}
}

/**
 * Copy data from temporary engine array into the real engine pool.
 */
void CopyTempEngineData()
{
	for (Engine *e : Engine::Iterate()) {
		if (e->index >= _temp_engine.size()) break;

		const Engine *se = GetTempDataEngine(e->index);
		e->intro_date          = se->intro_date;
		e->age                 = se->age;
		e->reliability         = se->reliability;
		e->reliability_spd_dec = se->reliability_spd_dec;
		e->reliability_start   = se->reliability_start;
		e->reliability_max     = se->reliability_max;
		e->reliability_final   = se->reliability_final;
		e->duration_phase_1    = se->duration_phase_1;
		e->duration_phase_2    = se->duration_phase_2;
		e->duration_phase_3    = se->duration_phase_3;
		e->flags               = se->flags;
		e->preview_asked       = se->preview_asked;
		e->preview_company     = se->preview_company;
		e->preview_wait        = se->preview_wait;
		e->company_avail       = se->company_avail;
		e->company_hidden      = se->company_hidden;
		e->name                = se->name;
	}

	ResetTempEngineData();
}

void ResetTempEngineData()
{
	/* Get rid of temporary data */
	for (std::vector<Engine*>::iterator it = _temp_engine.begin(); it != _temp_engine.end(); ++it) {
		FreeEngine(*it);
	}
	_temp_engine.clear();
}

static void Load_ENGS()
{
	/* Load old separate String ID list into a temporary array. This
	 * was always 256 entries. */
	StringID names[256];

	SlArray(names, lengthof(names), SLE_STRINGID);

	/* Copy each string into the temporary engine array. */
	for (EngineID engine = 0; engine < lengthof(names); engine++) {
		Engine *e = GetTempDataEngine(engine);
		e->name = CopyFromOldName(names[engine]);
	}
}

void AfterLoadEngines()
{
	AnalyseEngineCallbacks();
}

static const ChunkHandler engine_chunk_handlers[] = {
	MakeUpstreamChunkHandler<'EIDS', GeneralUpstreamChunkLoadInfo>(),
	MakeUpstreamChunkHandler<'ENGN', GeneralUpstreamChunkLoadInfo>(),
	{ 'ENGS', nullptr,   Load_ENGS, nullptr, nullptr, CH_RIFF  },
};

extern const ChunkHandlerTable _engine_chunk_handlers(engine_chunk_handlers);
