/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict_sl.cpp Code handling saving and loading of trace restrict programs */

#include "../stdafx.h"
#include "../tracerestrict.h"
#include "../strings_func.h"
#include "../string_func.h"
#include "saveload.h"
#include <vector>

static const SaveLoad _trace_restrict_mapping_desc[] = {
  SLE_VAR(TraceRestrictMappingItem, program_id, SLE_UINT32),
  SLE_END()
};

/**
 * Load mappings
 */
static void Load_TRRM()
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		TraceRestrictMappingItem &item = _tracerestrictprogram_mapping[index];
		SlObject(&item, _trace_restrict_mapping_desc);
	}
}

/**
 * Save mappings
 */
static void Save_TRRM()
{
	for (TraceRestrictMapping::iterator iter = _tracerestrictprogram_mapping.begin();
			iter != _tracerestrictprogram_mapping.end(); ++iter) {
		SlSetArrayIndex(iter->first);
		SlObject(&(iter->second), _trace_restrict_mapping_desc);
	}
}

/** program length save header struct */
struct TraceRestrictProgramStub {
	uint32 length;
};

static const SaveLoad _trace_restrict_program_stub_desc[] = {
	SLE_VAR(TraceRestrictProgramStub, length, SLE_UINT32),
	SLE_END()
};

/**
 * Load program pool
 */
static void Load_TRRP()
{
	int index;
	TraceRestrictProgramStub stub;
	while ((index = SlIterateArray()) != -1) {
		TraceRestrictProgram *prog = new (index) TraceRestrictProgram();
		SlObject(&stub, _trace_restrict_program_stub_desc);
		prog->items.resize(stub.length);
		SlArray(&(prog->items[0]), stub.length, SLE_UINT32);
		if (SlXvIsFeaturePresent(XSLFI_JOKERPP)) {
			for (size_t i = 0; i < prog->items.size(); i++) {
				TraceRestrictItem &item = prog->items[i]; // note this is a reference,
				if (GetTraceRestrictType(item) == 19 || GetTraceRestrictType(item) == 20) {
					SetTraceRestrictType(item, (TraceRestrictItemType)(GetTraceRestrictType(item) + 2));
				}
				if (IsTraceRestrictDoubleItem(item)) i++;
			}
		}
		CommandCost validation_result = prog->Validate();
		if (validation_result.Failed()) {
			char str[4096];
			char *strend = str + seprintf(str, lastof(str), "Trace restrict program %d: %s\nProgram dump:",
					index, GetStringPtr(validation_result.GetErrorMessage()));
			for (unsigned int i = 0; i < prog->items.size(); i++) {
				if (i % 3) {
					strend += seprintf(strend, lastof(str), " %08X", prog->items[i]);
				} else {
					strend += seprintf(strend, lastof(str), "\n%4u: %08X", i, prog->items[i]);
				}
			}
			SlErrorCorrupt(str);
		}
	}
}

/**
 * Save a program, used by SlAutolength
 */
static void RealSave_TRRP(TraceRestrictProgram *prog)
{
	TraceRestrictProgramStub stub;
	stub.length = prog->items.size();
	SlObject(&stub, _trace_restrict_program_stub_desc);
	SlArray(&(prog->items[0]), stub.length, SLE_UINT32);
}

/**
 * Save program pool
 */
static void Save_TRRP()
{
	for (TraceRestrictProgram *prog : TraceRestrictProgram::Iterate()) {
		SlSetArrayIndex(prog->index);
		SlAutolength((AutolengthProc*) RealSave_TRRP, prog);
	}
}

/** program length save header struct */
struct TraceRestrictSlotStub {
	uint32 length;
};

static const SaveLoad _trace_restrict_slot_stub_desc[] = {
	SLE_VAR(TraceRestrictSlotStub, length, SLE_UINT32),
	SLE_END()
};

static const SaveLoad _trace_restrict_slot_desc[] = {
	SLE_VAR(TraceRestrictSlot, max_occupancy, SLE_UINT32),
	SLE_SSTR(TraceRestrictSlot, name, SLF_ALLOW_CONTROL),
	SLE_VAR(TraceRestrictSlot, owner, SLE_UINT8),
	SLE_END()
};

/**
 * Load slot pool
 */
static void Load_TRRS()
{
	int index;
	TraceRestrictSlotStub stub;
	while ((index = SlIterateArray()) != -1) {
		TraceRestrictSlot *slot = new (index) TraceRestrictSlot();
		SlObject(slot, _trace_restrict_slot_desc);
		SlObject(&stub, _trace_restrict_slot_stub_desc);
		slot->occupants.resize(stub.length);
		if (stub.length) SlArray(&(slot->occupants[0]), stub.length, SLE_UINT32);
	}
	TraceRestrictSlot::RebuildVehicleIndex();
}

/**
 * Save a slot, used by SlAutolength
 */
static void RealSave_TRRS(TraceRestrictSlot *slot)
{
	SlObject(slot, _trace_restrict_slot_desc);
	TraceRestrictSlotStub stub;
	stub.length = slot->occupants.size();
	SlObject(&stub, _trace_restrict_slot_stub_desc);
	if (stub.length) SlArray(&(slot->occupants[0]), stub.length, SLE_UINT32);
}

/**
 * Save slot pool
 */
static void Save_TRRS()
{
	for (TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
		SlSetArrayIndex(slot->index);
		SlAutolength((AutolengthProc*) RealSave_TRRS, slot);
	}
}

/**
 * Update program reference counts from just-loaded mapping
 */
void AfterLoadTraceRestrict()
{
	for (TraceRestrictMapping::iterator iter = _tracerestrictprogram_mapping.begin();
			iter != _tracerestrictprogram_mapping.end(); ++iter) {
		_tracerestrictprogram_pool.Get(iter->second.program_id)->IncrementRefCount();
	}
}

extern const ChunkHandler _trace_restrict_chunk_handlers[] = {
	{ 'TRRM', Save_TRRM, Load_TRRM, nullptr, nullptr, CH_SPARSE_ARRAY},    // Trace Restrict Mapping chunk
	{ 'TRRP', Save_TRRP, Load_TRRP, nullptr, nullptr, CH_ARRAY},           // Trace Restrict Mapping Program Pool chunk
	{ 'TRRS', Save_TRRS, Load_TRRS, nullptr, nullptr, CH_ARRAY | CH_LAST}, // Trace Restrict Slot Pool chunk
};
