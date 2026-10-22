/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file randomizer_sl.cpp Code handling saving and loading of script randomizers */

#include "../stdafx.h"
#include "saveload.h"

#include "../safeguards.h"

/** Chunk handler for script randomizers. */
static const ChunkHandler randomizer_chunk_handlers[] = {
	MakeUpstreamChunkHandler<'SRND', GeneralUpstreamChunkLoadInfo>(),
};

extern const ChunkHandlerTable _randomizer_chunk_handlers(randomizer_chunk_handlers);
