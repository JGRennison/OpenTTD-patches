/* $Id$ */

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
	SLE_END()
};

/** Save all plans. */
static void Save_PLAN()
{
	Plan *p;
	FOR_ALL_PLANS(p) {
		SlSetArrayIndex(p->index);
		SlObject(p, _plan_desc);
	}
}

/** Load all plans. */
static void Load_PLAN()
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		Plan *p = new (index) Plan();
		SlObject(p, _plan_desc);
	}
}

/** Save all plan lines. */
static void Save_PLANLINE()
{
	Plan *p;
	FOR_ALL_PLANS(p) {
		for (size_t i = 0; i < p->lines.size(); i++) {
			SlSetArrayIndex((uint) p->index << 16 | (uint) i);
			PlanLine *pl = p->lines[i];
			size_t plsz = pl->tiles.size();
			SlSetLength(plsz * sizeof(TileIndex));
			SlArray(&pl->tiles[0], plsz, SLE_UINT32);
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

	Plan *p;
	FOR_ALL_PLANS(p) {
		p->SetVisibility(false);
	}
}

/** Chunk handlers related to plans. */
extern const ChunkHandler _plan_chunk_handlers[] = {
	{ 'PLAN', Save_PLAN, Load_PLAN, NULL, NULL, CH_ARRAY},
	{ 'PLLN', Save_PLANLINE, Load_PLANLINE, NULL, NULL, CH_ARRAY | CH_LAST},
};
