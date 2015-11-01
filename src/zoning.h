/* $Id$ */

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
enum ZoningEvaluationMode {
	ZEM_NOTHING = 0,   ///< No zoning action selected
	ZEM_AUTHORITY,     ///< Check the local authority's opinion.
	ZEM_CAN_BUILD,     ///< Check wither or not the player can build.
	ZEM_STA_CATCH,     ///< Check catchment area for stations
	ZEM_BUL_UNSER,     ///< Check for unserved buildings
	ZEM_IND_UNSER,     ///< Check for unserved industries
};

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

void ZoningMarkDirtyStationCoverageArea(const Station *st);
inline void ZoningMarkDirtyStationCoverageArea(const Waypoint *st) { } // no-op

#endif /* ZONING_H */
