/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file engine_sl.cpp Code handling saving and loading of engines */

#include "../stdafx.h"
#include "saveload_buffer.h"
#include "saveload_internal.h"
#include "../debug.h"
#include "../engine_base.h"
#include "../engine_func.h"
#include "../newgrf_callbacks.h"
#include "../string_func.h"
#include "../network/network.h"
#include <vector>

#include "../safeguards.h"

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

void Save_ERNC()
{
	assert(_sl_xv_feature_versions[XSLFI_ERNC_CHUNK] != 0);

	if (!IsNetworkServerSave()) {
		SlSetLength(0);
		return;
	}

	uint32_t count = 0;
	std::span<uint8_t> result = SlSaveToTempBuffer([&]() {
		for (const Engine *e : Engine::Iterate()) {
			if (HasBit(e->info.callback_mask, CBM_VEHICLE_CUSTOM_REFIT)) {
				count++;
				SlWriteUint16(e->index);
				SlWriteUint64(e->info.refit_mask);
			}
		}
	});

	SlSetLength(4 + (uint)result.size());
	SlWriteUint32(count);
	MemoryDumper::GetCurrent()->CopyBytes(result);
}

struct EngineRefitNetworkCache {
	EngineID id;
	CargoTypes refit_mask;
};
static std::vector<EngineRefitNetworkCache> _engine_refit_network_caches;

void Load_ERNC()
{
	if (SlGetFieldLength() == 0) return;

	if (!_networking || _network_server) {
		SlSkipBytes(SlGetFieldLength());
		return;
	}

	const uint32_t count = SlReadUint32();
	_engine_refit_network_caches.reserve(count);
	for (uint32_t idx = 0; idx < count; idx++) {
		EngineID id = SlReadUint16();
		CargoTypes refit_mask = SlReadUint64();
		_engine_refit_network_caches.emplace_back(id, refit_mask);
	}
}

void SlResetERNC()
{
	_engine_refit_network_caches.clear();
}

void SlProcessERNC()
{
	for (const EngineRefitNetworkCache &it : _engine_refit_network_caches) {
		Engine *e = Engine::GetIfValid(it.id);
		if (e == nullptr) continue;
		if (e->info.refit_mask != it.refit_mask) {
			format_buffer buffer;
			buffer.format("[load]: engine cache mismatch: engine: {}, refit mask: {:X} != {:X}", it.id, e->info.refit_mask, it.refit_mask);
			debug_print(DebugLevelID::desync, 0, buffer);
			LogDesyncMsg(buffer.to_string());

			e->info.refit_mask = it.refit_mask;
		}
	}

	_engine_refit_network_caches.clear();
	_engine_refit_network_caches.shrink_to_fit();
}

static ChunkSaveLoadSpecialOpResult Special_ERNC(uint32_t chunk_id, ChunkSaveLoadSpecialOp op)
{
	switch (op) {
		case CSLSO_SHOULD_SAVE_CHUNK:
			if (_sl_xv_feature_versions[XSLFI_ERNC_CHUNK] == 0) return CSLSOR_DONT_SAVE_CHUNK;
			break;

		default:
			break;
	}
	return CSLSOR_NONE;
}

static const ChunkHandler engine_chunk_handlers[] = {
	MakeUpstreamChunkHandler<'EIDS', GeneralUpstreamChunkLoadInfo>(),
	MakeUpstreamChunkHandler<'ENGN', GeneralUpstreamChunkLoadInfo>(),
	{ 'ENGS', nullptr,   Load_ENGS, nullptr, nullptr, CH_READONLY  },
	{ 'ERNC', Save_ERNC, Load_ERNC, nullptr, nullptr, CH_RIFF, Special_ERNC },
};

extern const ChunkHandlerTable _engine_chunk_handlers(engine_chunk_handlers);
