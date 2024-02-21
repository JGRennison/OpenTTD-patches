/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargomonitor_sl.cpp Code handling saving and loading of Cargo monitoring. */

#include "../stdafx.h"
#include "../cargomonitor.h"

#include "saveload.h"

#include "../safeguards.h"

/** Chunk definition of the cargomonitoring maps. */
static const ChunkHandler cargomonitor_chunk_handlers[] = {
	MakeUpstreamChunkHandler<'CMDL', GeneralUpstreamChunkLoadInfo>(),
	MakeUpstreamChunkHandler<'CMPU', GeneralUpstreamChunkLoadInfo>(),
};

extern const ChunkHandlerTable _cargomonitor_chunk_handlers(cargomonitor_chunk_handlers);
