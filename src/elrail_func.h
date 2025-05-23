/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file elrail_func.h header file for electrified rail specific functions */

#ifndef ELRAIL_FUNC_H
#define ELRAIL_FUNC_H

#include "rail.h"
#include "tile_cmd.h"
#include "transparency.h"

/**
 * Test if a rail type has catenary
 * @param rt Rail type to test
 */
inline bool HasRailCatenary(RailType rt)
{
	return GetRailTypeInfo(rt)->flags.Test(RailTypeFlag::Catenary);
}

/**
 * Test if we should draw rail catenary
 * @param rt Rail type to test
 */
inline bool HasRailCatenaryDrawn(RailType rt, RailType secondary = INVALID_RAILTYPE)
{
	return !IsInvisibilitySet(TO_CATENARY) && !_settings_game.vehicle.disable_elrails && (HasRailCatenary(rt) || (secondary != INVALID_RAILTYPE && HasRailCatenary(secondary)));
}

void DrawRailCatenary(const TileInfo *ti);
void DrawRailCatenaryOnTunnel(const TileInfo *ti);
void DrawRailCatenaryOnBridge(const TileInfo *ti);

void SettingsDisableElrail(int32_t new_value); ///< _settings_game.disable_elrail callback
void UpdateDisableElrailSettingState(bool disable, bool update_vehicles);

#endif /* ELRAIL_FUNC_H */
