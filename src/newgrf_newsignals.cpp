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
#include "newgrf_extension.h"
#include "map_func.h"
#include "tracerestrict.h"
#include "string_func.h"
#include "newgrf_dump.h"

#include "safeguards.h"

std::vector<const GRFFile *> _new_signals_grfs;
std::array<NewSignalStyle, MAX_NEW_SIGNAL_STYLES> _new_signal_styles;
uint8_t _default_signal_style_lookahead_extra_aspects = 0;
std::array<NewSignalStyleMapping, MAX_NEW_SIGNAL_STYLES> _new_signal_style_mapping;
uint8_t _num_new_signal_styles = 0;
uint16_t _enabled_new_signal_styles_mask = 0;

/* virtual */ uint32_t NewSignalsScopeResolver::GetRandomBits() const
{
	uint tmp = CountBits(this->tile + (TileX(this->tile) + TileY(this->tile)) * TILE_SIZE);
	return GB(tmp, 0, 2);
}

static uint8_t MapSignalStyle(uint8_t style)
{
	return style != 0 ? _new_signal_styles[style - 1].grf_local_id : 0;
}

uint32_t GetNewSignalsSideVariable()
{
	bool side;
	switch (_settings_game.construction.train_signal_side) {
		case 0:  side = false;                                 break; // left
		case 2:  side = true;                                  break; // right
		default: side = _settings_game.vehicle.road_side != 0; break; // driving side
	}
	return side ? 1 : 0;
}

/* virtual */ uint32_t NewSignalsScopeResolver::GetVariable(uint16_t variable, uint32_t parameter, GetVariableExtra &extra) const
{
	if (this->tile == INVALID_TILE) {
		switch (variable) {
			case 0x40: return 0;
			case A2VRI_SIGNALS_SIGNAL_RESTRICTION_INFO: return 0;
			case A2VRI_SIGNALS_SIGNAL_CONTEXT: return GetNewSignalsSignalContext(this->signal_context);
			case A2VRI_SIGNALS_SIGNAL_STYLE: return MapSignalStyle(this->signal_style);
			case A2VRI_SIGNALS_SIGNAL_SIDE: return GetNewSignalsSideVariable();
			case A2VRI_SIGNALS_SIGNAL_VERTICAL_CLEARANCE: return 0xFF;
		}
	}

	switch (variable) {
		case 0x40: return GetTerrainType(this->tile, this->context);
		case A2VRI_SIGNALS_SIGNAL_RESTRICTION_INFO:
			return GetNewSignalsRestrictedSignalsInfo(this->prog, this->tile, this->signal_style);
		case A2VRI_SIGNALS_SIGNAL_CONTEXT:
			return GetNewSignalsSignalContext(this->signal_context);
		case A2VRI_SIGNALS_SIGNAL_STYLE: return MapSignalStyle(this->signal_style);
		case A2VRI_SIGNALS_SIGNAL_SIDE: return GetNewSignalsSideVariable();
		case A2VRI_SIGNALS_SIGNAL_VERTICAL_CLEARANCE: return GetNewSignalsVerticalClearanceInfo(this->tile, this->z);
	}

	Debug(grf, 1, "Unhandled new signals tile variable 0x{:X}", variable);

	extra.available = false;
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
 * @param signal_context Signal context.
 * @param prog Routing restriction program.
 * @param z Signal pixel z.
 */
NewSignalsResolverObject::NewSignalsResolverObject(const GRFFile *grffile, TileIndex tile, TileContext context, uint32_t param1, uint32_t param2,
		CustomSignalSpriteContext signal_context, uint8_t signal_style, const TraceRestrictProgram *prog, uint z)
	: ResolverObject(grffile, CBID_NO_CALLBACK, param1, param2), newsignals_scope(*this, tile, context, signal_context, signal_style, prog, z)
{
	this->root_spritegroup = grffile != nullptr ? grffile->new_signals_group : nullptr;
}

uint GetNewSignalsRestrictedSignalsInfo(const TraceRestrictProgram *prog, TileIndex tile, uint8_t signal_style)
{
	uint result = 0;
	if (signal_style != 0 && HasBit(_signal_style_masks.always_reserve_through, signal_style)) result |= 2;
	if (prog != nullptr) {
		result |= 1;
		if ((prog->actions_used_flags & TRPAUF_RESERVE_THROUGH_ALWAYS) && !IsTileType(tile, MP_TUNNELBRIDGE)) result |= 2;
		if ((prog->actions_used_flags & TRPAUF_REVERSE_BEHIND) && !IsTileType(tile, MP_TUNNELBRIDGE)) result |= 4;
	}
	return result;
}

uint GetNewSignalsVerticalClearanceInfo(TileIndex tile, uint z)
{
	if (IsBridgeAbove(tile)) {
		uint height = GetBridgePixelHeight(GetNorthernBridgeEnd(tile));
		return std::min<uint>(0xFF, height - z);
	} else {
		return 0xFF;
	}
}

void DumpNewSignalsSpriteGroups(SpriteGroupDumper &dumper)
{
	bool first = true;
	for (const GRFFile *grf : _new_signals_grfs) {
		if (!first) dumper.Print("");
		dumper.Print(fmt::format("GRF: {:08X}", BSWAP32(grf->grfid)));
		first = false;
		dumper.DumpSpriteGroup(grf->new_signals_group, 0);
	}
}
