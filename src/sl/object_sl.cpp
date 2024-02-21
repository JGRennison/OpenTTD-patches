/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file object_sl.cpp Code handling saving and loading of objects */

#include "../stdafx.h"
#include "../object_base.h"
#include "../object_map.h"

#include "saveload.h"
#include "newgrf_sl.h"

#include "../safeguards.h"

static void Save_OBID()
{
	Save_NewGRFMapping(_object_mngr);
}

static void Load_OBID()
{
	Load_NewGRFMapping(_object_mngr);
}

static const ChunkHandler object_chunk_handlers[] = {
	{ 'OBID', Save_OBID, Load_OBID, nullptr,   nullptr, CH_ARRAY },
	MakeUpstreamChunkHandler<'OBJS', GeneralUpstreamChunkLoadInfo>(),
};

extern const ChunkHandlerTable _object_chunk_handlers(object_chunk_handlers);
