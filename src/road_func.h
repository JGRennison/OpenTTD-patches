/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file road_func.h Functions related to roads. */

#ifndef ROAD_FUNC_H
#define ROAD_FUNC_H

#include "core/bitmath_func.hpp"
#include "road.h"
#include "transparency.h"
#include "settings_type.h"

/**
 * Whether the given roadtype is valid.
 * @param r the roadtype to check for validness
 * @return true if and only if valid
 */
inline bool IsValidRoadBits(RoadBits r)
{
	return r.Reset(ROAD_ALL).None();
}

/**
 * Calculate the complement of a RoadBits value
 *
 * Simply flips all bits in the RoadBits value to get the complement
 * of the RoadBits.
 *
 * @param r The given RoadBits value
 * @return the complement
 */
inline RoadBits ComplementRoadBits(RoadBits r)
{
	dbg_assert(IsValidRoadBits(r));
	return r.Flip(ROAD_ALL);
}

/**
 * Calculate the mirrored RoadBits
 *
 * Simply move the bits to their new position.
 *
 * @param r The given RoadBits value
 * @return the mirrored
 */
inline RoadBits MirrorRoadBits(RoadBits r)
{
	dbg_assert(IsValidRoadBits(r));
	return static_cast<RoadBits>(GB(r.base(), 0, 2) << 2 | GB(r.base(), 2, 2));
}

/**
 * Check if we've got a straight road
 *
 * @param r The given RoadBits
 * @return true if we've got a straight road
 */
inline bool IsStraightRoad(RoadBits r)
{
	dbg_assert(IsValidRoadBits(r));
	return (r == ROAD_X || r == ROAD_Y);
}

/**
 * Create the road-part which belongs to the given DiagDirection
 *
 * This function returns a RoadBits value which belongs to
 * the given DiagDirection.
 *
 * @param d The DiagDirection
 * @return The result RoadBits which the selected road-part set
 */
inline RoadBits DiagDirToRoadBits(DiagDirection d)
{
	dbg_assert(IsValidDiagDirection(d));
	return static_cast<RoadBits>(RoadBits{RoadBit::NW}.base() << (3 ^ d));
}

/**
 * Create the road-part which belongs to the given Axis
 *
 * This function returns a RoadBits value which belongs to
 * the given Axis.
 *
 * @param a The Axis
 * @return The result RoadBits which the selected road-part set
 */
inline RoadBits AxisToRoadBits(Axis a)
{
	dbg_assert(IsValidAxis(a));
	return a == Axis::X ? ROAD_X : ROAD_Y;
}

/**
 * Test if a road type has catenary
 * @param roadtype Road type to test
 * @return \c true iff the road should have catenary.
 */
inline bool HasRoadCatenary(RoadType roadtype)
{
	dbg_assert(roadtype < ROADTYPE_END);
	return GetRoadTypeInfo(roadtype)->flags.Test(RoadTypeFlag::Catenary);
}

/**
 * Test if we should draw road catenary
 * @param roadtype Road type to test
 * @return \c true iff the road should have catenary and catenary is visible.
 */
inline bool HasRoadCatenaryDrawn(RoadType roadtype)
{
	return HasRoadCatenary(roadtype) && !IsInvisibilitySet(TO_CATENARY);
}

bool HasRoadTypeAvail(CompanyID company, RoadType roadtype);
bool ValParamRoadType(RoadType roadtype);
RoadTypes GetCompanyRoadTypes(CompanyID company, bool introduces = true);
RoadTypes GetRoadTypes(bool introduces);
RoadTypes AddDateIntroducedRoadTypes(RoadTypes current, CalTime::Date date);

void UpdateLevelCrossing(TileIndex tile, bool sound = true, bool force_close = false);
void MarkDirtyAdjacentLevelCrossingTilesOnAdd(TileIndex tile, Axis road_axis);
void UpdateAdjacentLevelCrossingTilesOnRemove(TileIndex tile, Axis road_axis);
bool IsCrossingOccupiedByRoadVehicle(TileIndex t);

void UpdateRoadCachedOneWayStatesAroundTile(TileIndex tile);
void UpdateCompanyRoadInfrastructure(RoadType rt, Owner o, int count);
Money RoadMaintenanceCost(RoadType roadtype, uint32_t num, uint32_t total_num);

struct TileInfo;
void DrawRoadOverlays(const TileInfo *ti, PaletteID pal, const RoadTypeInfo *road_rti, const RoadTypeInfo *tram_rit, uint road_offset, uint tram_offset, bool draw_underlay = true);

inline bool RoadLayoutChangeNotificationEnabled(bool added)
{
	return _settings_game.pf.reroute_rv_on_layout_change >= (added ? 2 : 1);
}

inline void NotifyRoadLayoutChanged()
{
	_road_layout_change_counter++;
}

inline void NotifyRoadLayoutChanged(bool added)
{
	if (RoadLayoutChangeNotificationEnabled(added)) NotifyRoadLayoutChanged();
}

void NotifyRoadLayoutChangedIfTileNonLeaf(TileIndex tile, RoadTramType rtt, RoadBits present_bits);
void NotifyRoadLayoutChangedIfSimpleTunnelBridgeNonLeaf(TileIndex start, TileIndex end, DiagDirection start_dir, RoadTramType rtt);

#endif /* ROAD_FUNC_H */
