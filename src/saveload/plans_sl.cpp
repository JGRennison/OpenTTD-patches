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

/** Description of a plan within the savegame. */
static const SaveLoad _plan_desc[] = {
	SLE_VAR(Plan, owner,          SLE_UINT8),
	SLE_VAR(Plan, visible,        SLE_BOOL),
	SLE_VAR(Plan, visible_by_all, SLE_BOOL),
	SLE_VAR(Plan, creation_date,  SLE_INT32),
	SLE_CONDSSSTR_X(Plan, name, 0, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ENH_VIEWPORT_PLANS, 3)),
	SLE_CONDSSSTR_X(Plan, name, 0, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP, SL_JOKER_1_20)),
	SLE_END()
};

static void RealSave_PLAN(Plan *p)
{
	SlObject(p, _plan_desc);
	SlWriteUint32(p->lines.size());
	for (size_t i = 0; i < p->lines.size(); i++) {
		PlanLine *pl = p->lines[i];
		SlWriteUint32(pl->tiles.size());
		SlArray(&pl->tiles[0], pl->tiles.size(), SLE_UINT32);
	}
}

/** Save all plans. */
static void Save_PLAN()
{
	for (Plan *p : Plan::Iterate()) {
		SlSetArrayIndex(p->index);
		SlAutolength((AutolengthProc*) RealSave_PLAN, p);
	}
}

/** Load all plans. */
static void Load_PLAN()
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		Plan *p = new (index) Plan();
		SlObject(p, _plan_desc);
		if (SlXvIsFeaturePresent(XSLFI_ENH_VIEWPORT_PLANS, 2)) {
			const size_t line_count = SlReadUint32();
			p->lines.resize(line_count);
			for (size_t i = 0; i < line_count; i++) {
				PlanLine *pl = new PlanLine();
				p->lines[i] = pl;
				const size_t tile_count = SlReadUint32();
				pl->tiles.resize(tile_count);
				SlArray(&pl->tiles[0], tile_count, SLE_UINT32);
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
		SlArray(&pl->tiles[0], plsz, SLE_UINT32);
	}

	for (Plan *p : Plan::Iterate()) {
		p->SetVisibility(false);
	}
}

/** Chunk handlers related to plans. */
extern const ChunkHandler _plan_chunk_handlers[] = {
	{ 'PLAN', Save_PLAN, Load_PLAN, nullptr, nullptr, CH_ARRAY},
	{ 'PLLN', nullptr, Load_PLANLINE, nullptr, nullptr, CH_ARRAY | CH_LAST},
};
