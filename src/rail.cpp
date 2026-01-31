/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file rail.cpp Implementation of rail specific functions. */

#include "stdafx.h"
#include "station_map.h"
#include "tunnelbridge_map.h"
#include "date_func.h"
#include "company_func.h"
#include "company_base.h"
#include "engine_base.h"

#include "table/track_data.h"

#include "safeguards.h"

/**
 * Return the rail type of tile, or INVALID_RAILTYPE if this is no rail tile.
 */
RailType GetTileRailType(TileIndex tile)
{
	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			return GetRailType(tile);

		case MP_ROAD:
			/* rail/road crossing */
			if (IsLevelCrossing(tile)) return GetRailType(tile);
			break;

		case MP_STATION:
			if (HasStationRail(tile)) return GetRailType(tile);
			break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) return GetRailType(tile);
			break;

		default:
			break;
	}
	return INVALID_RAILTYPE;
}

/**
 * Return the rail type of tile and track piece, or INVALID_RAILTYPE if this is no rail tile and return_invalid is true.
 */
RailType GenericGetRailTypeByTrack(TileIndex t, Track track, bool return_invalid)
{
	if (IsPlainRailTile(t)) {
		TrackBits bits = GetTrackBits(t);
		if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) {
			return (TrackToTrackBits(track) & TRACK_BIT_RT_1) ? GetRailType(t) : GetSecondaryRailType(t);
		} else {
			return GetRailType(t);
		}
	} else if (IsRailTunnelBridgeTile(t)) {
		TrackBits bits = GetTunnelBridgeTrackBits(t);
		if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) {
			return (TrackToTrackBits(track) & GetAcrossBridgePossibleTrackBits(t)) ? GetRailType(t) : GetSecondaryRailType(t);
		} else {
			return GetRailType(t);
		}
	} else {
		return return_invalid ? GetTileRailType(t) : GetRailType(t);
	}
}

/**
 * Return the rail type of tile and track piece, or INVALID_RAILTYPE if this is no rail tile and return_invalid is true.
 */
RailType GenericGetRailTypeByTrackBit(TileIndex t, TrackBits tb, bool return_invalid)
{
	if (IsPlainRailTile(t)) {
		TrackBits bits = GetTrackBits(t);
		if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) {
			return (tb & TRACK_BIT_RT_1) ? GetRailType(t) : GetSecondaryRailType(t);
		} else {
			return GetRailType(t);
		}
	} else if (IsRailTunnelBridgeTile(t)) {
		TrackBits bits = GetTunnelBridgeTrackBits(t);
		if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) {
			return (tb & (GetAcrossBridgePossibleTrackBits(t) | TRACK_BIT_WORMHOLE)) ? GetRailType(t) : GetSecondaryRailType(t);
		} else {
			return GetRailType(t);
		}
	} else {
		return return_invalid ? GetTileRailType(t) : GetRailType(t);
	}
}

/**
 * Return the rail type of tile and entrance direction, or INVALID_RAILTYPE if this is no rail tile and return_invalid is true.
 */
RailType GenericGetRailTypeByEntryDir(TileIndex t, DiagDirection enterdir, bool return_invalid)
{
	if (IsPlainRailTile(t)) {
		TrackBits bits = GetTrackBits(t);
		if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) {
			return (bits & DiagdirReachesTracks(enterdir) & TRACK_BIT_RT_1) ? GetRailType(t) : GetSecondaryRailType(t);
		} else {
			return GetRailType(t);
		}
	} else if (IsRailTunnelBridgeTile(t)) {
		TrackBits bits = GetTunnelBridgeTrackBits(t);
		if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) {
			return (bits & DiagdirReachesTracks(enterdir) & GetAcrossBridgePossibleTrackBits(t)) ? GetRailType(t) : GetSecondaryRailType(t);
		} else {
			return GetRailType(t);
		}
	} else {
		return return_invalid ? GetTileRailType(t) : GetRailType(t);
	}
}

/**
 * Return the secondary rail type of tile, or INVALID_RAILTYPE if this tile has no secondary rail type
 */
RailType GetTileSecondaryRailTypeIfValid(TileIndex t)
{
	if (IsPlainRailTile(t)) {
		TrackBits bits = GetTrackBits(t);
		if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) {
			return GetSecondaryRailType(t);
		} else {
			return INVALID_RAILTYPE;
		}
	} else if (IsRailTunnelBridgeTile(t)) {
		TrackBits bits = GetTunnelBridgeTrackBits(t);
		if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) {
			return GetSecondaryRailType(t);
		} else {
			return INVALID_RAILTYPE;
		}
	} else {
		return INVALID_RAILTYPE;
	}
}

/**
 * Finds out if a company has a certain buildable railtype available.
 * @param company the company in question
 * @param railtype requested RailType
 * @return true if company has requested RailType available
 */
bool HasRailTypeAvail(const CompanyID company, const RailType railtype)
{
	return !_railtypes_hidden_mask.Test(railtype) && Company::Get(company)->avail_railtypes.Test(railtype);
}

/**
 * Test if any buildable railtype is available for a company.
 * @param company the company in question
 * @return true if company has any RailTypes available
 */
bool HasAnyRailTypesAvail(const CompanyID company)
{
	RailTypes avail = Company::Get(company)->avail_railtypes;
	avail.Reset(_railtypes_hidden_mask);
	return avail.Any();
}

/**
 * Validate functions for rail building.
 * @param rail the railtype to check.
 * @return true if the current company may build the rail.
 */
bool ValParamRailType(const RailType rail)
{
	return rail < RAILTYPE_END && HasRailTypeAvail(_current_company, rail);
}

/**
 * Add the rail types that are to be introduced at the given date.
 * @param current The currently available railtypes.
 * @param date    The date for the introduction comparisons.
 * @return The rail types that should be available when date
 *         introduced rail types are taken into account as well.
 */
RailTypes AddDateIntroducedRailTypes(RailTypes current, CalTime::Date date)
{
	RailTypes rts = current;

	if (_settings_game.vehicle.no_introduce_vehicles_after > 0) {
		date = std::min<CalTime::Date>(date, CalTime::ConvertYMDToDate(_settings_game.vehicle.no_introduce_vehicles_after, 0, 1) - 1);
	}

	for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
		const RailTypeInfo *rti = GetRailTypeInfo(rt);
		/* Unused rail type. */
		if (rti->label == 0) continue;

		/* Not date introduced. */
		if (!IsInsideMM(rti->introduction_date, 0, CalTime::MAX_DATE.base())) continue;

		/* Not yet introduced at this date. */
		if (rti->introduction_date > date) continue;

		/* Have we introduced all required railtypes? */
		RailTypes required = rti->introduction_required_railtypes;
		if (!rts.All(required)) continue;

		rts.Set(rti->introduces_railtypes);
	}

	/* When we added railtypes we need to run this method again; the added
	 * railtypes might enable more rail types to become introduced. */
	return rts == current ? rts : AddDateIntroducedRailTypes(rts, date);
}

/**
 * Get the rail types the given company can build.
 * @param company the company to get the rail types for.
 * @param introduces If true, include rail types introduced by other rail types
 * @return the rail types.
 */
RailTypes GetCompanyRailTypes(CompanyID company, bool introduces)
{
	RailTypes rts{};

	CalTime::Date date = CalTime::CurDate();
	if (_settings_game.vehicle.no_introduce_vehicles_after > 0) {
		date = std::min<CalTime::Date>(date, CalTime::ConvertYMDToDate(_settings_game.vehicle.no_introduce_vehicles_after, 0, 1) - 1);
	}

	for (const Engine *e : Engine::IterateType(VEH_TRAIN)) {
		const EngineInfo *ei = &e->info;

		if (ei->climates.Test(_settings_game.game_creation.landscape) &&
				(e->company_avail.Test(company) || date >= e->intro_date + DAYS_IN_YEAR)) {
			const RailVehicleInfo *rvi = &e->VehInfo<RailVehicleInfo>();

			if (rvi->railveh_type != RAILVEH_WAGON) {
				assert(rvi->railtypes.Any());
				if (introduces) {
					rts.Set(GetAllIntroducesRailTypes(rvi->railtypes));
				} else {
					rts.Set(rvi->railtypes);
				}
			}
		}
	}

	if (introduces) return AddDateIntroducedRailTypes(rts, CalTime::CurDate());
	return rts;
}

/**
 * Get list of rail types, regardless of company availability.
 * @param introduces If true, include rail types introduced by other rail types
 * @return the rail types.
 */
RailTypes GetRailTypes(bool introduces)
{
	RailTypes rts{};

	for (const Engine *e : Engine::IterateType(VEH_TRAIN)) {
		const EngineInfo *ei = &e->info;
		if (!ei->climates.Test(_settings_game.game_creation.landscape)) continue;

		const RailVehicleInfo *rvi = &e->VehInfo<RailVehicleInfo>();
		if (rvi->railveh_type != RAILVEH_WAGON) {
			assert(rvi->railtypes.Any());
			if (introduces) {
				rts.Set(GetAllIntroducesRailTypes(rvi->railtypes));
			} else {
				rts.Set(rvi->railtypes);
			}
		}
	}

	if (introduces) return AddDateIntroducedRailTypes(rts, CalTime::MAX_DATE);
	return rts;
}

/**
 * Get the rail type for a given label.
 * @param label the railtype label.
 * @param allow_alternate_labels Search in the alternate label lists as well.
 * @return the railtype.
 */
RailType GetRailTypeByLabel(RailTypeLabel label, bool allow_alternate_labels)
{
	if (label == 0) return INVALID_RAILTYPE;

	/* Loop through each rail type until the label is found */
	for (RailType r = RAILTYPE_BEGIN; r != RAILTYPE_END; r++) {
		const RailTypeInfo *rti = GetRailTypeInfo(r);
		if (rti->label == label) return r;
	}

	if (allow_alternate_labels) {
		/* Test if any rail type defines the label as an alternate. */
		for (RailType r = RAILTYPE_BEGIN; r != RAILTYPE_END; r++) {
			const RailTypeInfo *rti = GetRailTypeInfo(r);
			if (std::ranges::find(rti->alternate_labels, label) != rti->alternate_labels.end()) return r;
		}
	}

	/* No matching label was found, so it is invalid */
	return INVALID_RAILTYPE;
}
