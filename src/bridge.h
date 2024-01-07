/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bridge.h Header file for bridges */

#ifndef BRIDGE_H
#define BRIDGE_H

#include "gfx_type.h"
#include "tile_cmd.h"

/**
 * This enum is related to the definition of bridge pieces,
 * which is used to determine the proper sprite table to use
 * while drawing a given bridge part.
 */
enum BridgePieces {
	BRIDGE_PIECE_NORTH = 0,
	BRIDGE_PIECE_SOUTH,
	BRIDGE_PIECE_INNER_NORTH,
	BRIDGE_PIECE_INNER_SOUTH,
	BRIDGE_PIECE_MIDDLE_ODD,
	BRIDGE_PIECE_MIDDLE_EVEN,
	BRIDGE_PIECE_HEAD,
	BRIDGE_PIECE_INVALID,
};

DECLARE_POSTFIX_INCREMENT(BridgePieces)

static const uint MAX_BRIDGES = 16; ///< Number of available bridge specs.

typedef uint BridgeType; ///< Bridge spec number.

/**
 * Actions that can be performed when the vehicle enters the depot.
 */
enum BridgePiecePillarFlags {
	BPPF_CORNER_W        = 1 << 0,
	BPPF_CORNER_S        = 1 << 1,
	BPPF_CORNER_E        = 1 << 2,
	BPPF_CORNER_N        = 1 << 3,
	BPPF_ALL_CORNERS     = 0xF,
	BPPF_EDGE_NE         = 1 << 4,
	BPPF_EDGE_SE         = 1 << 5,
	BPPF_EDGE_SW         = 1 << 6,
	BPPF_EDGE_NW         = 1 << 7,
};
DECLARE_ENUM_AS_BIT_SET(BridgePiecePillarFlags)

enum BridgeSpecCtrlFlags {
	BSCF_CUSTOM_PILLAR_FLAGS,
	BSCF_INVALID_PILLAR_FLAGS,
	BSCF_NOT_AVAILABLE_TOWN,
	BSCF_NOT_AVAILABLE_AI_GS,
};

/**
 * Struct containing information about a single bridge type
 */
struct BridgeSpec {
	Year avail_year;             ///< the year where it becomes available
	byte min_length;             ///< the minimum length (not counting start and end tile)
	uint16 max_length;           ///< the maximum length (not counting start and end tile)
	uint16 price;                ///< the price multiplier
	uint16 speed;                ///< maximum travel speed (1 unit = 1/1.6 mph = 1 km-ish/h)
	SpriteID sprite;             ///< the sprite which is used in the GUI
	PaletteID pal;               ///< the palette which is used in the GUI
	StringID material;           ///< the string that contains the bridge description
	StringID transport_name[2];  ///< description of the bridge, when built for road or rail
	PalSpriteID **sprite_table;  ///< table of sprites for drawing the bridge
	byte flags;                  ///< bit 0 set: disable drawing of far pillars.
	byte ctrl_flags;             ///< control flags
	byte pillar_flags[12];       ///< bridge pillar flags: 6 x pairs of x and y flags
};

extern BridgeSpec _bridge[MAX_BRIDGES];

Foundation GetBridgeFoundation(Slope tileh, Axis axis);
bool HasBridgeFlatRamp(Slope tileh, Axis axis);

/**
 * Get the specification of a bridge type.
 * @param i The type of bridge to get the specification for.
 * @return The specification.
 */
inline const BridgeSpec *GetBridgeSpec(BridgeType i)
{
	dbg_assert(i < lengthof(_bridge));
	return &_bridge[i];
}

void DrawBridgeMiddle(const TileInfo *ti);

CommandCost CheckBridgeAvailability(BridgeType bridge_type, uint bridge_len, DoCommandFlag flags = DC_NONE);
bool MayTownBuildBridgeType(BridgeType bridge_type);
int CalcBridgeLenCostFactor(int x);
BridgePiecePillarFlags GetBridgeTilePillarFlags(TileIndex tile, TileIndex northern_bridge_end, TileIndex southern_bridge_end, BridgeType bridge_type, TransportType bridge_transport_type);

struct BridgePieceDebugInfo {
	BridgePieces piece;
	BridgePiecePillarFlags pillar_flags;
	uint pillar_index;
};
BridgePieceDebugInfo GetBridgePieceDebugInfo(TileIndex tile);

void ResetBridges();

#endif /* BRIDGE_H */
