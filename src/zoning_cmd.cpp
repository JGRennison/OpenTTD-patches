/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file zoning_cmd.cpp */

#include "stdafx.h"
#include "openttd.h"
#include "station_type.h"
#include "station_base.h"
#include "industry.h"
#include "gfx_func.h"
#include "viewport_func.h"
#include "map_func.h"
#include "company_func.h"
#include "town_map.h"
#include "table/sprites.h"
#include "station_func.h"
#include "station_map.h"
#include "town.h"
#include "tracerestrict.h"
#include "window_func.h"
#include "zoning.h"

Zoning _zoning;
static const SpriteID ZONING_INVALID_SPRITE_ID = UINT_MAX;

/**
 * Draw the zoning sprites.
 *
 * @param SpriteID image
 *        the image
 * @param SpriteID colour
 *        the colour of the zoning
 * @param TileInfo ti
 *        the tile
 */
void DrawZoningSprites(SpriteID image, SpriteID colour, const TileInfo *ti)
{
	if (colour != ZONING_INVALID_SPRITE_ID) {
		AddSortableSpriteToDraw(image + ti->tileh, colour, ti->x, ti->y, 0x10, 0x10, 1, ti->z + 7);
	}
}

/**
 * Detect whether this area is within the acceptance of any station.
 *
 * @param TileArea area
 *        the area to search by
 * @param Owner owner
 *        the owner of the stations which we need to match again
  * @param StationFacility facility_mask
 *        one or more facilities in the mask must be present for a station to be used
 * @return true if a station is found
 */
bool IsAreaWithinAcceptanceZoneOfStation(TileArea area, Owner owner, StationFacility facility_mask)
{
	int catchment = _settings_game.station.station_spread + (_settings_game.station.modified_catchment ? MAX_CATCHMENT : CA_UNMODIFIED) + _settings_game.station.catchment_increase;

	StationFinder morestations(TileArea(TileXY(TileX(area.tile) - (catchment / 2), TileY(area.tile) - (catchment / 2)),
			TileX(area.tile) + area.w + catchment, TileY(area.tile) + area.h + catchment));

	for (Station * const *st_iter = morestations.GetStations()->Begin(); st_iter != morestations.GetStations()->End(); ++st_iter) {
		const Station *st = *st_iter;
		if (st->owner != owner || !(st->facilities & facility_mask)) continue;
		Rect rect = st->GetCatchmentRect();
		return TileArea(TileXY(rect.left, rect.top), TileXY(rect.right, rect.bottom)).Intersects(area);
	}

	return false;
}

/**
 * Detect whether this tile is within the acceptance of any station.
 *
 * @param TileIndex tile
 *        the tile to search by
 * @param Owner owner
 *        the owner of the stations
 * @param StationFacility facility_mask
 *        one or more facilities in the mask must be present for a station to be used
 * @param open_window_only
 *        only use stations which have their station window open
 * @return true if a station is found
 */
bool IsTileWithinAcceptanceZoneOfStation(TileIndex tile, Owner owner, StationFacility facility_mask, bool open_window_only)
{
	int catchment = _settings_game.station.station_spread + (_settings_game.station.modified_catchment ? MAX_CATCHMENT : CA_UNMODIFIED) + _settings_game.station.catchment_increase;

	StationFinder morestations(TileArea(TileXY(TileX(tile) - (catchment / 2), TileY(tile) - (catchment / 2)),
			catchment, catchment));

	for (Station * const *st_iter = morestations.GetStations()->Begin(); st_iter != morestations.GetStations()->End(); ++st_iter) {
		const Station *st = *st_iter;
		if (st->owner != owner || !(st->facilities & facility_mask)) continue;
		Rect rect = st->GetCatchmentRect();
		if ((uint)rect.left <= TileX(tile) && TileX(tile) <= (uint)rect.right
				&& (uint)rect.top <= TileY(tile) && TileY(tile) <= (uint)rect.bottom) {
			if (!open_window_only || FindWindowById(WC_STATION_VIEW, st->index) != NULL) {
				return true;
			}
		}
	}

	return false;
}

/**
 * Check whether the player can build in tile.
 *
 * @param TileIndex tile
 * @param Owner owner
 * @return red if they cannot
 */
SpriteID TileZoneCheckBuildEvaluation(TileIndex tile, Owner owner)
{
	/* Let's first check for the obvious things you cannot build on */
	switch (GetTileType(tile)) {
		case MP_INDUSTRY:
		case MP_OBJECT:
		case MP_STATION:
		case MP_HOUSE:
		case MP_TUNNELBRIDGE:
			return SPR_ZONING_INNER_HIGHLIGHT_RED;

		/* There are only two things you can own (or some else
		 * can own) that you can still build on. i.e. roads and
		 * railways.
		 * @todo
		 * Add something more intelligent, check what tool the
		 * user is currently using (and if none, assume some
		 * standards), then check it against if owned by some-
		 * one else (e.g. railway on someone else's road).
		 * While that being said, it should also check if it
		 * is not possible to build railway/road on someone
		 * else's/your own road/railway (e.g. the railway track
		 * is curved or a cross).
		 */
		case MP_ROAD:
		case MP_RAILWAY:
			if (GetTileOwner(tile) != owner) {
				return SPR_ZONING_INNER_HIGHLIGHT_RED;
			} else {
				return ZONING_INVALID_SPRITE_ID;
			}

		default:
			return ZONING_INVALID_SPRITE_ID;
	}
}

/**
 * Check the opinion of the local authority in the tile.
 *
 * @param TileIndex tile
 * @param Owner owner
 * @return black if no opinion, orange if bad,
 *         light blue if good or invalid if no town
 */
SpriteID TileZoneCheckOpinionEvaluation(TileIndex tile, Owner owner)
{
	int opinion = 0; // 0: no town, 1: no opinion, 2: bad, 3: good
	Town *town = ClosestTownFromTile(tile, _settings_game.economy.dist_local_authority);

	if (town != NULL) {
		if (HasBit(town->have_ratings, owner)) {
			opinion = (town->ratings[owner] > 0) ? 3 : 2;
		} else {
			opinion = 1;
		}
	}

	switch (opinion) {
		case 1:  return SPR_ZONING_INNER_HIGHLIGHT_BLACK;      // no opinion
		case 2:  return SPR_ZONING_INNER_HIGHLIGHT_ORANGE;     // bad
		case 3:  return SPR_ZONING_INNER_HIGHLIGHT_LIGHT_BLUE; // good
		default: return ZONING_INVALID_SPRITE_ID;              // no town
	}
}

/**
 * Detect whether the tile is within the catchment zone of a station.
 *
 * @param TileIndex tile
 * @param Owner owner
 * @param open_window_only
 *        only use stations which have their station window open
 * @return black if within, light blue if only in acceptance zone
 *         and nothing if no nearby station.
 */
SpriteID TileZoneCheckStationCatchmentEvaluation(TileIndex tile, Owner owner, bool open_window_only)
{
	// Never on a station.
	if (IsTileType(tile, MP_STATION)) {
		return ZONING_INVALID_SPRITE_ID;
	}

	// For provided goods
	StationFinder stations(TileArea(tile, 1, 1));

	for (Station * const *st_iter = stations.GetStations()->Begin(); st_iter != stations.GetStations()->End(); ++st_iter) {
		const Station *st = *st_iter;
		if (st->owner == owner) {
			if (!open_window_only || FindWindowById(WC_STATION_VIEW, st->index) != NULL) {
				return SPR_ZONING_INNER_HIGHLIGHT_BLACK;
			}
		}
	}

	// For accepted goods
	if (IsTileWithinAcceptanceZoneOfStation(tile, owner, ~FACIL_NONE, open_window_only)) {
		return SPR_ZONING_INNER_HIGHLIGHT_LIGHT_BLUE;
	}

	return ZONING_INVALID_SPRITE_ID;
}

/**
 * Detect whether a building is unserved by a station of owner.
 *
 * @param TileIndex tile
 * @param Owner owner
 * @return red if unserved, orange if only accepting, nothing if served or not
 *         a building
 */
SpriteID TileZoneCheckUnservedBuildingsEvaluation(TileIndex tile, Owner owner)
{
	if (!IsTileType(tile, MP_HOUSE)) {
		return ZONING_INVALID_SPRITE_ID;
	}

	CargoArray dat;

	memset(&dat, 0, sizeof(dat));
	AddAcceptedCargo(tile, dat, NULL);
	if (dat[CT_MAIL] + dat[CT_PASSENGERS] == 0) {
		// nothing is accepted, so now test if cargo is produced
		AddProducedCargo(tile, dat);
		if (dat[CT_MAIL] + dat[CT_PASSENGERS] == 0) {
			// total is still 0, so give up
			return ZONING_INVALID_SPRITE_ID;
		}
	}

	StationFinder stations(TileArea(tile, 1, 1));

	for (Station * const *st_iter = stations.GetStations()->Begin(); st_iter != stations.GetStations()->End(); ++st_iter) {
		const Station *st = *st_iter;
		if (st->owner == owner) {
			return ZONING_INVALID_SPRITE_ID;
		}
	}

	// For accepted goods
	if (IsTileWithinAcceptanceZoneOfStation(tile, owner, ~FACIL_NONE, false)) {
		return SPR_ZONING_INNER_HIGHLIGHT_ORANGE;
	}

	return SPR_ZONING_INNER_HIGHLIGHT_RED;
}

/**
 * Detect whether an industry is unserved by a station of owner.
 *
 * @param TileIndex tile
 * @param Owner owner
 * @return red if unserved, orange if only accepting, nothing if served or not
 *         a building
 */
SpriteID TileZoneCheckUnservedIndustriesEvaluation(TileIndex tile, Owner owner)
{
	if (IsTileType(tile, MP_INDUSTRY)) {
		Industry *ind = Industry::GetByTile(tile);
		StationFinder stations(ind->location);

		for (Station * const *st_iter = stations.GetStations()->Begin(); st_iter != stations.GetStations()->End(); ++st_iter) {
			const Station *st = *st_iter;
			if (st->owner == owner && st->facilities & (~FACIL_BUS_STOP)) {
				return ZONING_INVALID_SPRITE_ID;
			}
		}

		// For accepted goods
		if (IsAreaWithinAcceptanceZoneOfStation(ind->location, owner, ~FACIL_BUS_STOP)) {
			return SPR_ZONING_INNER_HIGHLIGHT_ORANGE;
		}

		return SPR_ZONING_INNER_HIGHLIGHT_RED;
	}

	return ZONING_INVALID_SPRITE_ID;
}

/**
 * Detect whether a tile is a restricted signal tile
 *
 * @param TileIndex tile
 * @param Owner owner
 * @return red if a restricted signal, nothing otherwise
 */
SpriteID TileZoneCheckTraceRestrictEvaluation(TileIndex tile, Owner owner)
{
	if (IsTileType(tile, MP_RAILWAY) && HasSignals(tile) && IsRestrictedSignal(tile)) {
		return SPR_ZONING_INNER_HIGHLIGHT_RED;
	}

	return ZONING_INVALID_SPRITE_ID;
}

/**
 * General evaluation function; calls all the other functions depending on
 * evaluation mode.
 *
 * @param TileIndex tile
 *        Tile to be evaluated.
 * @param Owner owner
 *        The current player
 * @param ZoningEvaluationMode ev_mode
 *        The current evaluation mode.
 * @return The colour returned by the evaluation functions (none if no ev_mode).
 */
SpriteID TileZoningSpriteEvaluation(TileIndex tile, Owner owner, ZoningEvaluationMode ev_mode)
{
	switch (ev_mode) {
		case ZEM_CAN_BUILD:     return TileZoneCheckBuildEvaluation(tile, owner);
		case ZEM_AUTHORITY:     return TileZoneCheckOpinionEvaluation(tile, owner);
		case ZEM_STA_CATCH:     return TileZoneCheckStationCatchmentEvaluation(tile, owner, false);
		case ZEM_STA_CATCH_WIN: return TileZoneCheckStationCatchmentEvaluation(tile, owner, true);
		case ZEM_BUL_UNSER:     return TileZoneCheckUnservedBuildingsEvaluation(tile, owner);
		case ZEM_IND_UNSER:     return TileZoneCheckUnservedIndustriesEvaluation(tile, owner);
		case ZEM_TRACERESTRICT: return TileZoneCheckTraceRestrictEvaluation(tile, owner);
		default:                return ZONING_INVALID_SPRITE_ID;
	}
}

/**
 * Draw the the zoning on the tile.
 *
 * @param TileInfo ti
 *        the tile to draw on.
 */
void DrawTileZoning(const TileInfo *ti)
{
	if (IsTileType(ti->tile, MP_VOID) || _game_mode != GM_NORMAL) {
		return;
	}

	if (_zoning.outer != ZEM_NOTHING) {
		DrawZoningSprites(SPR_SELECT_TILE, TileZoningSpriteEvaluation(ti->tile, _local_company, _zoning.outer), ti);
	}

	if (_zoning.inner != ZEM_NOTHING) {
		DrawZoningSprites(SPR_ZONING_INNER_HIGHLIGHT_BASE, TileZoningSpriteEvaluation(ti->tile, _local_company, _zoning.inner), ti);
	}
}

static uint GetZoningModeDependantStationCoverageRadius(const Station *st, ZoningEvaluationMode ev_mode)
{
	switch (ev_mode) {
		case ZEM_STA_CATCH:      return st->GetCatchmentRadius();
		case ZEM_STA_CATCH_WIN:  return st->GetCatchmentRadius();
		case ZEM_BUL_UNSER:      return st->GetCatchmentRadius();
		case ZEM_IND_UNSER:      return st->GetCatchmentRadius() + 10; // this is to wholly update industries partially within the region
		default:                 return 0;
	}
}

/**
 * Mark dirty the coverage area around a station if the current zoning mode depends on station coverage
 *
 * @param const Station *st
 *        The station to use
 */
void ZoningMarkDirtyStationCoverageArea(const Station *st)
{
	if (st->rect.IsEmpty()) return;

	uint radius = max<uint>(GetZoningModeDependantStationCoverageRadius(st, _zoning.outer), GetZoningModeDependantStationCoverageRadius(st, _zoning.inner));

	if (radius > 0) {
		Rect rect = st->GetCatchmentRectUsingRadius(radius);
		for (int x = rect.left; x <= rect.right; x++) {
			for (int y = rect.top; y <= rect.bottom; y++) {
				MarkTileDirtyByTile(TileXY(x, y));
			}
		}
	}
}
