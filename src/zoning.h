/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file zoning.h */

#ifndef ZONING_H
#define ZONING_H

#include "tile_cmd.h"
#include "company_type.h"

/**
 * Zoning evaluation modes
 */
enum ZoningEvaluationMode : uint8 {
	ZEM_NOTHING = 0,   ///< No zoning action selected
	ZEM_AUTHORITY,     ///< Check the local authority's opinion.
	ZEM_CAN_BUILD,     ///< Check wither or not the player can build.
	ZEM_STA_CATCH,     ///< Check catchment area for stations
	ZEM_STA_CATCH_WIN, ///< Check catchment area for stations with their station windows open
	ZEM_BUL_UNSER,     ///< Check for unserved buildings
	ZEM_IND_UNSER,     ///< Check for unserved industries
	ZEM_TRACERESTRICT, ///< Check for restricted signals
	ZEM_2x2_GRID,      ///< Show 2x2 town road grid
	ZEM_3x3_GRID,      ///< Show 3x3 town road grid
	ZEM_ONE_WAY_ROAD,  ///< Show one way roads

	ZEM_END,           ///< End marker
};

/**
 * Zoning evaluation modes
 */
enum ZoningModeMask {
	ZMM_NOTHING = 0,   ///< No zoning mask
	ZMM_INNER,         ///< Inner
	ZMM_OUTER,         ///< Outer
	ZMM_ALL = ZMM_INNER | ZMM_OUTER,
};
DECLARE_ENUM_AS_BIT_SET(ZoningModeMask)

/**
 * Global Zoning state structure
 */
struct Zoning {
	ZoningEvaluationMode inner;
	ZoningEvaluationMode outer;
};

extern Zoning _zoning;

SpriteID TileZoningSpriteEvaluation(TileIndex tile, Owner owner, ZoningEvaluationMode ev_mode);

int TileZoningEvaluation(TileIndex tile, Owner owner, ZoningEvaluationMode ev_mode);

void DrawTileZoning(const TileInfo *ti);

void ShowZoningToolbar();

void ZoningMarkDirtyStationCoverageArea(const Station *st, ZoningModeMask mask = ZMM_ALL);
inline void ZoningMarkDirtyStationCoverageArea(const Waypoint *st) { } // no-op

void ZoningStationWindowOpenClose(const Station *st);
void ZoningTownAuthorityRatingChange();

void SetZoningMode(bool inner, ZoningEvaluationMode mode);
void PostZoningModeChange();

void ClearZoningCaches();

#endif /* ZONING_H */
