/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict_sl.cpp Code handling saving and loading of trace restrict programs */

#include "../stdafx.h"
#include "../tracerestrict.h"
#include "saveload.h"
#include <vector>
#include "saveload.h"

static const SaveLoad _trace_restrict_mapping_desc[] = {
  SLE_VAR(TraceRestrictMappingItem, program_id, SLE_UINT32),
  SLE_END()
};

static void Load_TRRM()
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		TraceRestrictMappingItem &item = _tracerestrictprogram_mapping[index];
		SlObject(&item, _trace_restrict_mapping_desc);
	}
}

static void Save_TRRM()
{
	for (TraceRestrictMapping::iterator iter = _tracerestrictprogram_mapping.begin();
			iter != _tracerestrictprogram_mapping.end(); ++iter) {
		SlSetArrayIndex(iter->first);
		SlObject(&(iter->second), _trace_restrict_mapping_desc);
	}
}

struct TraceRestrictProgramStub {
	uint32 length;
};

static const SaveLoad _trace_restrict_program_stub_desc[] = {
	SLE_VAR(TraceRestrictProgramStub, length, SLE_UINT32),
	SLE_END()
};

static void Load_TRRP()
{
	int index;
	TraceRestrictProgramStub stub;
	while ((index = SlIterateArray()) != -1) {
		TraceRestrictProgram *prog = new (index) TraceRestrictProgram();
		SlObject(&stub, _trace_restrict_program_stub_desc);
		prog->items.resize(stub.length);
		SlArray(&(prog->items[0]), stub.length, SLE_UINT32);
		assert(prog->Validate().Succeeded());
	}
}

static void RealSave_TRRP(TraceRestrictProgram *prog)
{
	TraceRestrictProgramStub stub;
	stub.length = prog->items.size();
	SlObject(&stub, _trace_restrict_program_stub_desc);
	SlArray(&(prog->items[0]), stub.length, SLE_UINT32);
}

static void Save_TRRP()
{
	TraceRestrictProgram *prog;

	FOR_ALL_TRACE_RESTRICT_PROGRAMS(prog) {
		SlSetArrayIndex(prog->index);
		SlAutolength((AutolengthProc*) RealSave_TRRP, prog);
	}
}

void AfterLoadTraceRestrict()
{
	for (TraceRestrictMapping::iterator iter = _tracerestrictprogram_mapping.begin();
			iter != _tracerestrictprogram_mapping.end(); ++iter) {
		_tracerestrictprogram_pool.Get(iter->second.program_id)->IncrementRefCount();
	}
}

extern const ChunkHandler _trace_restrict_chunk_handlers[] = {
	{ 'TRRM', Save_TRRM, Load_TRRM, NULL, NULL, CH_SPARSE_ARRAY},
	{ 'TRRP', Save_TRRP, Load_TRRP, NULL, NULL, CH_ARRAY | CH_LAST},
};
