/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_newsignals.cpp NewGRF handling of new signals. */

#include "stdafx.h"
#include "debug.h"
#include "newgrf_newsignals.h"
#include "map_func.h"

#include "safeguards.h"

std::vector<const GRFFile *> _new_signals_grfs;

/* virtual */ uint32 NewSignalsScopeResolver::GetRandomBits() const
{
	uint tmp = CountBits(this->tile + (TileX(this->tile) + TileY(this->tile)) * TILE_SIZE);
	return GB(tmp, 0, 2);
}

/* virtual */ uint32 NewSignalsScopeResolver::GetVariable(uint16 variable, uint32 parameter, GetVariableExtra *extra) const
{
	if (this->tile == INVALID_TILE) {
		switch (variable) {
			case 0x40: return 0;
		}
	}

	switch (variable) {
		case 0x40: return GetTerrainType(this->tile, this->context);
	}

	DEBUG(grf, 1, "Unhandled new signals tile variable 0x%X", variable);

	extra->available = false;
	return UINT_MAX;
}

/* virtual */ const SpriteGroup *NewSignalsResolverObject::ResolveReal(const RealSpriteGroup *group) const
{
	if (!group->loading.empty()) return group->loading[0];
	if (!group->loaded.empty())  return group->loaded[0];
	return nullptr;
}

GrfSpecFeature NewSignalsResolverObject::GetFeature() const
{
	return GSF_SIGNALS;
}

/**
 * Resolver object for rail types.
 * @param grffile GRF file.
 * @param tile %Tile containing the track. For track on a bridge this is the southern bridgehead.
 * @param context Are we resolving sprites for the upper halftile, or on a bridge?
 * @param param1 Extra parameter (first parameter of the callback, except railtypes do not have callbacks).
 * @param param2 Extra parameter (second parameter of the callback, except railtypes do not have callbacks).
 */
NewSignalsResolverObject::NewSignalsResolverObject(const GRFFile *grffile, TileIndex tile, TileContext context, uint32 param1, uint32 param2)
	: ResolverObject(grffile, CBID_NO_CALLBACK, param1, param2), newsignals_scope(*this, tile, context)
{
	this->root_spritegroup = grffile != nullptr ? grffile->new_signals_group : nullptr;
}
