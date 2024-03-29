/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file depot_sl.cpp Code handling saving and loading of depots */

#include "../stdafx.h"
#include "../depot_base.h"
#include "../town.h"

#include "saveload.h"

#include "../safeguards.h"

static TownID _town_index;

static const SaveLoad _depot_desc[] = {
	 SLE_CONDVAR(Depot, xy,         SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_6),
	 SLE_CONDVAR(Depot, xy,         SLE_UINT32,                 SLV_6, SL_MAX_VERSION),
	SLEG_CONDVAR(_town_index,       SLE_UINT16,                 SL_MIN_VERSION, SLV_141),
	 SLE_CONDREF(Depot, town,       REF_TOWN,                 SLV_141, SL_MAX_VERSION),
	 SLE_CONDVAR(Depot, town_cn,    SLE_UINT16,               SLV_141, SL_MAX_VERSION),
	 SLE_CONDSTR(Depot, name,       SLE_STR, 0,               SLV_141, SL_MAX_VERSION),
	 SLE_CONDVAR(Depot, build_date, SLE_INT32,                SLV_142, SL_MAX_VERSION),
	 SLE_CONDNULL_X(4,                                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 5)),
};

static void Load_DEPT()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		Depot *depot = new (index) Depot();
		SlObject(depot, _depot_desc);

		/* Set the town 'pointer' so we can restore it later. */
		if (IsSavegameVersionBefore(SLV_141)) depot->town = (Town *)(size_t)_town_index;
	}
}

static void Ptrs_DEPT()
{
	for (Depot *depot : Depot::Iterate()) {
		SlObject(depot, _depot_desc);
		if (IsSavegameVersionBefore(SLV_141)) depot->town = Town::Get((size_t)depot->town);
	}
}

static const ChunkHandler depot_chunk_handlers[] = {
	MakeSaveUpstreamFeatureConditionalLoadUpstreamChunkHandler<'DEPT', XSLFI_TABLE_MISC_SL, 2>(Load_DEPT, Ptrs_DEPT, nullptr),
};

extern const ChunkHandlerTable _depot_chunk_handlers(depot_chunk_handlers);
