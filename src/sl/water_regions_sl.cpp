/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file water_regions_sl.cpp Handles saving and loading of water region data */
#include "../stdafx.h"

#include "saveload.h"

extern SaveLoadVersion _sl_xv_upstream_version;

struct GetWaterRegionsLoadInfo
{
	static SaveLoadVersion GetLoadVersion()
	{
		return _sl_xv_upstream_version != SL_MIN_VERSION ? _sl_xv_upstream_version : SLV_WATER_REGIONS;
	}
};

static const ChunkHandler water_region_chunk_handlers[] = {
	MakeUpstreamChunkHandler<'WRGN', GetWaterRegionsLoadInfo>(),
};

extern const ChunkHandlerTable _water_region_chunk_handlers(water_region_chunk_handlers);
