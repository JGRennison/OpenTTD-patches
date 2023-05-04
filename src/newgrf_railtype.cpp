/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_railtype.cpp NewGRF handling of rail types. */

#include "stdafx.h"
#include "debug.h"
#include "newgrf_railtype.h"
#include "newgrf_newsignals.h"
#include "newgrf_extension.h"
#include "date_func.h"
#include "depot_base.h"
#include "town.h"
#include "signal_func.h"

#include "safeguards.h"

/* virtual */ uint32 RailTypeScopeResolver::GetRandomBits() const
{
	uint tmp = CountBits(this->tile + (TileX(this->tile) + TileY(this->tile)) * TILE_SIZE);
	return GB(tmp, 0, 2);
}

/* virtual */ uint32 RailTypeScopeResolver::GetVariable(uint16 variable, uint32 parameter, GetVariableExtra *extra) const
{
	if (this->tile == INVALID_TILE) {
		switch (variable) {
			case 0x40: return 0;
			case 0x41: return 0;
			case 0x42: return 0;
			case 0x43: return _date;
			case 0x44: return HZB_TOWN_EDGE;
			case A2VRI_RAILTYPE_SIGNAL_RESTRICTION_INFO: return 0;
			case A2VRI_RAILTYPE_SIGNAL_CONTEXT: return this->signal_context;
			case A2VRI_RAILTYPE_SIGNAL_SIDE: return GetNewSignalsSideVariable();
			case A2VRI_RAILTYPE_SIGNAL_VERTICAL_CLEARANCE: return 0xFF;
		}
	}

	switch (variable) {
		case 0x40: return GetTerrainType(this->tile, this->context);
		case 0x41: return 0;
		case 0x42: return IsLevelCrossingTile(this->tile) && IsCrossingBarred(this->tile);
		case 0x43:
			if (IsRailDepotTile(this->tile)) return Depot::GetByTile(this->tile)->build_date;
			return _date;
		case 0x44: {
			const Town *t = nullptr;
			if (IsRailDepotTile(this->tile)) {
				t = Depot::GetByTile(this->tile)->town;
			} else if (IsLevelCrossingTile(this->tile)) {
				t = ClosestTownFromTile(this->tile, UINT_MAX);
			}
			return t != nullptr ? GetTownRadiusGroup(t, this->tile) : HZB_TOWN_EDGE;
		}
		case A2VRI_RAILTYPE_SIGNAL_RESTRICTION_INFO:
			return GetNewSignalsRestrictedSignalsInfo(this->prog, this->tile, 0);
		case A2VRI_RAILTYPE_SIGNAL_CONTEXT:
			return GetNewSignalsSignalContext(this->signal_context, this->tile);
		case A2VRI_RAILTYPE_SIGNAL_SIDE:
			return GetNewSignalsSideVariable();
		case A2VRI_RAILTYPE_SIGNAL_VERTICAL_CLEARANCE:
			return GetNewSignalsVerticalClearanceInfo(this->tile, this->z);
	}

	DEBUG(grf, 1, "Unhandled rail type tile variable 0x%X", variable);

	extra->available = false;
	return UINT_MAX;
}

GrfSpecFeature RailTypeResolverObject::GetFeature() const
{
	return GSF_RAILTYPES;
}

uint32 RailTypeResolverObject::GetDebugID() const
{
	return this->railtype_scope.rti->label;
}

/**
 * Resolver object for rail types.
 * @param rti Railtype. nullptr in NewGRF Inspect window.
 * @param tile %Tile containing the track. For track on a bridge this is the southern bridgehead.
 * @param context Are we resolving sprites for the upper halftile, or on a bridge?
 * @param rtsg Railpart of interest
 * @param param1 Extra parameter (first parameter of the callback, except railtypes do not have callbacks).
 * @param param2 Extra parameter (second parameter of the callback, except railtypes do not have callbacks).
 * @param signal_context Signal context.
 * @param z Signal pixel z.
 * @param prog Routing restriction program.
 */
RailTypeResolverObject::RailTypeResolverObject(const RailtypeInfo *rti, TileIndex tile, TileContext context, RailTypeSpriteGroup rtsg, uint32 param1, uint32 param2,
		CustomSignalSpriteContext signal_context, const TraceRestrictProgram *prog, uint z)
	: ResolverObject(rti != nullptr ? rti->grffile[rtsg] : nullptr, CBID_NO_CALLBACK, param1, param2), railtype_scope(*this, rti, tile, context, signal_context, prog, z)
{
	this->root_spritegroup = rti != nullptr ? rti->group[rtsg] : nullptr;
}

/**
 * Get the sprite to draw for the given tile.
 * @param rti The rail type data (spec).
 * @param tile The tile to get the sprite for.
 * @param rtsg The type of sprite to draw.
 * @param context Where are we drawing the tile?
 * @param[out] num_results If not nullptr, return the number of sprites in the spriteset.
 * @return The sprite to draw.
 */
SpriteID GetCustomRailSprite(const RailtypeInfo *rti, TileIndex tile, RailTypeSpriteGroup rtsg, TileContext context, uint *num_results)
{
	assert(rtsg < RTSG_END);

	if (rti->group[rtsg] == nullptr) return 0;

	RailTypeResolverObject object(rti, tile, context, rtsg);
	const SpriteGroup *group = object.Resolve();
	if (group == nullptr || group->GetNumResults() == 0) return 0;

	if (num_results) *num_results = group->GetNumResults();

	return group->GetResult();
}

inline uint8 RemapAspect(uint8 aspect, uint8 extra_aspects, uint8 style)
{
	if (likely(extra_aspects == 0 || _extra_aspects == 0)) return std::min<uint8>(aspect, 1);
	if (aspect == 0) return 0;
	if (style != 0 && HasBit(_signal_style_masks.combined_normal_shunt, style)) {
		if (aspect == 1) {
			return 0xFF;
		}
		aspect--;
	}
	if (aspect >= extra_aspects + 1) return 1;
	return aspect + 1;
}

static PalSpriteID GetRailTypeCustomSignalSprite(const RailtypeInfo *rti, TileIndex tile, SignalType type, SignalVariant var, uint8 aspect,
		CustomSignalSpriteContext context, const TraceRestrictProgram *prog, uint z)
{
	if (rti->group[RTSG_SIGNALS] == nullptr) return { 0, PAL_NONE };
	if (type == SIGTYPE_PROG && !HasBit(rti->ctrl_flags, RTCF_PROGSIG)) return { 0, PAL_NONE };
	if (type == SIGTYPE_NO_ENTRY && !HasBit(rti->ctrl_flags, RTCF_NOENTRYSIG)) return { 0, PAL_NONE };

	uint32 param1 = (context == CSSC_GUI) ? 0x10 : 0x00;
	uint32 param2 = (type << 16) | (var << 8) | RemapAspect(aspect, rti->signal_extra_aspects, 0);
	if ((prog != nullptr) && HasBit(rti->ctrl_flags, RTCF_RESTRICTEDSIG)) SetBit(param2, 24);
	RailTypeResolverObject object(rti, tile, TCX_NORMAL, RTSG_SIGNALS, param1, param2, context, prog, z);

	const SpriteGroup *group = object.Resolve();
	if (group == nullptr || group->GetNumResults() == 0) return { 0, PAL_NONE };

	PaletteID pal = HasBit(rti->ctrl_flags, RTCF_RECOLOUR_ENABLED) ? GB(GetRegister(0x100), 0, 24) : PAL_NONE;
	return { group->GetResult(), pal };
}

/**
 * Get the sprite to draw for a given signal.
 * @param rti The rail type data (spec).
 * @param tile The tile to get the sprite for.
 * @param type Signal type.
 * @param var Signal variant.
 * @param state Signal state.
 * @param gui Is the sprite being used on the map or in the GUI?
 * @return The sprite to draw.
 */
CustomSignalSpriteResult GetCustomSignalSprite(const RailtypeInfo *rti, TileIndex tile, SignalType type, SignalVariant var, uint8 aspect,
		CustomSignalSpriteContext context, uint8 style, const TraceRestrictProgram *prog, uint z)
{
	if (_settings_client.gui.show_all_signal_default && style == 0) return { { 0, PAL_NONE }, false };

	if (style == 0) {
		PalSpriteID spr = GetRailTypeCustomSignalSprite(rti, tile, type, var, aspect, context, prog, z);
		if (spr.sprite != 0) return { spr, HasBit(rti->ctrl_flags, RTCF_RESTRICTEDSIG) };
	}

	for (const GRFFile *grf : _new_signals_grfs) {
		if (style == 0) {
			if (type == SIGTYPE_PROG && !HasBit(grf->new_signal_ctrl_flags, NSCF_PROGSIG)) continue;
			if (type == SIGTYPE_NO_ENTRY && !HasBit(grf->new_signal_ctrl_flags, NSCF_NOENTRYSIG)) continue;
		}
		if (!HasBit(grf->new_signal_style_mask, style)) continue;

		uint32 param1 = (context == CSSC_GUI) ? 0x10 : 0x00;
		uint32 param2 = (type << 16) | (var << 8) | RemapAspect(aspect, grf->new_signal_extra_aspects, style);
		if ((prog != nullptr) && HasBit(grf->new_signal_ctrl_flags, NSCF_RESTRICTEDSIG)) SetBit(param2, 24);
		NewSignalsResolverObject object(grf, tile, TCX_NORMAL, param1, param2, context, style, prog, z);

		const SpriteGroup *group = object.Resolve();
		if (group != nullptr && group->GetNumResults() != 0) {
			PaletteID pal = HasBit(grf->new_signal_ctrl_flags, NSCF_RECOLOUR_ENABLED) ? GB(GetRegister(0x100), 0, 24) : PAL_NONE;
			return { { group->GetResult(), pal }, HasBit(grf->new_signal_ctrl_flags, NSCF_RESTRICTEDSIG) };
		}
	}

	return { { 0, PAL_NONE }, false };
}

/**
 * Translate an index to the GRF-local railtype-translation table into a RailType.
 * @param railtype  Index into GRF-local translation table.
 * @param grffile   Originating GRF file.
 * @return RailType or INVALID_RAILTYPE if the railtype is unknown.
 */
RailType GetRailTypeTranslation(uint8 railtype, const GRFFile *grffile)
{
	if (grffile == nullptr || grffile->railtype_list.size() == 0) {
		/* No railtype table present. Return railtype as-is (if valid), so it works for original railtypes. */
		if (railtype >= RAILTYPE_END || GetRailTypeInfo(static_cast<RailType>(railtype))->label == 0) return INVALID_RAILTYPE;

		return static_cast<RailType>(railtype);
	} else {
		/* Railtype table present, but invalid index, return invalid type. */
		if (railtype >= grffile->railtype_list.size()) return INVALID_RAILTYPE;

		/* Look up railtype including alternate labels. */
		return GetRailTypeByLabel(grffile->railtype_list[railtype]);
	}
}

/**
 * Perform a reverse railtype lookup to get the GRF internal ID.
 * @param railtype The global (OpenTTD) railtype.
 * @param grffile The GRF to do the lookup for.
 * @return the GRF internal ID.
 */
uint8 GetReverseRailTypeTranslation(RailType railtype, const GRFFile *grffile)
{
	/* No rail type table present, return rail type as-is */
	if (grffile == nullptr || grffile->railtype_list.size() == 0) return railtype;

	/* Look for a matching rail type label in the table */
	RailTypeLabel label = GetRailTypeInfo(railtype)->label;

	int idx = find_index(grffile->railtype_list, label);
	if (idx >= 0) return idx;

	/* If not found, return as invalid */
	return 0xFF;
}

void DumpRailTypeSpriteGroup(RailType rt, DumpSpriteGroupPrinter print)
{
	char buffer[64];
	const RailtypeInfo *rti = GetRailTypeInfo(rt);

	static const char *sprite_group_names[] =  {
		"RTSG_CURSORS",
		"RTSG_OVERLAY",
		"RTSG_GROUND",
		"RTSG_TUNNEL",
		"RTSG_WIRES",
		"RTSG_PYLONS",
		"RTSG_BRIDGE",
		"RTSG_CROSSING",
		"RTSG_DEPOT",
		"RTSG_FENCES",
		"RTSG_TUNNEL_PORTAL",
		"RTSG_SIGNALS",
		"RTSG_GROUND_COMPLETE"
	};
	static_assert(lengthof(sprite_group_names) == RTSG_END);

	SpriteGroupDumper dumper(print);

	bool non_first_group = false;
	for (RailTypeSpriteGroup rtsg = (RailTypeSpriteGroup)0; rtsg < RTSG_END; rtsg = (RailTypeSpriteGroup)(rtsg + 1)) {
		if (rti->group[rtsg] != nullptr) {
			if (non_first_group) {
				print(nullptr, DSGPO_PRINT, 0, "");
			} else {
				non_first_group = true;
			}
			char *b = buffer;
			b = strecpy(b, sprite_group_names[rtsg], lastof(buffer));
			if (rti->grffile[rtsg] != nullptr) {
				b += seprintf(b, lastof(buffer), ", GRF: %08X", BSWAP32(rti->grffile[rtsg]->grfid));
			}
			print(nullptr, DSGPO_PRINT, 0, buffer);
			dumper.DumpSpriteGroup(rti->group[rtsg], 0);
		}
	}
}
