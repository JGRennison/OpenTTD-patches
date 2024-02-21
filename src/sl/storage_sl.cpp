/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file storage_sl.cpp Code handling saving and loading of persistent storages. */

#include "../stdafx.h"
#include "../newgrf_storage.h"
#include "saveload.h"

#include "../safeguards.h"

/** Chunk handler for persistent storages. */
static const ChunkHandler persistent_storage_chunk_handlers[] = {
	MakeUpstreamChunkHandler<'PSAC', GeneralUpstreamChunkLoadInfo>(),
};

extern const ChunkHandlerTable _persistent_storage_chunk_handlers(persistent_storage_chunk_handlers);
