/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file plans_sl.cpp Code handling saving and loading of plans data. */

#include "../stdafx.h"
#include "../plans_base.h"
#include "../fios.h"

#include "saveload.h"

struct PlanLineStructHandler final : public TypedSaveLoadStructHandler<PlanLineStructHandler, Plan> {
	NamedSaveLoadTable GetDescription() const override
	{
		static const NamedSaveLoad _plan_line_sub_desc[] = {
			NSLT("tiles", SLE_VARVEC(PlanLine, tiles, SLE_UINT32)),
		};
		return _plan_line_sub_desc;
	}

	void Save(Plan *p) const override
	{
		SlSetStructListLength(p->lines.size());
		for (PlanLine *pl : p->lines) {
			SlObjectSaveFiltered(pl, this->GetLoadDescription());
		}
	}

	void Load(Plan *p) const override
	{
		size_t line_count = SlGetStructListLength(UINT32_MAX);
		p->lines.resize(line_count);
		for (size_t i = 0; i < line_count; i++) {
			PlanLine *pl = new PlanLine();
			p->lines[i] = pl;
			SlObjectLoadFiltered(pl, this->GetLoadDescription());
			pl->UpdateVisualExtents();
		}
	}
};

/** Description of a plan within the savegame. */
static const NamedSaveLoad _plan_desc[] = {
	NSL("owner",          SLE_VAR(Plan, owner,          SLE_UINT8)),
	NSL("visible",        SLE_VAR(Plan, visible,        SLE_BOOL)),
	NSL("visible_by_all", SLE_VAR(Plan, visible_by_all, SLE_BOOL)),
	NSL("creation_date",  SLE_VAR(Plan, creation_date,  SLE_INT32)),
	NSL("name",           SLE_CONDSSTR_X(Plan, name, SLE_STR, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ENH_VIEWPORT_PLANS, 3))),
	NSL("name",           SLE_CONDSSTR_X(Plan, name, SLE_STR, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP, SL_JOKER_1_20))),
	NSL("colour",         SLE_CONDVAR_X(Plan, colour, SLE_UINT8,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ENH_VIEWPORT_PLANS, 4))),
	NSLT_STRUCTLIST<PlanLineStructHandler>("lines"),
};

/** Save all plans. */
static void Save_PLAN()
{
	SaveLoadTableData slt = SlTableHeader(_plan_desc);

	for (Plan *p : Plan::Iterate()) {
		SlSetArrayIndex(p->index);
		SlObjectSaveFiltered(p, slt);
	}
}

/** Load all plans. */
static void Load_PLAN()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_plan_desc);

	if (SlIsTableChunk()) {
		int index;
		while ((index = SlIterateArray()) != -1) {
			Plan *p = new (index) Plan();
			SlObjectLoadFiltered(p, slt);
			p->SetVisibility(false);
		}
		return;
	}

	int index;
	while ((index = SlIterateArray()) != -1) {
		Plan *p = new (index) Plan();
		SlObjectLoadFiltered(p, slt);
		if (SlXvIsFeaturePresent(XSLFI_ENH_VIEWPORT_PLANS, 2)) {
			const size_t line_count = SlReadUint32();
			p->lines.resize(line_count);
			for (size_t i = 0; i < line_count; i++) {
				PlanLine *pl = new PlanLine();
				p->lines[i] = pl;
				const size_t tile_count = SlReadUint32();
				pl->tiles.resize(tile_count);
				SlArray(pl->tiles.data(), tile_count, SLE_UINT32);
				pl->UpdateVisualExtents();
			}
			p->SetVisibility(false);
		}
	}
}

/** Load all plan lines. */
static void Load_PLANLINE()
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		Plan *p = Plan::Get((uint) index >> 16);
		uint line_index = index & 0xFFFF;
		if (p->lines.size() <= line_index) p->lines.resize(line_index + 1);
		PlanLine *pl = new PlanLine();
		p->lines[line_index] = pl;
		size_t plsz = SlGetFieldLength() / sizeof(TileIndex);
		pl->tiles.resize(plsz);
		SlArray(pl->tiles.data(), plsz, SLE_UINT32);
		pl->UpdateVisualExtents();
	}

	for (Plan *p : Plan::Iterate()) {
		p->SetVisibility(false);
	}
}

/** Chunk handlers related to plans. */
static const ChunkHandler plan_chunk_handlers[] = {
	{ 'PLAN', Save_PLAN, Load_PLAN,     nullptr, nullptr, CH_TABLE },
	{ 'PLLN', nullptr,   Load_PLANLINE, nullptr, nullptr, CH_READONLY },
};

extern const ChunkHandlerTable _plan_chunk_handlers(plan_chunk_handlers);
