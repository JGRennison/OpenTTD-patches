/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ai_sl.cpp Handles the saveload part of the AIs */

#include "../stdafx.h"
#include "saveload.h"

#include "../safeguards.h"

static const ChunkHandler ai_chunk_handlers[] = {
	MakeUpstreamChunkHandler<'AIPL', GeneralUpstreamChunkLoadInfo>(),
};

extern const ChunkHandlerTable _ai_chunk_handlers(ai_chunk_handlers);
