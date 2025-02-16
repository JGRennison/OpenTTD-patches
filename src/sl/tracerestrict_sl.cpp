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

static const NamedSaveLoad _trace_restrict_mapping_desc[] = {
	NSL("program_id", SLE_VAR(TraceRestrictMappingItem, program_id, SLE_UINT32)),
};

/**
 * Load mappings
 */
static void Load_TRRM()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_trace_restrict_mapping_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		TraceRestrictMappingItem &item = _tracerestrictprogram_mapping[index];
		SlObjectLoadFiltered(&item, slt);
	}
}

/**
 * Save mappings
 */
static void Save_TRRM()
{
	SaveLoadTableData slt = SlTableHeader(_trace_restrict_mapping_desc);

	for (auto &it : _tracerestrictprogram_mapping) {
		SlSetArrayIndex(it.first);
		TraceRestrictMappingItem &item = it.second;
		SlObjectSaveFiltered(&item, slt);
	}
}

static const NamedSaveLoad _trace_restrict_program_desc[] = {
	NSL("items", SLE_VARVEC(TraceRestrictProgram, items, SLE_UINT32)),
};

/**
 * Load program pool
 */
static void Load_TRRP()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_trace_restrict_program_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		TraceRestrictProgram *prog = new (index) TraceRestrictProgram();
		SlObjectLoadFiltered(prog, slt);

		if (SlXvIsFeaturePresent(XSLFI_JOKERPP)) {
			for (auto iter : prog->IterateInstructionsMutable()) {
				TraceRestrictInstructionItemRef item = iter.InstructionRef(); // note this is a reference wrapper
				if (item.GetType() == 19 || item.GetType() == 20) {
					item.SetType((TraceRestrictItemType)(item.GetType() + 2));
				}
			}
		}
		if (SlXvIsFeatureMissing(XSLFI_TRACE_RESTRICT, 17)) {
			/* TRIT_SLOT subtype moved from cond op to combined aux and cond op field in version 17.
			 * Do this for all previous versions to avoid cases where it is unexpectedly present despite the version,
			 * e.g. in JokerPP and non-SLXI tracerestrict saves.
			 */
			for (auto iter : prog->IterateInstructionsMutable()) {
				TraceRestrictInstructionItemRef item = iter.InstructionRef(); // note this is a reference wrapper
				if (item.GetType() == TRIT_SLOT) {
					TraceRestrictSlotSubtypeField subtype = static_cast<TraceRestrictSlotSubtypeField>(item.GetCondOp());
					if (subtype == 7) {
						/* Was TRSCOF_ACQUIRE_TRY_ON_RESERVE */
						subtype = TRSCOF_ACQUIRE_TRY;
					}
					item.SetCombinedAuxCondOpField(subtype);
				}
			}
		}
		CommandCost validation_result = prog->Validate();
		if (validation_result.Failed()) {
			auto buffer = fmt::memory_buffer();
			fmt::format_to(std::back_inserter(buffer), "Trace restrict program {}: {}\nProgram dump:",
					index, GetStringPtr(validation_result.GetErrorMessage()));
			uint fail_offset = validation_result.HasResultData() ? validation_result.GetResultData() : UINT32_MAX;
			for (uint i = 0; i < (uint)prog->items.size(); i++) {
				if ((i % 3) == 0) {
					fmt::format_to(std::back_inserter(buffer), "\n{:4}:", i);
				}
				if (i == fail_offset) {
					fmt::format_to(std::back_inserter(buffer), " [{:08X}]", prog->items[i]);
				} else {
					fmt::format_to(std::back_inserter(buffer), " {:08X}", prog->items[i]);
				}
			}
			SlErrorCorrupt(fmt::to_string(buffer));
		}
	}
}

/**
 * Save program pool
 */
static void Save_TRRP()
{
	SaveLoadTableData slt = SlTableHeader(_trace_restrict_program_desc);

	for (TraceRestrictProgram *prog : TraceRestrictProgram::Iterate()) {
		SlSetArrayIndex(prog->index);
		SlObjectSaveFiltered(prog, slt);
	}
}

static const NamedSaveLoad _trace_restrict_slot_desc[] = {
	NSL("max_occupancy", SLE_VAR(TraceRestrictSlot, max_occupancy, SLE_UINT32)),
	NSL("name",          SLE_SSTR(TraceRestrictSlot, name, SLE_STR | SLF_ALLOW_CONTROL)),
	NSL("owner",         SLE_VAR(TraceRestrictSlot, owner, SLE_UINT8)),
	NSL("vehicle_type",  SLE_CONDVAR_X(TraceRestrictSlot, vehicle_type, SLE_UINT8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TRACE_RESTRICT, 13))),
	NSL("occupants",     SLE_VARVEC(TraceRestrictSlot, occupants, SLE_UINT32)),
	NSLT("flags",        SLE_VAR(TraceRestrictSlot, flags, SLE_UINT8)),
	NSLT("parent_group", SLE_VAR(TraceRestrictSlot, parent_group, SLE_UINT16)),
};

/**
 * Load slot pool
 */
static void Load_TRRS()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_trace_restrict_slot_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		TraceRestrictSlot *slot = new (index) TraceRestrictSlot();
		SlObjectLoadFiltered(slot, slt);
	}
	TraceRestrictSlot::RebuildVehicleIndex();
}

/**
 * Save slot pool
 */
static void Save_TRRS()
{
	SaveLoadTableData slt = SlTableHeader(_trace_restrict_slot_desc);

	for (TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
		SlSetArrayIndex(slot->index);
		SlObjectSaveFiltered(slot, slt);
	}
}

static const NamedSaveLoad _trace_restrict_slot_group_desc[] = {
	NSLT("name",          SLE_SSTR(TraceRestrictSlotGroup, name, SLE_STR | SLF_ALLOW_CONTROL)),
	NSLT("owner",         SLE_VAR(TraceRestrictSlotGroup, owner, SLE_UINT8)),
	NSLT("vehicle_type",  SLE_VAR(TraceRestrictSlotGroup, vehicle_type, SLE_UINT8)),
	NSLT("parent",        SLE_VAR(TraceRestrictSlotGroup, parent, SLE_UINT16)),
};

/**
 * Load slot group pool
 */
static void Load_TRRG()
{
	SaveLoadTableData slt = SlTableHeader(_trace_restrict_slot_group_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		TraceRestrictSlotGroup *slot_group = new (index) TraceRestrictSlotGroup();
		SlObjectLoadFiltered(slot_group, slt);
	}
}

/**
 * Save slot group pool
 */
static void Save_TRRG()
{
	SaveLoadTableData slt = SlTableHeader(_trace_restrict_slot_group_desc);

	for (TraceRestrictSlotGroup *slot_group : TraceRestrictSlotGroup::Iterate()) {
		SlSetArrayIndex(slot_group->index);
		SlObjectSaveFiltered(slot_group, slt);
	}
}

static const NamedSaveLoad _trace_restrict_counter_desc[] = {
	NSL("value",  SLE_VAR(TraceRestrictCounter, value, SLE_INT32)),
	NSL("name",   SLE_SSTR(TraceRestrictCounter, name, SLE_STR | SLF_ALLOW_CONTROL)),
	NSL("owner",  SLE_VAR(TraceRestrictCounter, owner, SLE_UINT8)),
	NSLT("flags", SLE_VAR(TraceRestrictCounter, flags, SLE_UINT8)),
};

/**
 * Load counter pool
 */
static void Load_TRRC()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_trace_restrict_counter_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		TraceRestrictCounter *ctr = new (index) TraceRestrictCounter();
		SlObjectLoadFiltered(ctr, slt);
	}
}

/**
 * Save counter pool
 */
static void Save_TRRC()
{
	SaveLoadTableData slt = SlTableHeader(_trace_restrict_counter_desc);

	for (TraceRestrictCounter *ctr : TraceRestrictCounter::Iterate()) {
		SlSetArrayIndex(ctr->index);
		SlObjectSaveFiltered(ctr, slt);
	}
}

/**
 * Update program reference counts from just-loaded mapping
 */
void AfterLoadTraceRestrict()
{
	for (const auto &it : _tracerestrictprogram_mapping) {
		_tracerestrictprogram_pool.Get(it.second.program_id)->IncrementRefCount(it.first);
	}
}

extern const ChunkHandler trace_restrict_chunk_handlers[] = {
	{ 'TRRM', Save_TRRM, Load_TRRM, nullptr, nullptr, CH_SPARSE_TABLE },    // Trace Restrict Mapping chunk
	{ 'TRRP', Save_TRRP, Load_TRRP, nullptr, nullptr, CH_TABLE },           // Trace Restrict Mapping Program Pool chunk
	{ 'TRRS', Save_TRRS, Load_TRRS, nullptr, nullptr, CH_TABLE },           // Trace Restrict Slot Pool chunk
	{ 'TRRG', Save_TRRG, Load_TRRG, nullptr, nullptr, CH_TABLE },           // Trace Restrict Slot Group Pool chunk
	{ 'TRRC', Save_TRRC, Load_TRRC, nullptr, nullptr, CH_TABLE },           // Trace Restrict Counter Pool chunk
};

extern const ChunkHandlerTable _trace_restrict_chunk_handlers(trace_restrict_chunk_handlers);
