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
#include "viewport_func.h"
#include "3rdparty/cpp-btree/btree_set.h"

Zoning _zoning;
static const SpriteID ZONING_INVALID_SPRITE_ID = UINT_MAX;

static btree::btree_set<uint32> _zoning_cache_inner;
static btree::btree_set<uint32> _zoning_cache_outer;

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

inline SpriteID TileZoningSpriteEvaluationCached(TileIndex tile, Owner owner, ZoningEvaluationMode ev_mode, bool is_inner)
{
	if (ev_mode == ZEM_BUL_UNSER && !IsTileType(tile, MP_HOUSE)) return ZONING_INVALID_SPRITE_ID;
	if (ev_mode == ZEM_IND_UNSER && !IsTileType(tile, MP_INDUSTRY)) return ZONING_INVALID_SPRITE_ID;
	if (ev_mode >= ZEM_STA_CATCH && ev_mode <= ZEM_IND_UNSER) {
		// cacheable
		btree::btree_set<uint32> &cache = is_inner ? _zoning_cache_inner : _zoning_cache_outer;
		auto iter = cache.lower_bound(tile << 3);
		if (iter != cache.end() && *iter >> 3 == tile) {
			switch (*iter & 7) {
				case 0: return ZONING_INVALID_SPRITE_ID;
				case 1: return SPR_ZONING_INNER_HIGHLIGHT_RED;
				case 2: return SPR_ZONING_INNER_HIGHLIGHT_ORANGE;
				case 3: return SPR_ZONING_INNER_HIGHLIGHT_BLACK;
				case 4: return SPR_ZONING_INNER_HIGHLIGHT_LIGHT_BLUE;
				default: NOT_REACHED();
			}
		} else {
			SpriteID s = TileZoningSpriteEvaluation(tile, owner, ev_mode);
			uint val = tile << 3;
			switch (s) {
				case ZONING_INVALID_SPRITE_ID:              val |= 0; break;
				case SPR_ZONING_INNER_HIGHLIGHT_RED:        val |= 1; break;
				case SPR_ZONING_INNER_HIGHLIGHT_ORANGE:     val |= 2; break;
				case SPR_ZONING_INNER_HIGHLIGHT_BLACK:      val |= 3; break;
				case SPR_ZONING_INNER_HIGHLIGHT_LIGHT_BLUE: val |= 4; break;
				default: NOT_REACHED();
			}
			cache.insert(iter, val);
			return s;
		}
	} else {
		return TileZoningSpriteEvaluation(tile, owner, ev_mode);
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
		const SpriteID colour = TileZoningSpriteEvaluationCached(ti->tile, _local_company, _zoning.outer, false);

		if (colour != ZONING_INVALID_SPRITE_ID) {
			DrawTileSelectionRect(ti, colour);
		}
	}

	if (_zoning.inner != ZEM_NOTHING) {
		const SpriteID colour = TileZoningSpriteEvaluationCached(ti->tile, _local_company, _zoning.inner, true);

		if (colour != ZONING_INVALID_SPRITE_ID) {
			SpriteID sprite = SPR_ZONING_INNER_HIGHLIGHT_BASE;

			if (IsHalftileSlope(ti->tileh)) {
				const int INF = 1000;
				static const SubSprite sub_sprites[4] = {
					{ -INF    , -INF   , 32 - 33, INF     }, // CORNER_W, clip 33 pixels from right
					{ -INF    ,  0 + 22, INF    , INF     }, // CORNER_S, clip 22 pixels from top
					{ -31 + 34, -INF   , INF    , INF     }, // CORNER_E, clip 34 pixels from left
					{ -INF    , -INF   , INF    , 30 - 8  }  // CORNER_N, clip  8 pixels from bottom
				};

				DrawSelectionSprite(sprite, colour, ti, 7 + TILE_HEIGHT, FOUNDATION_PART_HALFTILE, &(sub_sprites[GetHalftileSlopeCorner(ti->tileh)]));
			} else {
				sprite += SlopeToSpriteOffset(ti->tileh);
			}
			DrawSelectionSprite(sprite, colour, ti, 7, FOUNDATION_PART_NORMAL);
		}
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
void ZoningMarkDirtyStationCoverageArea(const Station *st, ZoningModeMask mask)
{
	if (st->rect.IsEmpty()) return;

	uint outer_radius = mask & ZMM_OUTER ? GetZoningModeDependantStationCoverageRadius(st, _zoning.outer) : 0;
	uint inner_radius = mask & ZMM_INNER ? GetZoningModeDependantStationCoverageRadius(st, _zoning.inner) : 0;
	uint radius = max<uint>(outer_radius, inner_radius);

	if (radius > 0) {
		Rect rect = st->GetCatchmentRectUsingRadius(radius);
		for (int y = rect.top; y <= rect.bottom; y++) {
			for (int x = rect.left; x <= rect.right; x++) {
				MarkTileDirtyByTile(TileXY(x, y));
			}
		}
		auto invalidate_cache_rect = [&](btree::btree_set<uint32> &cache) {
			for (int y = rect.top; y <= rect.bottom; y++) {
				auto iter = cache.lower_bound(TileXY(rect.left, y) << 3);
				auto end_iter = iter;
				uint end = TileXY(rect.right, y) << 3;
				while (end_iter != cache.end() && *end_iter < end) ++end_iter;
				cache.erase(iter, end_iter);
			}
		};
		if (outer_radius) invalidate_cache_rect(_zoning_cache_outer);
		if (inner_radius) invalidate_cache_rect(_zoning_cache_inner);
	}
}

void ZoningStationWindowOpenClose(const Station *st)
{
	ZoningModeMask mask = ZMM_NOTHING;
	if (_zoning.inner == ZEM_STA_CATCH_WIN) mask |= ZMM_INNER;
	if (_zoning.outer == ZEM_STA_CATCH_WIN) mask |= ZMM_OUTER;
	if (mask != ZMM_NOTHING) ZoningMarkDirtyStationCoverageArea(st, mask);
}

void ZoningTownAuthorityRatingChange()
{
	ZoningModeMask mask = ZMM_NOTHING;
	if (_zoning.inner == ZEM_AUTHORITY) mask |= ZMM_INNER;
	if (_zoning.outer == ZEM_AUTHORITY) mask |= ZMM_OUTER;
	if (mask != ZMM_NOTHING) {
		MarkWholeScreenDirty();
	}
}

void ClearZoningCaches()
{
	_zoning_cache_inner.clear();
	_zoning_cache_outer.clear();
}

void SetZoningMode(bool inner, ZoningEvaluationMode mode)
{
	ZoningEvaluationMode &current_mode = inner ? _zoning.inner : _zoning.outer;
	btree::btree_set<uint32> &cache = inner ? _zoning_cache_inner : _zoning_cache_outer;

	if (current_mode == mode) return;

	current_mode = mode;
	cache.clear();
	MarkWholeScreenDirty();
}
