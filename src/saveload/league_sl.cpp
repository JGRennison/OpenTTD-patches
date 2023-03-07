/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file league_sl.cpp Code handling saving and loading of league tables */
#include "../stdafx.h"

#include "saveload.h"

extern SaveLoadVersion _sl_xv_upstream_version;

struct GetLeagueChunkLoadInfo
{
	static SaveLoadVersion GetLoadVersion()
	{
		return _sl_xv_upstream_version != SL_MIN_VERSION ? _sl_xv_upstream_version : SLV_MULTITRACK_LEVEL_CROSSINGS;
	}
};

static const ChunkHandler league_chunk_handlers[] = {
	MakeUpstreamChunkHandler<'LEAE', GetLeagueChunkLoadInfo>(),
	MakeUpstreamChunkHandler<'LEAT', GetLeagueChunkLoadInfo>(),
};

extern const ChunkHandlerTable _league_chunk_handlers(league_chunk_handlers);
