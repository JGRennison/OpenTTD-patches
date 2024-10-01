/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_newlandscape.cpp NewGRF handling of new landscape. */

#include "stdafx.h"
#include "debug.h"
#include "newgrf_newlandscape.h"
#include "newgrf_extension.h"
#include "map_func.h"
#include "clear_map.h"
#include "core/hash_func.hpp"
#include "string_func.h"
#include "newgrf_dump.h"

#include "safeguards.h"

std::vector<const GRFFile *> _new_landscape_rocks_grfs;

/* virtual */ uint32_t NewLandscapeScopeResolver::GetVariable(uint16_t variable, uint32_t parameter, GetVariableExtra &extra) const
{
	if (unlikely(this->ti->tile == INVALID_TILE)) {
		switch (variable) {
			case 0x40: return 0;
			case 0x41: return 0;
			case 0x42: return 0;
			case 0x43: return 0;
			case 0x44: return this->landscape_type;
			case 0x60: return 0;
		}
	}

	switch (variable) {
		case 0x40:
			return GetTerrainType(this->ti->tile, TCX_NORMAL);

		case 0x41:
			return this->ti->tileh;

		case 0x42:
			return this->ti->z / TILE_HEIGHT;

		case 0x43:
			return SimpleHash32(this->ti->tile);

		case 0x44:
			return this->landscape_type;

		case 0x45:
			return GetClearDensity(this->ti->tile) | (IsSnowTile(this->ti->tile) ? 0x10 : 0);

		case 0x60: {
			TileIndex tile = this->ti->tile;
			if (parameter != 0) tile = GetNearbyTile(parameter, tile); // only perform if it is required
			uint32_t result = 0;
			if (extra.mask & ~0x100) result |= GetNearbyTileInformation(tile, this->ro.grffile == nullptr || this->ro.grffile->grf_version >= 8, extra.mask);
			if (extra.mask & 0x100) {
				switch (this->landscape_type) {
					case NEW_LANDSCAPE_ROCKS:
						if (IsTileType(tile, MP_CLEAR) && IsClearGround(tile, CLEAR_ROCKS)) result |= 0x100;
						break;
				}
			}
			return result;
		}
	}

	DEBUG(grf, 1, "Unhandled new landscape tile variable 0x%X", variable);

	extra.available = false;
	return UINT_MAX;
}

/* virtual */ const SpriteGroup *NewLandscapeResolverObject::ResolveReal(const RealSpriteGroup *group) const
{
	if (!group->loading.empty()) return group->loading[0];
	if (!group->loaded.empty())  return group->loaded[0];
	return nullptr;
}

GrfSpecFeature NewLandscapeResolverObject::GetFeature() const
{
	return GSF_NEWLANDSCAPE;
}

NewLandscapeResolverObject::NewLandscapeResolverObject(const GRFFile *grffile, const TileInfo *ti, NewLandscapeType landscape_type, uint32_t param1, uint32_t param2)
	: ResolverObject(grffile, CBID_NO_CALLBACK, param1, param2), newlandscape_scope(*this, ti, landscape_type)
{
	if (grffile != nullptr) {
		switch (landscape_type) {
			case NEW_LANDSCAPE_ROCKS:
				this->root_spritegroup = grffile->new_rocks_group;
				break;

			default:
				this->root_spritegroup = nullptr;
				break;
		}
	} else {
		this->root_spritegroup = nullptr;
	}
}

void DumpNewLandscapeRocksSpriteGroups(SpriteGroupDumper &dumper)
{
	bool first = true;
	for (const GRFFile *grf : _new_landscape_rocks_grfs) {
		if (!first) dumper.Print("");
		dumper.Print(fmt::format("GRF: {:08X}", BSWAP32(grf->grfid)));
		first = false;
		dumper.DumpSpriteGroup(grf->new_rocks_group, 0);
	}
}
