/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file autoreplace_sl.cpp Code handling saving and loading of autoreplace rules */

#include "../stdafx.h"
#include "../autoreplace_base.h"

#include "saveload.h"

#include "../safeguards.h"

static const ChunkHandler autoreplace_chunk_handlers[] = {
	MakeUpstreamChunkHandler<'ERNW', GeneralUpstreamChunkLoadInfo>(),
};

extern const ChunkHandlerTable _autoreplace_chunk_handlers(autoreplace_chunk_handlers);
