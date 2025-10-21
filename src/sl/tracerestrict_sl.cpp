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

struct TraceRestrictProgramLabelsStructHandler final : public TypedSaveLoadStructHandler<TraceRestrictProgramLabelsStructHandler, TraceRestrictProgram> {
public:
	struct LabelWrapper {
		std::string label;
	};

	NamedSaveLoadTable GetDescription() const override
	{
		static const NamedSaveLoad description[] = {
			NSLT("label", SLE_SSTR(LabelWrapper, label, SLE_STR)),
		};
		return description;
	}

	void Save(TraceRestrictProgram *prog) const override
	{
		if (prog->texts == nullptr) {
			SlSetStructListLength(0);
			return;
		}

		SlSetStructListLength(prog->texts->labels.size());
		for (std::string &str : prog->texts->labels) {
			SlObjectSaveFiltered(&str, this->GetLoadDescription());
		}
	}

	void Load(TraceRestrictProgram *prog) const override
	{
		size_t num_labels = SlGetStructListLength(UINT16_MAX);
		if (num_labels == 0) return;

		if (prog->texts == nullptr) prog->texts = std::make_unique<TraceRestrictProgramTexts>();
		prog->texts->labels.resize(num_labels);
		for (std::string &str : prog->texts->labels) {
			SlObjectLoadFiltered(&str, this->GetLoadDescription());
		}
	}
};

static const NamedSaveLoad _trace_restrict_program_desc[] = {
	NSL("items", SLE_VARVEC(TraceRestrictProgram, items, SLE_UINT32)),
	NSLT_STRUCTLIST<TraceRestrictProgramLabelsStructHandler>("labels"),
};

/**
 * Load program pool
 */
static void Load_TRRP()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_trace_restrict_program_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		TraceRestrictProgram *prog = new (TraceRestrictProgramID(index)) TraceRestrictProgram();
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
			format_buffer buffer;
			buffer.format("Trace restrict program {}: {}\nProgram dump:", index, GetStringFmtParam(validation_result.GetErrorMessage()));
			uint fail_offset = validation_result.GetResultDataWithType().GetOrDefault<uint32_t>(UINT32_MAX);
			for (uint i = 0; i < (uint)prog->items.size(); i++) {
				if ((i % 3) == 0) {
					buffer.format("\n{:4}:", i);
				}
				if (i == fail_offset) {
					buffer.format(" [{:08X}]", prog->items[i]);
				} else {
					buffer.format(" {:08X}", prog->items[i]);
				}
			}
			SlErrorCorrupt(buffer.to_string());
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
	NSL("occupants",     SLE_CUSTOMLIST(TraceRestrictSlot, occupants, SLE_UINT32)),
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
		TraceRestrictSlot *slot = new (TraceRestrictSlotID(index)) TraceRestrictSlot();
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
		TraceRestrictSlotGroup *slot_group = new (TraceRestrictSlotGroupID(index)) TraceRestrictSlotGroup();
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
		TraceRestrictCounter *ctr = new (TraceRestrictCounterID(index)) TraceRestrictCounter();
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
 * Update program reference counts from just-loaded mapping and slot group memberships from slot parent values
 */
void AfterLoadTraceRestrict()
{
	for (const auto &it : _tracerestrictprogram_mapping) {
		TraceRestrictProgram::Get(it.second.program_id)->IncrementRefCount(it.first);
	}

	for (const TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
		TraceRestrictSlotGroupID parent = slot->parent_group;
		while (parent != INVALID_TRACE_RESTRICT_SLOT_GROUP) {
			TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::Get(parent);
			sg->contained_slots.push_back(slot->index);
			parent = sg->parent;
		}
	}
}

extern const ChunkHandler trace_restrict_chunk_handlers[] = {
	{ 'TRRM', Save_TRRM, Load_TRRM, nullptr, nullptr, CH_SPARSE_TABLE },    // Trace Restrict Mapping chunk
	{ 'TRRP', Save_TRRP, Load_TRRP, nullptr, nullptr, CH_TABLE },           // Trace Restrict Program Pool chunk
	{ 'TRRS', Save_TRRS, Load_TRRS, nullptr, nullptr, CH_TABLE },           // Trace Restrict Slot Pool chunk
	{ 'TRRG', Save_TRRG, Load_TRRG, nullptr, nullptr, CH_TABLE },           // Trace Restrict Slot Group Pool chunk
	{ 'TRRC', Save_TRRC, Load_TRRC, nullptr, nullptr, CH_TABLE },           // Trace Restrict Counter Pool chunk
};

extern const ChunkHandlerTable _trace_restrict_chunk_handlers(trace_restrict_chunk_handlers);
