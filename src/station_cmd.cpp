/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_cmd.cpp Handling of station tiles. */

#include "stdafx.h"
#include "aircraft.h"
#include "bridge_map.h"
#include "viewport_func.h"
#include "viewport_kdtree.h"
#include "command_func.h"
#include "town.h"
#include "news_func.h"
#include "train.h"
#include "ship.h"
#include "roadveh.h"
#include "industry.h"
#include "newgrf_cargo.h"
#include "newgrf_debug.h"
#include "newgrf_station.h"
#include "newgrf_canal.h" /* For the buoy */
#include "pathfinder/yapf/yapf_cache.h"
#include "road_internal.h" /* For drawing catenary/checking road removal */
#include "autoslope.h"
#include "water.h"
#include "strings_func.h"
#include "clear_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "string_func.h"
#include "animated_tile_func.h"
#include "elrail_func.h"
#include "station_base.h"
#include "station_cmd.h"
#include "station_func.h"
#include "station_kdtree.h"
#include "roadstop_base.h"
#include "newgrf_railtype.h"
#include "newgrf_roadtype.h"
#include "waypoint_base.h"
#include "waypoint_cmd.h"
#include "waypoint_func.h"
#include "pbs.h"
#include "debug.h"
#include "core/random_func.hpp"
#include "core/container_func.hpp"
#include "company_base.h"
#include "table/airporttile_ids.h"
#include "newgrf_airporttiles.h"
#include "order_backup.h"
#include "newgrf_house.h"
#include "company_gui.h"
#include "linkgraph/linkgraph_base.h"
#include "linkgraph/refresh.h"
#include "zoning.h"
#include "tunnelbridge_map.h"
#include "cheat_type.h"
#include "newgrf_roadstop.h"
#include "core/math_func.hpp"
#include "landscape_cmd.h"
#include "rail_cmd.h"

#include "widgets/station_widget.h"

#include "table/strings.h"

#include "3rdparty/cpp-btree/btree_set.h"
#include "3rdparty/robin_hood/robin_hood.h"

#include <bitset>

#include "safeguards.h"

static StationSpec::TileFlags GetStationTileFlags(StationGfx gfx, const StationSpec *statspec);

bool _town_noise_no_update = false;

/**
 * Check whether the given tile is a hangar.
 * @param t the tile to of whether it is a hangar.
 * @pre IsTileType(t, MP_STATION)
 * @return true if and only if the tile is a hangar.
 */
bool IsHangar(TileIndex t)
{
	assert_tile(IsTileType(t, MP_STATION), t);

	/* If the tile isn't an airport there's no chance it's a hangar. */
	if (!IsAirport(t)) return false;

	const Station *st = Station::GetByTile(t);
	const AirportSpec *as = st->airport.GetSpec();

	for (const auto &depot : as->depots) {
		if (st->airport.GetRotatedTileFromOffset(depot.ti) == TileIndex(t)) return true;
	}

	return false;
}

/**
 * Look for a station owned by the given company around the given tile area.
 * @param ta the area to search over
 * @param closest_station the closest owned station found so far
 * @param company the company whose stations to look for
 * @param st to 'return' the found station
 * @return Succeeded command (if zero or one station found) or failed command (for two or more stations found).
 */
template <class T, class F>
CommandCost GetStationAround(TileArea ta, StationID closest_station, CompanyID company, T **st, F filter)
{
	ta.Expand(1);

	/* check around to see if there are any stations there owned by the company */
	for (TileIndex tile_cur : ta) {
		if (IsTileType(tile_cur, MP_STATION)) {
			StationID t = GetStationIndex(tile_cur);
			if (!T::IsValidID(t) || T::Get(t)->owner != company || !filter(T::Get(t))) continue;
			if (closest_station == INVALID_STATION) {
				closest_station = t;
			} else if (closest_station != t) {
				return CommandCost(STR_ERROR_ADJOINS_MORE_THAN_ONE_EXISTING);
			}
		}
	}
	*st = (closest_station == INVALID_STATION) ? nullptr : T::Get(closest_station);
	return CommandCost();
}

/**
 * Function to check whether the given tile matches some criterion.
 * @param tile the tile to check
 * @return true if it matches, false otherwise
 */
typedef bool (*CMSAMatcher)(TileIndex tile);

/**
 * Counts the numbers of tiles matching a specific type in the area around
 * @param tile the center tile of the 'count area'
 * @param cmp the comparator/matcher (@see CMSAMatcher)
 * @return the number of matching tiles around
 */
static int CountMapSquareAround(TileIndex tile, CMSAMatcher cmp)
{
	int num = 0;

	for (int dx = -3; dx <= 3; dx++) {
		for (int dy = -3; dy <= 3; dy++) {
			TileIndex t = TileAddWrap(tile, dx, dy);
			if (t != INVALID_TILE && cmp(t)) num++;
		}
	}

	return num;
}

/**
 * Check whether the tile is a mine.
 * @param tile the tile to investigate.
 * @return true if and only if the tile is a mine
 */
static bool CMSAMine(TileIndex tile)
{
	/* No industry */
	if (!IsTileType(tile, MP_INDUSTRY)) return false;

	const Industry *ind = Industry::GetByTile(tile);

	/* No extractive industry */
	if (!GetIndustrySpec(ind->type)->life_type.Test(IndustryLifeType::Extractive)) return false;

	for (const auto &p : ind->Produced()) {
		/* The industry extracts something non-liquid, i.e. no oil or plastic, so it is a mine.
		 * Also the production of passengers and mail is ignored. */
		if (IsValidCargoType(p.cargo) &&
				!CargoSpec::Get(p.cargo)->classes.Any({CargoClass::Liquid, CargoClass::Passengers, CargoClass::Mail})) {
			return true;
		}
	}

	return false;
}

/**
 * Check whether the tile is water.
 * @param tile the tile to investigate.
 * @return true if and only if the tile is a water tile
 */
static bool CMSAWater(TileIndex tile)
{
	return IsTileType(tile, MP_WATER) && IsWater(tile);
}

/**
 * Check whether the tile is a tree.
 * @param tile the tile to investigate.
 * @return true if and only if the tile is a tree tile
 */
static bool CMSATree(TileIndex tile)
{
	return IsTileType(tile, MP_TREES);
}

#define M(x) ((x) - STR_SV_STNAME)

enum StationNaming : uint8_t {
	STATIONNAMING_RAIL,
	STATIONNAMING_ROAD,
	STATIONNAMING_AIRPORT,
	STATIONNAMING_OILRIG,
	STATIONNAMING_DOCK,
	STATIONNAMING_HELIPORT,
};

/** Information to handle station action 0 property 24 correctly */
struct StationNameInformation {
	uint32_t free_names; ///< Current bitset of free names (we can remove names).
	std::bitset<NUM_INDUSTRYTYPES> indtypes; ///< Bit set indicating when an industry type has been found.
};

/**
 * Find a station action 0 property 24 station name, or reduce the
 * free_names if needed.
 * @param tile the tile to search
 * @param user_data the StationNameInformation to base the search on
 * @return true if the tile contains an industry that has not given
 *              its name to one of the other stations in town.
 */
static bool FindNearIndustryName(TileIndex tile, void *user_data)
{
	/* All already found industry types */
	StationNameInformation *sni = (StationNameInformation*)user_data;
	if (!IsTileType(tile, MP_INDUSTRY)) return false;

	/* If the station name is undefined it means that it doesn't name a station */
	IndustryType indtype = GetIndustryType(tile);
	if (GetIndustrySpec(indtype)->station_name == STR_UNDEFINED) return false;

	/* In all cases if an industry that provides a name is found two of
	 * the standard names will be disabled. */
	sni->free_names &= ~(1 << M(STR_SV_STNAME_OILFIELD) | 1 << M(STR_SV_STNAME_MINES));
	return !sni->indtypes[indtype];
}

static StringID GenerateStationName(Station *st, TileIndex tile, StationNaming name_class, bool force_change = false)
{
	static const uint32_t _gen_station_name_bits[] = {
		0,                                       // STATIONNAMING_RAIL
		0,                                       // STATIONNAMING_ROAD
		1U << M(STR_SV_STNAME_AIRPORT),          // STATIONNAMING_AIRPORT
		1U << M(STR_SV_STNAME_OILFIELD),         // STATIONNAMING_OILRIG
		1U << M(STR_SV_STNAME_DOCKS),            // STATIONNAMING_DOCK
		1U << M(STR_SV_STNAME_HELIPORT),         // STATIONNAMING_HELIPORT
	};

	const Town *t = st->town;

	StationNameInformation sni{};
	sni.free_names = UINT32_MAX;

	std::bitset<MAX_EXTRA_STATION_NAMES> extra_names;

	for (const Station *s : Station::Iterate()) {
		if ((force_change || s != st) && s->town == t) {
			if (s->indtype != IT_INVALID) {
				sni.indtypes[s->indtype] = true;
				StringID name = GetIndustrySpec(s->indtype)->station_name;
				if (name != STR_UNDEFINED) {
					/* Filter for other industrytypes with the same name */
					for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
						const IndustrySpec *indsp = GetIndustrySpec(it);
						if (indsp->enabled && indsp->station_name == name) sni.indtypes[it] = true;
					}
				}
				continue;
			}
			if (s->extra_name_index < MAX_EXTRA_STATION_NAMES) {
				extra_names.set(s->extra_name_index);
			}
			uint str = M(s->string_id);
			if (str <= 0x20) {
				if (str == M(STR_SV_STNAME_FOREST)) {
					str = M(STR_SV_STNAME_WOODS);
				}
				ClrBit(sni.free_names, str);
			}
		}
	}

	st->extra_name_index = UINT16_MAX;

	TileIndex indtile = tile;
	if (CircularTileSearch(&indtile, 7, FindNearIndustryName, &sni)) {
		/* An industry has been found nearby */
		IndustryType indtype = GetIndustryType(indtile);
		const IndustrySpec *indsp = GetIndustrySpec(indtype);
		/* STR_NULL means it only disables oil rig/mines */
		if (indsp->station_name != STR_NULL) {
			st->indtype = indtype;
			return STR_SV_STNAME_FALLBACK;
		}
	}

	/* Oil rigs/mines name could be marked not free by looking for a near by industry. */

	/* check default names */
	uint32_t tmp = sni.free_names & _gen_station_name_bits[name_class];
	if (tmp != 0) return STR_SV_STNAME + FindFirstBit(tmp);

	/* check mine? */
	if (HasBit(sni.free_names, M(STR_SV_STNAME_MINES))) {
		if (CountMapSquareAround(tile, CMSAMine) >= 2) {
			return STR_SV_STNAME_MINES;
		}
	}

	/* check close enough to town to get central as name? */
	const bool is_central = DistanceMax(tile, t->xy) < 8;
	if (HasBit(sni.free_names, M(STR_SV_STNAME)) && (is_central ||
			DistanceSquare(tile, t->xy) <= std::max(t->cache.squared_town_zone_radius[HZB_TOWN_INNER_SUBURB], t->cache.squared_town_zone_radius[HZB_TOWN_OUTER_SUBURB]))) {
		return STR_SV_STNAME;
	}

	bool use_extra_names = !_extra_station_names.empty();
	auto check_extra_names = [&]() -> bool {
		if (use_extra_names) {
			use_extra_names = false;
			const bool near_water = CountMapSquareAround(tile, CMSAWater) >= 5;
			std::vector<uint16_t> candidates;
			for (size_t i = 0; i < _extra_station_names.size(); i++) {
				const ExtraStationNameInfo &info = _extra_station_names[i];
				if (extra_names[i]) continue;
				if (!HasBit(info.flags, name_class)) continue;
				if (HasBit(info.flags, ESNIF_CENTRAL) && !is_central) continue;
				if (HasBit(info.flags, ESNIF_NOT_CENTRAL) && is_central) continue;
				if (HasBit(info.flags, ESNIF_NEAR_WATER) && !near_water) continue;
				if (HasBit(info.flags, ESNIF_NOT_NEAR_WATER) && near_water) continue;
				candidates.push_back(static_cast<uint16_t>(i));
			}

			if (!candidates.empty()) {
				SavedRandomSeeds saved_seeds;
				SaveRandomSeeds(&saved_seeds);
				st->extra_name_index = candidates[RandomRange((uint)candidates.size())];
				RestoreRandomSeeds(saved_seeds);
				return true;
			}
		}
		return false;
	};

	if (_extra_station_names_probability > 0) {
		SavedRandomSeeds saved_seeds;
		SaveRandomSeeds(&saved_seeds);
		bool extra_name = (RandomRange(0xFF) < _extra_station_names_probability) && check_extra_names();
		RestoreRandomSeeds(saved_seeds);
		if (extra_name) return STR_SV_STNAME_FALLBACK;
	}

	/* check close enough to town to get central as name? */
	if (is_central && HasBit(sni.free_names, M(STR_SV_STNAME_CENTRAL))) {
		return STR_SV_STNAME_CENTRAL;
	}

	/* Check lakeside */
	if (HasBit(sni.free_names, M(STR_SV_STNAME_LAKESIDE)) &&
			DistanceFromEdge(tile) < 20 &&
			CountMapSquareAround(tile, CMSAWater) >= 5) {
		return STR_SV_STNAME_LAKESIDE;
	}

	/* Check woods */
	if (HasBit(sni.free_names, M(STR_SV_STNAME_WOODS)) && (
				CountMapSquareAround(tile, CMSATree) >= 8 ||
				CountMapSquareAround(tile, IsTileForestIndustry) >= 2)
			) {
		return _settings_game.game_creation.landscape == LandscapeType::Tropic ? STR_SV_STNAME_FOREST : STR_SV_STNAME_WOODS;
	}

	/* check elevation compared to town */
	int z = GetTileZ(tile);
	int z2 = GetTileZ(t->xy);
	if (z < z2) {
		if (HasBit(sni.free_names, M(STR_SV_STNAME_VALLEY))) return STR_SV_STNAME_VALLEY;
	} else if (z > z2) {
		if (HasBit(sni.free_names, M(STR_SV_STNAME_HEIGHTS))) return STR_SV_STNAME_HEIGHTS;
	}

	/* check direction compared to town */
	static const int8_t _direction_and_table[] = {
		~( (1 << M(STR_SV_STNAME_WEST))  | (1 << M(STR_SV_STNAME_EAST)) | (1 << M(STR_SV_STNAME_NORTH)) ),
		~( (1 << M(STR_SV_STNAME_SOUTH)) | (1 << M(STR_SV_STNAME_WEST)) | (1 << M(STR_SV_STNAME_NORTH)) ),
		~( (1 << M(STR_SV_STNAME_SOUTH)) | (1 << M(STR_SV_STNAME_EAST)) | (1 << M(STR_SV_STNAME_NORTH)) ),
		~( (1 << M(STR_SV_STNAME_SOUTH)) | (1 << M(STR_SV_STNAME_WEST)) | (1 << M(STR_SV_STNAME_EAST)) ),
	};

	sni.free_names &= _direction_and_table[
		(TileX(tile) < TileX(t->xy)) +
		(TileY(tile) < TileY(t->xy)) * 2];

	/** Bitmask of remaining station names that can be used when a more specific name has not been used. */
	static const uint32_t fallback_names = (
		(1U << M(STR_SV_STNAME_NORTH)) |
		(1U << M(STR_SV_STNAME_SOUTH)) |
		(1U << M(STR_SV_STNAME_EAST)) |
		(1U << M(STR_SV_STNAME_WEST)) |
		(1U << M(STR_SV_STNAME_TRANSFER)) |
		(1U << M(STR_SV_STNAME_HALT)) |
		(1U << M(STR_SV_STNAME_EXCHANGE)) |
		(1U << M(STR_SV_STNAME_ANNEXE)) |
		(1U << M(STR_SV_STNAME_SIDINGS)) |
		(1U << M(STR_SV_STNAME_BRANCH)) |
		(1U << M(STR_SV_STNAME_UPPER)) |
		(1U << M(STR_SV_STNAME_LOWER))
	);

	sni.free_names &= fallback_names;
	if (sni.free_names != 0) return STR_SV_STNAME + FindFirstBit(sni.free_names);

	if (check_extra_names()) return STR_SV_STNAME_FALLBACK;

	return STR_SV_STNAME_FALLBACK;
}
#undef M

/**
 * Find the closest deleted station of the current company
 * @param tile the tile to search from.
 * @return the closest station or nullptr if too far.
 */
static Station *GetClosestDeletedStation(TileIndex tile)
{
	uint threshold = 8;

	Station *best_station = nullptr;
	ForAllStationsRadius(tile, threshold, [&](Station *st) {
		if (!st->IsInUse() && st->owner == _current_company) {
			uint cur_dist = DistanceManhattan(tile, st->xy);

			if (cur_dist < threshold) {
				threshold = cur_dist;
				best_station = st;
			} else if (cur_dist == threshold && best_station != nullptr) {
				/* In case of a tie, lowest station ID wins */
				if (st->index < best_station->index) best_station = st;
			}
		}
	});

	return best_station;
}


void Station::GetTileArea(TileArea *ta, StationType type) const
{
	switch (type) {
		case StationType::Rail:
			*ta = this->train_station;
			return;

		case StationType::Airport:
			*ta = this->airport;
			return;

		case StationType::Truck:
			*ta = this->truck_station;
			return;

		case StationType::Bus:
			*ta = this->bus_station;
			return;

		case StationType::Dock:
		case StationType::Oilrig:
			*ta = this->docking_station;
			return;

		default: NOT_REACHED();
	}
}

/**
 * Update the cargo history.
 */
void Station::UpdateCargoHistory()
{
	uint storage_offset = 0;
	bool update_window = false;
	for (const CargoSpec *cs : CargoSpec::Iterate()) {
		uint amount = this->goods[cs->Index()].CargoTotalCount();
		if (!HasBit(this->station_cargo_history_cargoes, cs->Index())) {
			if (amount == 0) {
				/* No cargo present, and no history stored for this cargo, no work to do */
				continue;
			} else {
				if (this->station_cargo_history_cargoes == 0) update_window = true;
				SetBit(this->station_cargo_history_cargoes, cs->Index());
				this->station_cargo_history.emplace(this->station_cargo_history.begin() + storage_offset);
			}
		}
		this->station_cargo_history[storage_offset][this->station_cargo_history_offset] = RXCompressUint(amount);
		storage_offset++;
	}
	this->station_cargo_history_offset++;
	if (this->station_cargo_history_offset == MAX_STATION_CARGO_HISTORY_DAYS) this->station_cargo_history_offset = 0;
	if (update_window) InvalidateWindowData(WC_STATION_VIEW, this->index, -1);
}

/**
 * Update the virtual coords needed to draw the station sign.
 */
void Station::UpdateVirtCoord()
{
	if (IsHeadless()) return;
	Point pt = RemapCoords2(TileX(this->xy) * TILE_SIZE, TileY(this->xy) * TILE_SIZE);

	pt.y -= 32 * ZOOM_BASE;
	if ((this->facilities & FACIL_AIRPORT) && this->airport.type == AT_OILRIG) pt.y -= 16 * ZOOM_BASE;

	if (_viewport_sign_kdtree_valid && this->sign.kdtree_valid) _viewport_sign_kdtree.Remove(ViewportSignKdtreeItem::MakeStation(this->index));

	auto params = MakeParameters(this->index, this->facilities);
	this->sign.UpdatePosition(ShouldShowBaseStationViewportLabel(this) ? ZOOM_LVL_DRAW_SPR : ZOOM_LVL_END, pt.x, pt.y, params, STR_VIEWPORT_STATION, STR_STATION_NAME);

	if (_viewport_sign_kdtree_valid) _viewport_sign_kdtree.Insert(ViewportSignKdtreeItem::MakeStation(this->index));

	SetWindowDirty(WC_STATION_VIEW, this->index);
}

/**
 * Move the station main coordinate somewhere else.
 * @param new_xy new tile location of the sign
 */
void Station::MoveSign(TileIndex new_xy)
{
	if (this->xy == new_xy) return;

	MarkAllViewportOverlayStationLinksDirty(this);

	_station_kdtree.Remove(this->index);

	this->BaseStation::MoveSign(new_xy);

	_station_kdtree.Insert(this->index);

	MarkAllViewportOverlayStationLinksDirty(this);
}

/** Update the virtual coords needed to draw the station sign for all stations. */
void UpdateAllStationVirtCoords()
{
	if (IsHeadless()) return;
	for (BaseStation *st : BaseStation::Iterate()) {
		st->UpdateVirtCoord();
	}
}

void BaseStation::FillCachedName() const
{
	auto tmp_params = MakeParameters(this->index);
	this->cached_name = GetStringWithArgs(Waypoint::IsExpected(this) ? STR_WAYPOINT_NAME : STR_STATION_NAME, tmp_params);
}

void ClearAllStationCachedNames()
{
	for (BaseStation *st : BaseStation::Iterate()) {
		st->cached_name.clear();
	}
}

/**
 * Get a mask of the cargo types that the station accepts.
 * @param st Station to query
 * @return the expected mask
 */
CargoTypes GetAcceptanceMask(const Station *st)
{
	CargoTypes mask = 0;

	for (CargoType i = 0; i < NUM_CARGO; i++) {
		if (HasBit(st->goods[i].status, GoodsEntry::GES_ACCEPTANCE)) SetBit(mask, i);
	}
	return mask;
}

/**
 * Get a mask of the cargo types that are empty at the station.
 * @param st Station to query
 * @return the empty mask
 */
CargoTypes GetEmptyMask(const Station *st)
{
	CargoTypes mask = 0;

	for (CargoType i = 0; i < NUM_CARGO; i++) {
		if (st->goods[i].CargoTotalCount() == 0) SetBit(mask, i);
	}
	return mask;
}

/**
 * Add news item for when a station changes which cargoes it accepts.
 * @param st Station of cargo change.
 * @param cargoes Bit mask of cargo types to list.
 * @param reject True iff the station rejects the cargo types.
 */
static void ShowRejectOrAcceptNews(const Station *st, CargoTypes cargoes, bool reject)
{
	SetDParam(0, st->index);
	SetDParam(1, cargoes);
	StringID msg = reject ? STR_NEWS_STATION_NO_LONGER_ACCEPTS_CARGO_LIST : STR_NEWS_STATION_NOW_ACCEPTS_CARGO_LIST;
	AddNewsItem(msg, NewsType::Acceptance, NewsStyle::Small, NewsFlag::InColour, NewsReferenceType::Station, st->index);
}

/**
 * Get the cargo types being produced around the tile (in a rectangle).
 * @param north_tile Northern most tile of area
 * @param w X extent of the area
 * @param h Y extent of the area
 * @param rad Search radius in addition to the given area
 */
CargoArray GetProductionAroundTiles(TileIndex north_tile, int w, int h, int rad)
{
	CargoArray produced{};

	btree::btree_set<IndustryID> industries;
	TileArea ta = TileArea(north_tile, w, h).Expand(rad);

	/* Loop over all tiles to get the produced cargo of
	 * everything except industries */
	for (TileIndex tile : ta) {
		if (IsTileType(tile, MP_INDUSTRY)) industries.insert(GetIndustryIndex(tile));
		AddProducedCargo(tile, produced);
	}

	/* Loop over the seen industries. They produce cargo for
	 * anything that is within 'rad' of any one of their tiles.
	 */
	for (IndustryID industry : industries) {
		const Industry *i = Industry::Get(industry);
		/* Skip industry with neutral station */
		if (i->neutral_station != nullptr && !_settings_game.station.serve_neutral_industries) continue;

		for (const auto &p : i->Produced()) {
			if (p.cargo != INVALID_CARGO) produced[p.cargo]++;
		}
	}

	return produced;
}

/**
 * Get the acceptance of cargoes around the tile in 1/8.
 * @param center_tile Center of the search area
 * @param w X extent of area
 * @param h Y extent of area
 * @param rad Search radius in addition to given area
 * @param always_accepted bitmask of cargo accepted by houses and headquarters; can be nullptr
 * @param ind Industry associated with neutral station (e.g. oil rig) or nullptr
 */
CargoArray GetAcceptanceAroundTiles(TileIndex center_tile, int w, int h, int rad, CargoTypes *always_accepted)
{
	CargoArray acceptance{};
	if (always_accepted != nullptr) *always_accepted = 0;

	TileArea ta = TileArea(center_tile, w, h).Expand(rad);

	for (TileIndex tile : ta) {
		/* Ignore industry if it has a neutral station. */
		if (!_settings_game.station.serve_neutral_industries && IsTileType(tile, MP_INDUSTRY) && Industry::GetByTile(tile)->neutral_station != nullptr) continue;

		AddAcceptedCargo(tile, acceptance, always_accepted);
	}

	return acceptance;
}

/**
 * Get the acceptance of cargoes around the station in.
 * @param st Station to get acceptance of.
 * @param always_accepted bitmask of cargo accepted by houses and headquarters; can be nullptr
 */
static CargoArray GetAcceptanceAroundStation(const Station *st, CargoTypes *always_accepted)
{
	CargoArray acceptance{};
	if (always_accepted != nullptr) *always_accepted = 0;

	BitmapTileIterator it(st->catchment_tiles);
	for (TileIndex tile = it; tile != INVALID_TILE; tile = ++it) {
		AddAcceptedCargo(tile, acceptance, always_accepted);
	}

	return acceptance;
}

/**
 * Update the acceptance for a station.
 * @param st Station to update
 * @param show_msg controls whether to display a message that acceptance was changed.
 */
void UpdateStationAcceptance(Station *st, bool show_msg)
{
	/* old accepted goods types */
	CargoTypes old_acc = GetAcceptanceMask(st);

	/* And retrieve the acceptance. */
	CargoArray acceptance{};
	if (!st->rect.IsEmpty()) {
		acceptance = GetAcceptanceAroundStation(st, &st->always_accepted);
	}

	/* Adjust in case our station only accepts fewer kinds of goods */
	for (CargoType i = 0; i < NUM_CARGO; i++) {
		uint amt = acceptance[i];

		/* Make sure the station can accept the goods type. */
		bool is_passengers = IsCargoInClass(i, CargoClass::Passengers);
		if ((!is_passengers && !(st->facilities & ~FACIL_BUS_STOP)) ||
				(is_passengers && !(st->facilities & ~FACIL_TRUCK_STOP))) {
			amt = 0;
		}

		GoodsEntry &ge = st->goods[i];
		SB(ge.status, GoodsEntry::GES_ACCEPTANCE, 1, amt >= 8);
		if (LinkGraph::IsValidID(ge.link_graph)) {
			(*LinkGraph::Get(ge.link_graph))[ge.node].SetDemand(amt / 8);
		}
	}

	/* Only show a message in case the acceptance was actually changed. */
	CargoTypes new_acc = GetAcceptanceMask(st);
	if (old_acc == new_acc) return;

	/* show a message to report that the acceptance was changed? */
	if (show_msg && st->owner == _local_company && st->IsInUse()) {
		/* Combine old and new masks to get changes */
		CargoTypes accepts = new_acc & ~old_acc;
		CargoTypes rejects = ~new_acc & old_acc;

		/* Show news message if there are any changes */
		if (accepts != 0) ShowRejectOrAcceptNews(st, accepts, false);
		if (rejects != 0) ShowRejectOrAcceptNews(st, rejects, true);
	}

	/* redraw the station view since acceptance changed */
	SetWindowWidgetDirty(WC_STATION_VIEW, st->index, WID_SV_ACCEPT_RATING_LIST);
}

static void UpdateStationSignCoord(BaseStation *st)
{
	const StationRect *r = &st->rect;

	if (r->IsEmpty()) return; // no tiles belong to this station

	/* clamp sign coord to be inside the station rect */
	TileIndex new_xy = TileXY(ClampU(TileX(st->xy), r->left, r->right), ClampU(TileY(st->xy), r->top, r->bottom));
	st->MoveSign(new_xy);

	if (!Station::IsExpected(st)) return;
	Station *full_station = Station::From(st);
	for (const GoodsEntry &ge : full_station->goods) {
		LinkGraphID lg = ge.link_graph;
		if (!LinkGraph::IsValidID(lg)) continue;
		(*LinkGraph::Get(lg))[ge.node].UpdateLocation(st->xy);
	}
}

/**
 * Common part of building various station parts and possibly attaching them to an existing one.
 * @param[in,out] st Station to attach to
 * @param flags Command flags
 * @param reuse Whether to try to reuse a deleted station (gray sign) if possible
 * @param area Area occupied by the new part
 * @param name_class Station naming class to use to generate the new station's name
 * @return Command error that occurred, if any
 */
static CommandCost BuildStationPart(Station **st, DoCommandFlag flags, bool reuse, TileArea area, StationNaming name_class)
{
	/* Find a deleted station close to us */
	if (*st == nullptr && reuse) *st = GetClosestDeletedStation(area.tile);

	if (*st != nullptr) {
		if ((*st)->owner != _current_company) {
			return CommandCost(CMD_ERROR);
		}

		CommandCost ret = (*st)->rect.BeforeAddRect(area.tile, area.w, area.h, StationRect::ADD_TEST);
		if (ret.Failed()) return ret;
	} else {
		/* allocate and initialize new station */
		if (!Station::CanAllocateItem()) return CommandCost(STR_ERROR_TOO_MANY_STATIONS_LOADING);

		if (flags & DC_EXEC) {
			*st = new Station(area.tile);
			_station_kdtree.Insert((*st)->index);

			(*st)->town = ClosestTownFromTile(area.tile, UINT_MAX);
			(*st)->string_id = GenerateStationName(*st, area.tile, name_class);

			if (Company::IsValidID(_current_company)) {
				if (_local_company == _current_company && !(*st)->town->have_ratings.Test(_current_company)) {
					ZoningTownAuthorityRatingChange();
				}
				(*st)->town->have_ratings.Set(_current_company);
				if (_cheats.town_rating.value) {
					(*st)->town->ratings[_current_company] = RATING_MAXIMUM;
				}
			}
		}
	}
	return CommandCost();
}

/**
 * This is called right after a station was deleted.
 * It checks if the whole station is free of substations, and if so, the station will be
 * deleted after a little while.
 * @param st Station
 */
static void DeleteStationIfEmpty(BaseStation *st)
{
	if (!st->IsInUse()) {
		st->delete_ctr = 0;
		InvalidateWindowData(WC_STATION_LIST, st->owner, 0);
	}
	/* station remains but it probably lost some parts - station sign should stay in the station boundaries */
	UpdateStationSignCoord(st);
}

/**
 * After adding/removing tiles to station, update some station-related stuff.
 * @param adding True if adding tiles, false if removing them.
 * @param type StationType being modified.
 */
void Station::AfterStationTileSetChange(bool adding, StationType type)
{
	this->UpdateVirtCoord();
	DirtyCompanyInfrastructureWindows(this->owner);
	if (adding) InvalidateWindowData(WC_STATION_LIST, this->owner, 0);

	switch (type) {
		case StationType::Rail:
			SetWindowWidgetDirty(WC_STATION_VIEW, this->index, WID_SV_TRAINS);
			break;
		case StationType::Airport:
			break;
		case StationType::Truck:
		case StationType::Bus:
			SetWindowWidgetDirty(WC_STATION_VIEW, this->index, WID_SV_ROADVEHS);
			break;
		case StationType::Dock:
			SetWindowWidgetDirty(WC_STATION_VIEW, this->index, WID_SV_SHIPS);
			break;
		default: NOT_REACHED();
	}

	if (adding) {
		this->RecomputeCatchment();
		UpdateStationAcceptance(this, false);
		InvalidateWindowData(WC_SELECT_STATION, 0, 0);
	} else {
		DeleteStationIfEmpty(this);
		this->RecomputeCatchment();
	}

}

CommandCost ClearTile_Station(TileIndex tile, DoCommandFlag flags);

/**
 * Checks if the given tile is buildable, flat and has a certain height.
 * @param tile TileIndex to check.
 * @param invalid_dirs Prohibited directions for slopes (set of #DiagDirection).
 * @param allowed_z Height allowed for the tile. If allowed_z is negative, it will be set to the height of this tile.
 * @param allow_steep Whether steep slopes are allowed.
 * @param check_bridge Check for the existence of a bridge.
 * @return The cost in case of success, or an error code if it failed.
 */
CommandCost CheckBuildableTile(TileIndex tile, uint invalid_dirs, int &allowed_z, bool allow_steep, bool check_bridge)
{
	if (check_bridge && IsBridgeAbove(tile)) {
		return CommandCost(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
	}

	CommandCost ret = EnsureNoVehicleOnGround(tile);
	if (ret.Failed()) return ret;

	auto [tileh, z] = GetTileSlopeZ(tile);

	/* Prohibit building if
	 *   1) The tile is "steep" (i.e. stretches two height levels).
	 *   2) The tile is non-flat and the build_on_slopes switch is disabled.
	 */
	if ((!allow_steep && IsSteepSlope(tileh)) ||
			((!_settings_game.construction.build_on_slopes) && tileh != SLOPE_FLAT)) {
		return CommandCost(STR_ERROR_FLAT_LAND_REQUIRED);
	}

	CommandCost cost(EXPENSES_CONSTRUCTION);
	int flat_z = z + GetSlopeMaxZ(tileh);
	if (tileh != SLOPE_FLAT) {
		/* Forbid building if the tile faces a slope in a invalid direction. */
		for (DiagDirection dir = DIAGDIR_BEGIN; dir != DIAGDIR_END; dir++) {
			if (HasBit(invalid_dirs, dir) && !CanBuildDepotByTileh(dir, tileh)) {
				return CommandCost(STR_ERROR_FLAT_LAND_REQUIRED);
			}
		}
		cost.AddCost(_price[PR_BUILD_FOUNDATION]);
	}

	/* The level of this tile must be equal to allowed_z. */
	if (allowed_z < 0) {
		/* First tile. */
		allowed_z = flat_z;
	} else if (allowed_z != flat_z) {
		return CommandCost(STR_ERROR_FLAT_LAND_REQUIRED);
	}

	return cost;
}

CommandCost IsRailStationBridgeAboveOk(TileIndex tile, const StationSpec *statspec, uint8_t layout, TileIndex northern_bridge_end, TileIndex southern_bridge_end, int bridge_height,
		BridgeType bridge_type, TransportType bridge_transport_type)
{
	if (statspec != nullptr && statspec->internal_flags.Test(StationSpecIntlFlag::BridgeHeightsSet)) {
		int height_above = statspec->GetBridgeAboveFlags(layout).height;
		if (height_above == 0) return CommandCost(INVALID_STRING_ID);
		if (GetTileMaxZ(tile) + height_above > bridge_height) {
			return CommandCost(STR_ERROR_BRIDGE_TOO_LOW_FOR_STATION);
		}
	} else if (!statspec) {
		/* Default stations/waypoints */
		const int height = layout < 4 ? 2 : 5;
		if (GetTileMaxZ(tile) + height > bridge_height) return CommandCost(STR_ERROR_BRIDGE_TOO_LOW_FOR_STATION);
	} else {
		if (!_settings_game.construction.allow_stations_under_bridges) return CommandCost(INVALID_STRING_ID);
	}

	BridgePiecePillarFlags disallowed_pillar_flags;
	if (statspec != nullptr && statspec->internal_flags.Test(StationSpecIntlFlag::BridgeDisallowedPillarsSet)) {
		/* Pillar flags set by NewGRF */
		disallowed_pillar_flags = (BridgePiecePillarFlags) statspec->GetBridgeAboveFlags(layout).disallowed_pillars;
	} else if (!statspec) {
		/* Default stations/waypoints */
		if (layout < 8) {
			static const uint8_t st_flags[8] = { 0x50, 0xA0, 0x50, 0xA0, 0x50 | 0x26, 0xA0 | 0x1C, 0x50 | 0x89, 0xA0 | 0x43 };
			disallowed_pillar_flags = (BridgePiecePillarFlags) st_flags[layout];
		} else {
			disallowed_pillar_flags = (BridgePiecePillarFlags) 0;
		}
	} else if (GetStationTileFlags(layout, statspec).Test(StationSpec::TileFlag::Blocked)) {
		/* Non-track station tiles */
		disallowed_pillar_flags = (BridgePiecePillarFlags) 0;
	} else {
		/* Tracked station tiles */
		const Axis axis = HasBit(layout, 0) ? AXIS_Y : AXIS_X;
		disallowed_pillar_flags = (BridgePiecePillarFlags) (axis == AXIS_X ? 0x50 : 0xA0);
	}

	if ((GetBridgeTilePillarFlags(tile, northern_bridge_end, southern_bridge_end, bridge_type, bridge_transport_type) & disallowed_pillar_flags) == 0) {
		return CommandCost();
	} else {
		return CommandCost(STR_ERROR_BRIDGE_PILLARS_OBSTRUCT_STATION);
	}
}

CommandCost IsRailStationBridgeAboveOk(TileIndex tile, const StationSpec *statspec, uint8_t layout)
{
	if (!IsBridgeAbove(tile)) return CommandCost();

	TileIndex southern_bridge_end = GetSouthernBridgeEnd(tile);
	TileIndex northern_bridge_end = GetNorthernBridgeEnd(tile);
	return IsRailStationBridgeAboveOk(tile, statspec, layout, northern_bridge_end, southern_bridge_end, GetBridgeHeight(southern_bridge_end),
			GetBridgeType(southern_bridge_end), GetTunnelBridgeTransportType(southern_bridge_end));
}

CommandCost IsRoadStopBridgeAboveOK(TileIndex tile, const RoadStopSpec *spec, bool drive_through, DiagDirection entrance,
		TileIndex northern_bridge_end, TileIndex southern_bridge_end, int bridge_height,
		BridgeType bridge_type, TransportType bridge_transport_type)
{
	if (spec != nullptr && spec->internal_flags.Test(RoadStopSpecIntlFlag::BridgeHeightsSet)) {
		int height = spec->bridge_height[drive_through ? (GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET + DiagDirToAxis(entrance)) : entrance];
		if (height == 0) return CommandCost(INVALID_STRING_ID);
		if (GetTileMaxZ(tile) + height > bridge_height) {
			return CommandCost(STR_ERROR_BRIDGE_TOO_LOW_FOR_STATION);
		}
	} else {
		if (!_settings_game.construction.allow_road_stops_under_bridges) return CommandCost(INVALID_STRING_ID);

		if (GetTileMaxZ(tile) + (drive_through ? 1 : 2) > bridge_height) {
			return CommandCost(STR_ERROR_BRIDGE_TOO_LOW_FOR_STATION);
		}
	}

	BridgePiecePillarFlags disallowed_pillar_flags = (BridgePiecePillarFlags) 0;
	if (spec != nullptr && spec->internal_flags.Test(RoadStopSpecIntlFlag::BridgeDisallowedPillarsSet)) {
		disallowed_pillar_flags = (BridgePiecePillarFlags) spec->bridge_disallowed_pillars[drive_through ? (GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET + DiagDirToAxis(entrance)) : entrance];
	} else if (drive_through) {
		disallowed_pillar_flags = (BridgePiecePillarFlags) (DiagDirToAxis(entrance) == AXIS_X ? 0x50 : 0xA0);
	} else {
		SetBit(disallowed_pillar_flags, 4 + entrance);
	}
	if ((GetBridgeTilePillarFlags(tile, northern_bridge_end, southern_bridge_end, bridge_type, bridge_transport_type) & disallowed_pillar_flags) == 0) {
		return CommandCost();
	} else {
		return CommandCost(STR_ERROR_BRIDGE_PILLARS_OBSTRUCT_STATION);
	}
}

/**
 * Checks if a rail station can be built at the given area.
 * @param tile_area Area to check.
 * @param flags Operation to perform.
 * @param axis Rail station axis.
 * @param station StationID to be queried and returned if available.
 * @param rt The rail type to check for (overbuilding rail stations over rail).
 * @param affected_vehicles List of trains with PBS reservations on the tiles
 * @param spec_class Station class.
 * @param spec_index Index into the station class.
 * @param plat_len Platform length.
 * @param numtracks Number of platforms.
 * @return The cost in case of success, or an error code if it failed.
 */
static CommandCost CheckFlatLandRailStation(TileArea tile_area, DoCommandFlag flags, Axis axis, StationID *station, RailType rt, std::vector<Train *> &affected_vehicles, StationClassID spec_class, uint16_t spec_index, uint8_t plat_len, uint8_t numtracks)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);
	int allowed_z = -1;
	uint invalid_dirs = 5 << axis;

	const StationSpec *statspec = StationClass::Get(spec_class)->GetSpec(spec_index);
	bool slope_cb = statspec != nullptr && statspec->callback_mask.Test(StationCallbackMask::SlopeCheck);

	for (TileIndex tile_cur : tile_area) {
		CommandCost ret = CheckBuildableTile(tile_cur, invalid_dirs, allowed_z, false, false);
		if (ret.Failed()) return ret;
		cost.AddCost(ret);

		if (slope_cb) {
			/* Do slope check if requested. */
			ret = PerformStationTileSlopeCheck(tile_area.tile, tile_cur, rt, statspec, axis, plat_len, numtracks);
			if (ret.Failed()) return ret;
		}

		/* if station is set, then we have special handling to allow building on top of already existing stations.
		 * so station points to INVALID_STATION if we can build on any station.
		 * Or it points to a station if we're only allowed to build on exactly that station. */
		if (station != nullptr && IsTileType(tile_cur, MP_STATION)) {
			if (!IsRailStation(tile_cur)) {
				return ClearTile_Station(tile_cur, DC_AUTO); // get error message
			} else {
				StationID st = GetStationIndex(tile_cur);
				if (*station == INVALID_STATION) {
					*station = st;
				} else if (*station != st) {
					return CommandCost(STR_ERROR_ADJOINS_MORE_THAN_ONE_EXISTING);
				}
				if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && HasStationReservation(tile_cur)) {
					CommandCost ret = CheckTrainReservationPreventsTrackModification(tile_cur, GetRailStationTrack(tile_cur));
					if (ret.Failed()) return ret;
				}
			}
		} else {
			/* If we are building a station with a valid railtype, we may be able to overbuild an existing rail tile. */
			if (rt != INVALID_RAILTYPE && IsPlainRailTile(tile_cur)) {
				/* Don't overbuild signals. */
				if (HasSignals(tile_cur)) return CommandCost(STR_ERROR_MUST_REMOVE_SIGNALS_FIRST);

				/* The current rail type must have power on the to-be-built type (e.g. convert normal rail to electrified rail). */
				if (HasPowerOnRail(GetRailType(tile_cur), rt)) {
					TrackBits tracks = GetTrackBits(tile_cur);
					Track track = RemoveFirstTrack(&tracks);
					Track expected_track = HasBit(invalid_dirs, DIAGDIR_NE) ? TRACK_X : TRACK_Y;

					/* The existing track must align with the desired station axis. */
					if (tracks == TRACK_BIT_NONE && track == expected_track) {
						/* Check for trains having a reservation for this tile. */
						if (HasBit(GetRailReservationTrackBits(tile_cur), track)) {
							Train *v = GetTrainForReservation(tile_cur, track);
							if (v != nullptr) {
								CommandCost ret = CheckTrainReservationPreventsTrackModification(v);
								if (ret.Failed()) return ret;
								affected_vehicles.push_back(v);
							}
						}
						CommandCost ret = Command<CMD_REMOVE_SINGLE_RAIL>::Do(flags, tile_cur, track);
						if (ret.Failed()) return ret;
						cost.AddCost(ret);
						/* With flags & ~DC_EXEC CmdLandscapeClear would fail since the rail still exists */
						continue;
					}
				}
			}
			ret = Command<CMD_LANDSCAPE_CLEAR>::Do(flags, tile_cur);
			if (ret.Failed()) return ret;
			cost.AddCost(ret);
		}
	}

	return cost;
}

/**
 * Checks if a road stop can be built at the given tile.
 * @param tile_area Area to check.
 * @param spec Road stop spec.
 * @param flags Operation to perform.
 * @param invalid_dirs Prohibited directions (set of DiagDirections).
 * @param is_drive_through True if trying to build a drive-through station.
 * @param station_type Station type (bus, truck or road waypoint).
 * @param axis Axis of a drive-through road stop.
 * @param station StationID to be queried and returned if available.
 * @param rt Road type to build.
 * @param require_road Is existing road required.
 * @return The cost in case of success, or an error code if it failed.
 */
CommandCost CheckFlatLandRoadStop(TileArea tile_area, const RoadStopSpec *spec, DoCommandFlag flags, uint invalid_dirs, bool is_drive_through, StationType station_type, Axis axis, StationID *station, RoadType rt, bool require_road)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);
	int allowed_z = -1;

	for (TileIndex cur_tile : tile_area) {
		bool allow_under_bridge = _settings_game.construction.allow_road_stops_under_bridges || (spec != nullptr && spec->internal_flags.Test(RoadStopSpecIntlFlag::BridgeHeightsSet));
		CommandCost ret = CheckBuildableTile(cur_tile, invalid_dirs, allowed_z, !is_drive_through, !allow_under_bridge);
		if (ret.Failed()) return ret;
		cost.AddCost(ret);

		if (allow_under_bridge && IsBridgeAbove(cur_tile)) {
			TileIndex southern_bridge_end = GetSouthernBridgeEnd(cur_tile);
			TileIndex northern_bridge_end = GetNorthernBridgeEnd(cur_tile);
			CommandCost bridge_ret = IsRoadStopBridgeAboveOK(cur_tile, spec, is_drive_through, (DiagDirection) FindFirstBit(invalid_dirs),
					northern_bridge_end, southern_bridge_end, GetBridgeHeight(southern_bridge_end),
					GetBridgeType(southern_bridge_end), GetTunnelBridgeTransportType(southern_bridge_end));
			if (bridge_ret.Failed()) return bridge_ret;
		}

		/* If station is set, then we have special handling to allow building on top of already existing stations.
		 * Station points to INVALID_STATION if we can build on any station.
		 * Or it points to a station if we're only allowed to build on exactly that station. */
		if (station != nullptr && IsTileType(cur_tile, MP_STATION)) {
			if (!IsAnyRoadStop(cur_tile)) {
				return ClearTile_Station(cur_tile, DC_AUTO); // Get error message.
			} else {
				if (station_type != GetStationType(cur_tile) ||
						is_drive_through != IsDriveThroughStopTile(cur_tile)) {
					return ClearTile_Station(cur_tile, DC_AUTO); // Get error message.
				}
				/* Drive-through station in the wrong direction. */
				if (is_drive_through && IsDriveThroughStopTile(cur_tile) && GetDriveThroughStopAxis(cur_tile) != axis) {
					return CommandCost(STR_ERROR_DRIVE_THROUGH_DIRECTION);
				}
				StationID st = GetStationIndex(cur_tile);
				if (*station == INVALID_STATION) {
					*station = st;
				} else if (*station != st) {
					return CommandCost(STR_ERROR_ADJOINS_MORE_THAN_ONE_EXISTING);
				}
			}
		} else {
			bool build_over_road = is_drive_through && IsNormalRoadTile(cur_tile);
			/* Road bits in the wrong direction. */
			RoadBits rb = IsNormalRoadTile(cur_tile) ? GetAllRoadBits(cur_tile) : ROAD_NONE;
			if (build_over_road && (rb & (axis == AXIS_X ? ROAD_Y : ROAD_X)) != 0) {
				/* Someone was pedantic and *NEEDED* three fracking different error messages. */
				switch (CountBits(rb)) {
					case 1:
						return CommandCost(STR_ERROR_DRIVE_THROUGH_DIRECTION);

					case 2:
						if (rb == ROAD_X || rb == ROAD_Y) return CommandCost(STR_ERROR_DRIVE_THROUGH_DIRECTION);
						return CommandCost(STR_ERROR_DRIVE_THROUGH_CORNER);

					default: // 3 or 4
						return CommandCost(STR_ERROR_DRIVE_THROUGH_JUNCTION);
				}
			}

			if (build_over_road) {
				/* There is a road, check if we can build road+tram stop over it. */
				RoadType road_rt = GetRoadType(cur_tile, RTT_ROAD);
				if (road_rt != INVALID_ROADTYPE) {
					Owner road_owner = GetRoadOwner(cur_tile, RTT_ROAD);
					if (road_owner == OWNER_TOWN) {
						if (!_settings_game.construction.road_stop_on_town_road) return CommandCost(STR_ERROR_DRIVE_THROUGH_ON_TOWN_ROAD);
					} else if (!_settings_game.construction.road_stop_on_competitor_road && road_owner != OWNER_NONE) {
						ret = CheckOwnership(road_owner);
						if (ret.Failed()) return ret;
					}
					uint num_pieces = CountBits(GetRoadBits(cur_tile, RTT_ROAD));

					if (rt != INVALID_ROADTYPE && RoadTypeIsRoad(rt) && !HasPowerOnRoad(rt, road_rt)) return CommandCost(STR_ERROR_NO_SUITABLE_ROAD);

					cost.AddCost(RoadBuildCost(road_rt) * (2 - num_pieces));
				} else if (rt != INVALID_ROADTYPE && RoadTypeIsRoad(rt)) {
					cost.AddCost(RoadBuildCost(rt) * 2);
				}

				/* There is a tram, check if we can build road+tram stop over it. */
				RoadType tram_rt = GetRoadType(cur_tile, RTT_TRAM);
				if (tram_rt != INVALID_ROADTYPE) {
					Owner tram_owner = GetRoadOwner(cur_tile, RTT_TRAM);
					if (Company::IsValidID(tram_owner) &&
							(!_settings_game.construction.road_stop_on_competitor_road ||
							/* Disallow breaking end-of-line of someone else
							 * so trams can still reverse on this tile. */
							HasExactlyOneBit(GetRoadBits(cur_tile, RTT_TRAM)))) {
						ret = CheckOwnership(tram_owner);
						if (ret.Failed()) return ret;
					}
					uint num_pieces = CountBits(GetRoadBits(cur_tile, RTT_TRAM));

					if (rt != INVALID_ROADTYPE && RoadTypeIsTram(rt) && !HasPowerOnRoad(rt, tram_rt)) return CommandCost(STR_ERROR_NO_SUITABLE_ROAD);

					cost.AddCost(RoadBuildCost(tram_rt) * (2 - num_pieces));
				} else if (rt != INVALID_ROADTYPE && RoadTypeIsTram(rt)) {
					cost.AddCost(RoadBuildCost(rt) * 2);
				}
			} else if (require_road) {
				return CommandCost(STR_ERROR_THERE_IS_NO_ROAD);
			} else {
				ret = Command<CMD_LANDSCAPE_CLEAR>::Do(flags, cur_tile);
				if (ret.Failed()) return ret;
				cost.AddCost(ret);
				cost.AddCost(RoadBuildCost(rt) * 2);
			}
		}
	}

	return cost;
}

/**
 * Checks if an airport can be built at the given location and clear the area.
 * @param tile_iter Airport tile iterator.
 * @param flags Operation to perform.
 * @param station StationID of airport allowed in search area.
 * @return The cost in case of success, or an error code if it failed.
 */
static CommandCost CheckFlatLandAirport(AirportTileTableIterator tile_iter, DoCommandFlag flags, StationID *station)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);
	int allowed_z = -1;

	for (; tile_iter != INVALID_TILE; ++tile_iter) {
		const TileIndex tile_cur = tile_iter;
		CommandCost ret = CheckBuildableTile(tile_cur, 0, allowed_z, true, true);
		if (ret.Failed()) return ret;
		cost.AddCost(ret);

		/* if station is set, then allow building on top of an already
		 * existing airport, either the one in *station if it is not
		 * INVALID_STATION, or anyone otherwise and store which one
		 * in *station */
		if (station != nullptr && IsTileType(tile_cur, MP_STATION)) {
			if (!IsAirport(tile_cur)) {
				return ClearTile_Station(tile_cur, DC_AUTO); // get error message
			} else {
				StationID st = GetStationIndex(tile_cur);
				if (*station == INVALID_STATION) {
					*station = st;
				} else if (*station != st) {
					return CommandCost(STR_ERROR_ADJOINS_MORE_THAN_ONE_EXISTING);
				}
			}
		} else {
			ret = Command<CMD_LANDSCAPE_CLEAR>::Do(flags, tile_cur);
			if (ret.Failed()) return ret;
			cost.AddCost(ret);
		}
	}

	return cost;
}

/**
 * Check whether we can expand the rail part of the given station.
 * @param st the station to expand
 * @param new_ta the current (and if all is fine new) tile area of the rail part of the station
 * @return Succeeded or failed command.
 */
CommandCost CanExpandRailStation(const BaseStation *st, TileArea &new_ta)
{
	TileArea cur_ta = st->train_station;

	/* determine new size of train station region.. */
	int x = std::min(TileX(cur_ta.tile), TileX(new_ta.tile));
	int y = std::min(TileY(cur_ta.tile), TileY(new_ta.tile));
	new_ta.w = (uint16_t)std::max(TileX(cur_ta.tile) + cur_ta.w, TileX(new_ta.tile) + new_ta.w) - x;
	new_ta.h = (uint16_t)std::max(TileY(cur_ta.tile) + cur_ta.h, TileY(new_ta.tile) + new_ta.h) - y;
	new_ta.tile = TileXY(x, y);

	/* make sure the final size is not too big. */
	if (new_ta.w > _settings_game.station.station_spread || new_ta.h > _settings_game.station.station_spread) {
		return CommandCost(STR_ERROR_STATION_TOO_SPREAD_OUT);
	}

	return CommandCost();
}

static inline uint8_t *CreateSingle(uint8_t *layout, int n)
{
	int i = n;
	do *layout++ = 0; while (--i);
	layout[((n - 1) >> 1) - n] = 2;
	return layout;
}

static inline uint8_t *CreateMulti(uint8_t *layout, int n, uint8_t b)
{
	int i = n;
	do *layout++ = b; while (--i);
	if (n > 4) {
		layout[0 - n] = 0;
		layout[n - 1 - n] = 0;
	}
	return layout;
}

/**
 * Create the station layout for the given number of tracks and platform length.
 * @param layout    The layout to write to.
 * @param numtracks The number of tracks to write.
 * @param plat_len  The length of the platforms.
 * @param statspec  The specification of the station to (possibly) get the layout from.
 */
void GetStationLayout(uint8_t *layout, uint numtracks, uint plat_len, const StationSpec *statspec)
{
	if (statspec != nullptr) {
		auto found = statspec->layouts.find(GetStationLayoutKey(numtracks, plat_len));
		if (found != std::end(statspec->layouts)) {
			/* Custom layout defined, copy to buffer. */
			std::copy(std::begin(found->second), std::end(found->second), layout);
			return;
		}
	}

	if (plat_len == 1) {
		CreateSingle(layout, numtracks);
	} else {
		if (numtracks & 1) layout = CreateSingle(layout, plat_len);
		int n = numtracks >> 1;

		while (--n >= 0) {
			layout = CreateMulti(layout, plat_len, 4);
			layout = CreateMulti(layout, plat_len, 6);
		}
	}
}

/**
 * Find a nearby station that joins this station.
 * @tparam T the class to find a station for
 * @param existing_station an existing station we build over
 * @param station_to_join the station to join to
 * @param adjacent whether adjacent stations are allowed
 * @param ta the area of the newly build station
 * @param st 'return' pointer for the found station
 * @param error_message the error message when building a station on top of others
 * @return command cost with the error or 'okay'
 */
template <class T, class F>
CommandCost FindJoiningBaseStation(StationID existing_station, StationID station_to_join, bool adjacent, TileArea ta, T **st, StringID error_message, F filter)
{
	assert(*st == nullptr);
	bool check_surrounding = true;

	if (existing_station != INVALID_STATION) {
		if (adjacent && existing_station != station_to_join) {
			/* You can't build an adjacent station over the top of one that
			 * already exists. */
			return CommandCost(error_message);
		} else {
			/* Extend the current station, and don't check whether it will
			 * be near any other stations. */
			T *candidate = T::GetIfValid(existing_station);
			if (candidate != nullptr && filter(candidate)) *st = candidate;
			check_surrounding = (*st == nullptr);
		}
	} else {
		/* There's no station here. Don't check the tiles surrounding this
		 * one if the company wanted to build an adjacent station. */
		if (adjacent) check_surrounding = false;
	}

	if (check_surrounding) {
		/* Make sure there is no more than one other station around us that is owned by us. */
		CommandCost ret = GetStationAround(ta, existing_station, _current_company, st, filter);
		if (ret.Failed()) return ret;
	}

	/* Distant join */
	if (*st == nullptr && station_to_join != INVALID_STATION) *st = T::GetIfValid(station_to_join);

	return CommandCost();
}

/**
 * Find a nearby station that joins this station.
 * @param existing_station an existing station we build over
 * @param station_to_join the station to join to
 * @param adjacent whether adjacent stations are allowed
 * @param ta the area of the newly build station
 * @param st 'return' pointer for the found station
 * @param error_message the error message when building a station on top of others
 * @return command cost with the error or 'okay'
 */
static CommandCost FindJoiningStation(StationID existing_station, StationID station_to_join, bool adjacent, TileArea ta, Station **st, StringID error_message = STR_ERROR_MUST_REMOVE_RAILWAY_STATION_FIRST)
{
	return FindJoiningBaseStation<Station>(existing_station, station_to_join, adjacent, ta, st, error_message, [](Station *st) -> bool { return true; });
}

/**
 * Find a nearby waypoint that joins this waypoint.
 * @param existing_waypoint an existing waypoint we build over
 * @param waypoint_to_join the waypoint to join to
 * @param adjacent whether adjacent waypoints are allowed
 * @param ta the area of the newly build waypoint
 * @param wp 'return' pointer for the found waypoint
 * @return command cost with the error or 'okay'
 */
CommandCost FindJoiningWaypoint(StationID existing_waypoint, StationID waypoint_to_join, bool adjacent, TileArea ta, Waypoint **wp, bool is_road)
{
	return FindJoiningBaseStation<Waypoint>(existing_waypoint, waypoint_to_join, adjacent, ta, wp,
			is_road ? STR_ERROR_MUST_REMOVE_ROADWAYPOINT_FIRST : STR_ERROR_MUST_REMOVE_RAILWAYPOINT_FIRST,
			[is_road](Waypoint *wp) -> bool { return HasBit(wp->waypoint_flags, WPF_ROAD) == is_road; });
}

/**
 * Clear any rail station platform reservation ahead of and behind train.
 * @param v vehicle which may hold reservations
 */
void FreeTrainStationPlatformReservation(const Train *v)
{
	if (IsRailStationTile(v->tile)) SetRailStationPlatformReservation(v->tile, TrackdirToExitdir(v->GetVehicleTrackdir()), false);
	v = v->Last();
	if (IsRailStationTile(v->tile)) SetRailStationPlatformReservation(v->tile, TrackdirToExitdir(ReverseTrackdir(v->GetVehicleTrackdir())), false);
}

/**
 * Clear platform reservation during station building/removing.
 * @param v vehicle which holds reservation
 */
static void FreeTrainReservation(Train *v)
{
	FreeTrainTrackReservation(v);
	FreeTrainStationPlatformReservation(v);
}

/**
 * Restore platform reservation during station building/removing.
 * @param v vehicle which held reservation
 */
static void RestoreTrainReservation(Train *v)
{
	if (IsRailStationTile(v->tile)) SetRailStationPlatformReservation(v->tile, TrackdirToExitdir(v->GetVehicleTrackdir()), true);
	TryPathReserve(v, true, true);
	v = v->Last();
	if (IsRailStationTile(v->tile)) SetRailStationPlatformReservation(v->tile, TrackdirToExitdir(ReverseTrackdir(v->GetVehicleTrackdir())), true);
}

/**
 * Get station tile flags for the given StationGfx.
 * @param gfx StationGfx of station tile.
 * @param statspec Station spec of station tile.
 * @return Tile flags to apply.
 */
static StationSpec::TileFlags GetStationTileFlags(StationGfx gfx, const StationSpec *statspec)
{
	/* Default stations do not draw pylons under roofs (gfx >= 4) */
	if (statspec == nullptr || gfx >= statspec->tileflags.size()) return gfx < 4 ? StationSpec::TileFlag::Pylons : StationSpec::TileFlags{};
	return statspec->tileflags[gfx];
}

/**
 * Set rail station tile flags for the given tile.
 * @param tile Tile to set flags on.
 * @param statspec Statspec of the tile.
 */
void SetRailStationTileFlags(TileIndex tile, const StationSpec *statspec)
{
	const auto flags = GetStationTileFlags(GetStationGfx(tile), statspec);
	SetStationTileBlocked(tile, flags.Test(StationSpec::TileFlag::Blocked));
	SetStationTileHavePylons(tile, flags.Test(StationSpec::TileFlag::Pylons));
	SetStationTileHaveWires(tile, !flags.Test(StationSpec::TileFlag::NoWires));
}

/**
 * Build rail station
 * @param flags operation to perform
 * @param tile_org northern most position of station dragging/placement
 * @param rt railtype
 * @param axis orientation (Axis)
 * @param numtracks number of tracks
 * @param plat_len platform length
 * @param spec_class custom station class
 * @param spec_index custom station id
 * @param station_to_join station ID to join (NEW_STATION if build new one)
 * @param adjacent allow stations directly adjacent to other stations.
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildRailStation(DoCommandFlag flags, TileIndex tile_org, RailType rt, Axis axis, uint8_t numtracks, uint8_t plat_len, StationClassID spec_class, uint16_t spec_index, StationID station_to_join, bool adjacent)
{
	/* Does the authority allow this? */
	CommandCost ret = CheckIfAuthorityAllowsNewStation(tile_org, flags);
	if (ret.Failed()) return ret;

	if (!ValParamRailType(rt) || !IsValidAxis(axis)) return CMD_ERROR;

	/* Check if the given station class is valid */
	if (static_cast<uint>(spec_class) >= StationClass::GetClassCount()) return CMD_ERROR;
	const StationClass *cls = StationClass::Get(spec_class);
	if (IsWaypointClass(*cls)) return CMD_ERROR;
	if (spec_index >= cls->GetSpecCount()) return CMD_ERROR;
	if (plat_len == 0 || numtracks == 0) return CMD_ERROR;

	int w_org, h_org;
	if (axis == AXIS_X) {
		w_org = plat_len;
		h_org = numtracks;
	} else {
		h_org = plat_len;
		w_org = numtracks;
	}

	/* Check if the first tile and the last tile are valid */
	if (!IsValidTile(tile_org) || TileAddWrap(tile_org, w_org - 1, h_org - 1) == INVALID_TILE) return CMD_ERROR;

	bool reuse = (station_to_join != NEW_STATION);
	if (!reuse) station_to_join = INVALID_STATION;
	bool distant_join = (station_to_join != INVALID_STATION);

	if (distant_join && (!_settings_game.station.distant_join_stations || !Station::IsValidID(station_to_join))) return CMD_ERROR;

	if (h_org > _settings_game.station.station_spread || w_org > _settings_game.station.station_spread) return CMD_ERROR;

	/* these values are those that will be stored in train_tile and station_platforms */
	TileArea new_location(tile_org, w_org, h_org);

	/* Make sure the area below consists of clear tiles. (OR tiles belonging to a certain rail station) */
	StationID est = INVALID_STATION;
	std::vector<Train *> affected_vehicles;

	const StationSpec *statspec = StationClass::Get(spec_class)->GetSpec(spec_index);

	TileIndexDiff tile_delta = TileOffsByAxis(axis); // offset to go to the next platform tile
	TileIndexDiff track_delta = TileOffsByAxis(OtherAxis(axis)); // offset to go to the next track
	TempBufferST<uint8_t> layout_buffer(numtracks * plat_len);
	GetStationLayout(layout_buffer, numtracks, plat_len, statspec);

	{
		TileIndex tile_track = tile_org;
		uint8_t *check_layout_ptr = layout_buffer;
		for (uint i = 0; i < numtracks; i++) {
			TileIndex tile = tile_track;
			for (uint j = 0; j < plat_len; j++) {
				CommandCost ret = IsRailStationBridgeAboveOk(tile, statspec, *check_layout_ptr++);
				if (ret.Failed()) {
					return CommandCost::DualErrorMessage(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST, ret.GetErrorMessage());
				}
				tile += tile_delta;
			}
			tile_track += track_delta;
		}
	}

	/* Clear the land below the station. */
	CommandCost cost = CheckFlatLandRailStation(new_location, flags, axis, &est, rt, affected_vehicles, spec_class, spec_index, plat_len, numtracks);
	if (cost.Failed()) return cost;
	/* Add construction expenses. */
	cost.AddCost((numtracks * _price[PR_BUILD_STATION_RAIL] + _price[PR_BUILD_STATION_RAIL_LENGTH]) * plat_len);
	cost.AddCost(numtracks * plat_len * RailBuildCost(rt));

	Station *st = nullptr;
	ret = FindJoiningStation(est, station_to_join, adjacent, new_location, &st);
	if (ret.Failed()) return ret;

	ret = BuildStationPart(&st, flags, reuse, new_location, STATIONNAMING_RAIL);
	if (ret.Failed()) return ret;

	if (st != nullptr && st->train_station.tile != INVALID_TILE) {
		ret = CanExpandRailStation(st, new_location);
		if (ret.Failed()) return ret;
	}

	/* Check if we can allocate a custom stationspec to this station */
	int specindex = AllocateSpecToStation(statspec, st, (flags & DC_EXEC) != 0);
	if (specindex == -1) return CommandCost(STR_ERROR_TOO_MANY_STATION_SPECS);

	if (statspec != nullptr) {
		/* Perform NewStation checks */

		/* Check if the station size is permitted */
		if (HasBit(statspec->disallowed_platforms, std::min(numtracks - 1, 7))) return CommandCost(STR_ERROR_STATION_DISALLOWED_NUMBER_TRACKS);
		if (HasBit(statspec->disallowed_lengths, std::min(plat_len - 1, 7))) return CommandCost(STR_ERROR_STATION_DISALLOWED_LENGTH);

		/* Check if the station is buildable */
		if (statspec->callback_mask.Test(StationCallbackMask::Avail)) {
			uint16_t cb_res = GetStationCallback(CBID_STATION_AVAILABILITY, 0, 0, statspec, nullptr, INVALID_TILE, rt);
			if (cb_res != CALLBACK_FAILED && !Convert8bitBooleanCallback(statspec->grf_prop.grffile, CBID_STATION_AVAILABILITY, cb_res)) return CMD_ERROR;
		}
	}

	if (flags & DC_EXEC) {
		st->train_station = new_location;
		st->AddFacility(FACIL_TRAIN, new_location.tile);

		st->rect.BeforeAddRect(tile_org, w_org, h_org, StationRect::ADD_TRY);

		if (statspec != nullptr) {
			/* Include this station spec's animation trigger bitmask
			 * in the station's cached copy. */
			st->cached_anim_triggers |= statspec->animation.triggers;
		}

		Track track = AxisToTrack(axis);

		uint8_t numtracks_orig = numtracks;

		Company *c = Company::Get(st->owner);
		TileIndex tile_track = tile_org;
		uint8_t *layout_ptr = layout_buffer;
		do {
			TileIndex tile = tile_track;
			int w = plat_len;
			do {
				uint8_t layout = *layout_ptr++;
				if (IsRailStationTile(tile) && HasStationReservation(tile)) {
					/* Check for trains having a reservation for this tile. */
					Train *v = GetTrainForReservation(tile, AxisToTrack(GetRailStationAxis(tile)));
					if (v != nullptr) {
						affected_vehicles.push_back(v);
						/* Not necessary to call CheckTrainReservationPreventsTrackModification as that is done by CheckFlatLandRailStation */
						FreeTrainReservation(v);
					}
				}

				/* Railtype can change when overbuilding. */
				if (IsRailStationTile(tile)) {
					if (!IsStationTileBlocked(tile)) c->infrastructure.rail[GetRailType(tile)]--;
					c->infrastructure.station--;
				}

				/* Remove animation if overbuilding */
				DeleteAnimatedTile(tile);
				uint8_t old_specindex = HasStationTileRail(tile) ? GetCustomStationSpecIndex(tile) : 0;
				MakeRailStation(tile, st->owner, st->index, axis, layout & ~1, rt);
				/* Free the spec if we overbuild something */
				if (old_specindex != specindex) DeallocateSpecFromStation(st, old_specindex);

				SetCustomStationSpecIndex(tile, specindex);
				SetStationTileRandomBits(tile, GB(Random(), 0, 4));
				SetAnimationFrame(tile, 0);

				if (statspec != nullptr) {
					/* Use a fixed axis for GetPlatformInfo as our platforms / numtracks are always the right way around */
					uint32_t platinfo = GetPlatformInfo(AXIS_X, GetStationGfx(tile), plat_len, numtracks_orig, plat_len - w, numtracks_orig - numtracks, false);

					/* As the station is not yet completely finished, the station does not yet exist. */
					uint16_t callback = GetStationCallback(CBID_STATION_BUILD_TILE_LAYOUT, platinfo, 0, statspec, nullptr, tile, rt);
					if (callback != CALLBACK_FAILED) {
						if (callback <= UINT8_MAX) {
							SetStationGfx(tile, (callback & ~1) + axis);
						} else {
							ErrorUnknownCallbackResult(statspec->grf_prop.grfid, CBID_STATION_BUILD_TILE_LAYOUT, callback);
						}
					}

					/* Trigger station animation -- after building? */
					TriggerStationAnimation(st, tile, SAT_BUILT);
				}

				SetRailStationTileFlags(tile, statspec);

				if (!IsStationTileBlocked(tile)) c->infrastructure.rail[rt]++;
				c->infrastructure.station++;

				tile += tile_delta;
			} while (--w);
			AddTrackToSignalBuffer(tile_track, track, _current_company);
			YapfNotifyTrackLayoutChange(tile_track, track);
			tile_track += track_delta;
		} while (--numtracks);

		for (uint i = 0; i < affected_vehicles.size(); ++i) {
			/* Restore reservations of trains. */
			RestoreTrainReservation(affected_vehicles[i]);
		}

		/* Check whether we need to expand the reservation of trains already on the station. */
		TileArea update_reservation_area;
		if (axis == AXIS_X) {
			update_reservation_area = TileArea(tile_org, 1, numtracks_orig);
		} else {
			update_reservation_area = TileArea(tile_org, numtracks_orig, 1);
		}

		for (TileIndex tile : update_reservation_area) {
			/* Don't even try to make eye candy parts reserved. */
			if (IsStationTileBlocked(tile)) continue;

			DiagDirection dir = AxisToDiagDir(axis);
			TileIndexDiff tile_offset = TileOffsByDiagDir(dir);
			TileIndex platform_begin = tile;
			TileIndex platform_end = tile;

			/* We can only account for tiles that are reachable from this tile, so ignore primarily blocked tiles while finding the platform begin and end. */
			for (TileIndex next_tile = platform_begin - tile_offset; IsCompatibleTrainStationTile(next_tile, platform_begin); next_tile -= tile_offset) {
				platform_begin = next_tile;
			}
			for (TileIndex next_tile = platform_end + tile_offset; IsCompatibleTrainStationTile(next_tile, platform_end); next_tile += tile_offset) {
				platform_end = next_tile;
			}

			/* If there is at least on reservation on the platform, we reserve the whole platform. */
			bool reservation = false;
			for (TileIndex t = platform_begin; !reservation && t <= platform_end; t += tile_offset) {
				reservation = HasStationReservation(t);
			}

			if (reservation) {
				SetRailStationPlatformReservation(platform_begin, dir, true);
			}
		}

		st->MarkTilesDirty(false);
		st->AfterStationTileSetChange(true, StationType::Rail);
		ZoningMarkDirtyStationCoverageArea(st);
	}

	return cost;
}

static TileArea MakeStationAreaSmaller(BaseStation *st, TileArea ta, bool (*func)(BaseStation *, TileIndex))
{
restart:

	/* too small? */
	if (ta.w != 0 && ta.h != 0) {
		/* check the left side, x = constant, y changes */
		for (uint i = 0; !func(st, ta.tile + TileDiffXY(0, i));) {
			/* the left side is unused? */
			if (++i == ta.h) {
				ta.tile += TileDiffXY(1, 0);
				ta.w--;
				goto restart;
			}
		}

		/* check the right side, x = constant, y changes */
		for (uint i = 0; !func(st, ta.tile + TileDiffXY(ta.w - 1, i));) {
			/* the right side is unused? */
			if (++i == ta.h) {
				ta.w--;
				goto restart;
			}
		}

		/* check the upper side, y = constant, x changes */
		for (uint i = 0; !func(st, ta.tile + TileDiffXY(i, 0));) {
			/* the left side is unused? */
			if (++i == ta.w) {
				ta.tile += TileDiffXY(0, 1);
				ta.h--;
				goto restart;
			}
		}

		/* check the lower side, y = constant, x changes */
		for (uint i = 0; !func(st, ta.tile + TileDiffXY(i, ta.h - 1));) {
			/* the left side is unused? */
			if (++i == ta.w) {
				ta.h--;
				goto restart;
			}
		}
	} else {
		ta.Clear();
	}

	return ta;
}

static bool TileBelongsToRailStation(BaseStation *st, TileIndex tile)
{
	return st->TileBelongsToRailStation(tile);
}

static void MakeRailStationAreaSmaller(BaseStation *st)
{
	st->train_station = MakeStationAreaSmaller(st, st->train_station, TileBelongsToRailStation);
}

static bool TileBelongsToShipStation(BaseStation *st, TileIndex tile)
{
	return IsDockTile(tile) && GetStationIndex(tile) == st->index;
}

static void MakeShipStationAreaSmaller(Station *st)
{
	st->ship_station = MakeStationAreaSmaller(st, st->ship_station, TileBelongsToShipStation);
	UpdateStationDockingTiles(st);
}

static bool TileBelongsToRoadWaypointStation(BaseStation *st, TileIndex tile)
{
	return IsRoadWaypointTile(tile) && GetStationIndex(tile) == st->index;
}

void MakeRoadWaypointStationAreaSmaller(BaseStation *st, TileArea &road_waypoint_area)
{
	road_waypoint_area = MakeStationAreaSmaller(st, road_waypoint_area, TileBelongsToRoadWaypointStation);
}

/**
 * Remove a number of tiles from any rail station within the area.
 * @param ta the area to clear station tile from.
 * @param affected_stations the stations affected.
 * @param flags the command flags.
 * @param removal_cost the cost for removing the tile, including the rail.
 * @param keep_rail whether to keep the rail of the station.
 * @tparam T the type of station to remove.
 * @return the number of cleared tiles or an error.
 */
template <class T>
CommandCost RemoveFromRailBaseStation(TileArea ta, std::vector<T *> &affected_stations, DoCommandFlag flags, Money removal_cost, bool keep_rail)
{
	/* Count of the number of tiles removed */
	int quantity = 0;
	CommandCost total_cost(EXPENSES_CONSTRUCTION);
	/* Accumulator for the errors seen during clearing. If no errors happen,
	 * and the quantity is 0 there is no station. Otherwise it will be one
	 * of the other error that got accumulated. */
	CommandCost error;

	/* Do the action for every tile into the area */
	for (TileIndex tile : ta) {
		/* Make sure the specified tile is a rail station */
		if (!HasStationTileRail(tile)) continue;

		/* If there is a vehicle on ground, do not allow to remove (flood) the tile */
		CommandCost ret = EnsureNoVehicleOnGround(tile);
		error.AddCost(ret);
		if (ret.Failed()) continue;

		/* Check ownership of station */
		T *st = T::GetByTile(tile);
		if (st == nullptr) continue;

		if (_current_company != OWNER_WATER) {
			ret = CheckOwnership(st->owner);
			error.AddCost(ret);
			if (ret.Failed()) continue;
		}

		Train *v = nullptr;
		Track track = GetRailStationTrack(tile);
		if (HasStationReservation(tile)) {
			v = GetTrainForReservation(tile, track);
			if (v != nullptr) {
				CommandCost ret = CheckTrainReservationPreventsTrackModification(v);
				error.AddCost(ret);
				if (ret.Failed()) continue;
				if (flags & DC_EXEC) FreeTrainReservation(v);
			}
		}

		/* If we reached here, the tile is valid so increase the quantity of tiles we will remove */
		quantity++;

		if (keep_rail || IsStationTileBlocked(tile)) {
			/* Don't refund the 'steel' of the track when we keep the
			 *  rail, or when the tile didn't have any rail at all. */
			total_cost.AddCost(-_price[PR_CLEAR_RAIL]);
		}

		if (flags & DC_EXEC) {
			bool already_affected = include(affected_stations, st);
			if (!already_affected) ZoningMarkDirtyStationCoverageArea(st);

			/* read variables before the station tile is removed */
			uint specindex = GetCustomStationSpecIndex(tile);
			Owner owner = GetTileOwner(tile);
			RailType rt = GetRailType(tile);

			bool build_rail = keep_rail && !IsStationTileBlocked(tile);
			if (!build_rail && !IsStationTileBlocked(tile)) Company::Get(owner)->infrastructure.rail[rt]--;

			DoClearSquare(tile);
			DeleteNewGRFInspectWindow(GSF_STATIONS, tile.base());
			if (build_rail) MakeRailNormal(tile, owner, TrackToTrackBits(track), rt);
			Company::Get(owner)->infrastructure.station--;
			DirtyCompanyInfrastructureWindows(owner);

			st->rect.AfterRemoveTile(st, tile);
			AddTrackToSignalBuffer(tile, track, owner);
			YapfNotifyTrackLayoutChange(tile, track);

			DeallocateSpecFromStation(st, specindex);

			if (v != nullptr) RestoreTrainReservation(v);
		}
	}

	if (quantity == 0) return error.Failed() ? error : CommandCost(STR_ERROR_THERE_IS_NO_STATION);

	for (T *st : affected_stations) {

		/* now we need to make the "spanned" area of the railway station smaller
		 * if we deleted something at the edges.
		 * we also need to adjust train_tile. */
		MakeRailStationAreaSmaller(st);
		UpdateStationSignCoord(st);

		/* if we deleted the whole station, delete the train facility. */
		if (st->train_station.tile == INVALID_TILE) {
			st->facilities &= ~FACIL_TRAIN;
			SetWindowClassesDirty(WC_VEHICLE_ORDERS);
			SetWindowWidgetDirty(WC_STATION_VIEW, st->index, WID_SV_TRAINS);
			st->UpdateVirtCoord();
			DeleteStationIfEmpty(st);
		}
	}

	total_cost.AddCost(quantity * removal_cost);
	return total_cost;
}

/**
 * Remove a single tile from a rail station.
 * This allows for custom-built station with holes and weird layouts
 * @param flags operation to perform
 * @param start tile of station piece to remove
 * @param end other edge of the rect to remove
 * @param keep_rail if set keep the rail
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveFromRailStation(DoCommandFlag flags, TileIndex start, TileIndex end, bool keep_rail)
{
	if (end == 0) end = start;
	if (start >= Map::Size() || end >= Map::Size()) return CMD_ERROR;

	TileArea ta(start, end);
	std::vector<Station *> affected_stations;

	CommandCost ret = RemoveFromRailBaseStation(ta, affected_stations, flags, _price[PR_CLEAR_STATION_RAIL], keep_rail);
	if (ret.Failed()) return ret;

	/* Do all station specific functions here. */
	for (Station *st : affected_stations) {

		if (st->train_station.tile == INVALID_TILE) SetWindowWidgetDirty(WC_STATION_VIEW, st->index, WID_SV_TRAINS);
		st->MarkTilesDirty(false);
		st->RecomputeCatchment();
	}

	/* Now apply the rail cost to the number that we deleted */
	return ret;
}

/**
 * Remove a single tile from a waypoint.
 * This allows for custom-built waypoint with holes and weird layouts
 * @param flags operation to perform
 * @param start tile of waypoint piece to remove
 * @param end other edge of the rect to remove
 * @param keep_rail if set keep the rail
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveFromRailWaypoint(DoCommandFlag flags, TileIndex start, TileIndex end, bool keep_rail)
{
	if (end == 0) end = start;
	if (start >= Map::Size() || end >= Map::Size()) return CMD_ERROR;

	TileArea ta(start, end);
	std::vector<Waypoint *> affected_stations;

	return RemoveFromRailBaseStation(ta, affected_stations, flags, _price[PR_CLEAR_WAYPOINT_RAIL], keep_rail);
}


/**
 * Remove a rail station/waypoint
 * @param st The station/waypoint to remove the rail part from
 * @param flags operation to perform
 * @param removal_cost the cost for removing a tile
 * @tparam T the type of station to remove
 * @return cost or failure of operation
 */
template <class T>
CommandCost RemoveRailStation(T *st, DoCommandFlag flags, Money removal_cost)
{
	/* Current company owns the station? */
	if (_current_company != OWNER_WATER) {
		CommandCost ret = CheckOwnership(st->owner);
		if (ret.Failed()) return ret;
	}

	/* determine width and height of platforms */
	TileArea ta = st->train_station;

	assert(ta.w != 0 && ta.h != 0);

	CommandCost cost(EXPENSES_CONSTRUCTION);
	/* clear all areas of the station */
	for (TileIndex tile : ta) {
		/* only remove tiles that are actually train station tiles */
		if (st->TileBelongsToRailStation(tile)) {
			std::vector<T*> affected_stations; // dummy
			CommandCost ret = RemoveFromRailBaseStation(TileArea(tile, 1, 1), affected_stations, flags, removal_cost, false);
			if (ret.Failed()) return ret;
			cost.AddCost(ret);
		}
	}

	return cost;
}

/**
 * Remove a rail station
 * @param tile Tile of the station.
 * @param flags operation to perform
 * @return cost or failure of operation
 */
static CommandCost RemoveRailStation(TileIndex tile, DoCommandFlag flags)
{
	/* if there is flooding, remove platforms tile by tile */
	if (_current_company == OWNER_WATER) {
		return Command<CMD_REMOVE_FROM_RAIL_STATION>::Do(DC_EXEC, tile, TileIndex{}, false);
	}

	Station *st = Station::GetByTile(tile);

	if (flags & DC_EXEC) ZoningMarkDirtyStationCoverageArea(st);

	CommandCost cost = RemoveRailStation(st, flags, _price[PR_CLEAR_STATION_RAIL]);

	if (flags & DC_EXEC) st->RecomputeCatchment();

	return cost;
}

/**
 * Remove a rail waypoint
 * @param tile Tile of the waypoint.
 * @param flags operation to perform
 * @return cost or failure of operation
 */
static CommandCost RemoveRailWaypoint(TileIndex tile, DoCommandFlag flags)
{
	/* if there is flooding, remove waypoints tile by tile */
	if (_current_company == OWNER_WATER) {
		return Command<CMD_REMOVE_FROM_RAIL_WAYPOINT>::Do(DC_EXEC, tile, TileIndex{}, false);
	}

	return RemoveRailStation(Waypoint::GetByTile(tile), flags, _price[PR_CLEAR_WAYPOINT_RAIL]);
}


/**
 * @param truck_station Determines whether a stop is #RoadStopType::Bus or #RoadStopType::Truck
 * @param st The Station to do the whole procedure for
 * @return a pointer to where to link a new RoadStop*
 */
static RoadStop **FindRoadStopSpot(bool truck_station, Station *st)
{
	RoadStop **primary_stop = (truck_station) ? &st->truck_stops : &st->bus_stops;

	if (*primary_stop == nullptr) {
		/* we have no roadstop of the type yet, so write a "primary stop" */
		return primary_stop;
	} else {
		/* there are stops already, so append to the end of the list */
		RoadStop *stop = *primary_stop;
		while (stop->next != nullptr) stop = stop->next;
		return &stop->next;
	}
}

CommandCost RemoveRoadStop(TileIndex tile, DoCommandFlag flags, int replacement_spec_index = -1);

/**
 * Find a nearby station that joins this road stop.
 * @param existing_stop an existing road stop we build over
 * @param station_to_join the station to join to
 * @param adjacent whether adjacent stations are allowed
 * @param ta the area of the newly build station
 * @param st 'return' pointer for the found station
 * @return command cost with the error or 'okay'
 */
static CommandCost FindJoiningRoadStop(StationID existing_stop, StationID station_to_join, bool adjacent, TileArea ta, Station **st)
{
	return FindJoiningBaseStation<Station>(existing_stop, station_to_join, adjacent, ta, st, STR_ERROR_MUST_REMOVE_ROAD_STOP_FIRST, [](Station *st) -> bool { return true; });
}

/**
 * Build a bus or truck stop.
 * @param flags Operation to perform.
 * @param tile Northernmost tile of the stop.
 * @param width Width of the road stop.
 * @param length Length of the road stop.
 * @param stop_type Type of road stop (bus/truck).
 * @param is_drive_through False for normal stops, true for drive-through.
 * @param ddir Entrance direction (#DiagDirection) for normal stops. Converted to the axis for drive-through stops.
 * @param rt The roadtype.
 * @param spec_class Road stop spec class.
 * @param spec_index Road stop spec index.
 * @param station_to_join Station ID to join (NEW_STATION if build new one).
 * @param adjacent Allow stations directly adjacent to other stations.
 * @return The cost of this operation or an error.
 */
CommandCost CmdBuildRoadStop(DoCommandFlag flags, TileIndex tile, uint8_t width, uint8_t length, RoadStopType stop_type, bool is_drive_through,
		DiagDirection ddir, RoadType rt, RoadStopClassID spec_class, uint16_t spec_index, StationID station_to_join, bool adjacent)
{
	if (!ValParamRoadType(rt) || !IsValidDiagDirection(ddir) || stop_type >= RoadStopType::End) return CMD_ERROR;
	bool reuse = (station_to_join != NEW_STATION);
	if (!reuse) station_to_join = INVALID_STATION;
	bool distant_join = (station_to_join != INVALID_STATION);

	/* Check if the given station class is valid */
	if (static_cast<uint>(spec_class) >= RoadStopClass::GetClassCount()) return CMD_ERROR;
	const RoadStopClass *cls = RoadStopClass::Get(spec_class);
	if (IsWaypointClass(*cls)) return CMD_ERROR;
	if (spec_index >= cls->GetSpecCount()) return CMD_ERROR;

	const RoadStopSpec *roadstopspec = cls->GetSpec(spec_index);
	if (roadstopspec != nullptr) {
		if (stop_type == RoadStopType::Truck && roadstopspec->stop_type != ROADSTOPTYPE_FREIGHT && roadstopspec->stop_type != ROADSTOPTYPE_ALL) return CMD_ERROR;
		if (stop_type == RoadStopType::Bus && roadstopspec->stop_type != ROADSTOPTYPE_PASSENGER && roadstopspec->stop_type != ROADSTOPTYPE_ALL) return CMD_ERROR;
		if (!is_drive_through && roadstopspec->flags.Test(RoadStopSpecFlag::DriveThroughOnly)) return CMD_ERROR;
	}

	/* Check if the requested road stop is too big */
	if (width > _settings_game.station.station_spread || length > _settings_game.station.station_spread) return CommandCost(STR_ERROR_STATION_TOO_SPREAD_OUT);
	/* Check for incorrect width / length. */
	if (width == 0 || length == 0) return CMD_ERROR;
	/* Check if the first tile and the last tile are valid */
	if (!IsValidTile(tile) || TileAddWrap(tile, width - 1, length - 1) == INVALID_TILE) return CMD_ERROR;

	TileArea roadstop_area(tile, width, length);

	if (distant_join && (!_settings_game.station.distant_join_stations || !Station::IsValidID(station_to_join))) return CMD_ERROR;

	/* Trams only have drive through stops */
	if (!is_drive_through && RoadTypeIsTram(rt)) return CMD_ERROR;

	Axis axis = DiagDirToAxis(ddir);

	CommandCost ret = CheckIfAuthorityAllowsNewStation(tile, flags);
	if (ret.Failed()) return ret;

	bool is_truck_stop = stop_type == RoadStopType::Truck;
	StationType station_type = is_truck_stop ? StationType::Truck : StationType::Bus;

	/* Total road stop cost. */
	Money unit_cost;
	if (roadstopspec != nullptr) {
		unit_cost = roadstopspec->GetBuildCost(is_truck_stop ? PR_BUILD_STATION_TRUCK : PR_BUILD_STATION_BUS);
	} else {
		unit_cost = _price[is_truck_stop ? PR_BUILD_STATION_TRUCK : PR_BUILD_STATION_BUS];
	}
	CommandCost cost(EXPENSES_CONSTRUCTION, roadstop_area.w * roadstop_area.h * unit_cost);
	StationID est = INVALID_STATION;
	ret = CheckFlatLandRoadStop(roadstop_area, roadstopspec, flags, is_drive_through ? 5 << axis : 1 << ddir, is_drive_through, station_type, axis, &est, rt, false);
	if (ret.Failed()) return ret;
	cost.AddCost(ret);

	Station *st = nullptr;
	ret = FindJoiningRoadStop(est, station_to_join, adjacent, roadstop_area, &st);
	if (ret.Failed()) return ret;

	/* Check if this number of road stops can be allocated. */
	if (!RoadStop::CanAllocateItem(static_cast<size_t>(roadstop_area.w) * roadstop_area.h)) return CommandCost(is_truck_stop ? STR_ERROR_TOO_MANY_TRUCK_STOPS : STR_ERROR_TOO_MANY_BUS_STOPS);

	ret = BuildStationPart(&st, flags, reuse, roadstop_area, STATIONNAMING_ROAD);
	if (ret.Failed()) return ret;

	/* Check if we can allocate a custom stationspec to this station */
	int specindex = AllocateRoadStopSpecToStation(roadstopspec, st, (flags & DC_EXEC) != 0);
	if (specindex == -1) return CommandCost(STR_ERROR_TOO_MANY_STATION_SPECS);

	if (roadstopspec != nullptr) {
		/* Perform NewGRF checks */

		/* Check if the road stop is buildable */
		if (roadstopspec->callback_mask.Test(RoadStopCallbackMask::Avail)) {
			uint16_t cb_res = GetRoadStopCallback(CBID_STATION_AVAILABILITY, 0, 0, roadstopspec, nullptr, INVALID_TILE, rt, station_type, 0);
			if (cb_res != CALLBACK_FAILED && !Convert8bitBooleanCallback(roadstopspec->grf_prop.grffile, CBID_STATION_AVAILABILITY, cb_res)) return CMD_ERROR;
		}
	}

	if (flags & DC_EXEC) {
		/* Check every tile in the area. */
		for (TileIndex cur_tile : roadstop_area) {
			/* Get existing road types and owners before any tile clearing */
			RoadType road_rt = MayHaveRoad(cur_tile) ? GetRoadType(cur_tile, RTT_ROAD) : INVALID_ROADTYPE;
			RoadType tram_rt = MayHaveRoad(cur_tile) ? GetRoadType(cur_tile, RTT_TRAM) : INVALID_ROADTYPE;
			Owner road_owner = road_rt != INVALID_ROADTYPE ? GetRoadOwner(cur_tile, RTT_ROAD) : _current_company;
			Owner tram_owner = tram_rt != INVALID_ROADTYPE ? GetRoadOwner(cur_tile, RTT_TRAM) : _current_company;

			DisallowedRoadDirections drd = DRD_NONE;
			if (road_rt != INVALID_ROADTYPE) {
				if (IsNormalRoadTile(cur_tile)){
					drd = GetDisallowedRoadDirections(cur_tile);
				} else if (IsDriveThroughStopTile(cur_tile)) {
					drd = GetDriveThroughStopDisallowedRoadDirections(cur_tile);
				}
			}

			if (IsTileType(cur_tile, MP_STATION) && IsAnyRoadStop(cur_tile)) {
				RemoveRoadStop(cur_tile, flags, specindex);
			}

			if (roadstopspec != nullptr) {
				/* Include this road stop spec's animation trigger bitmask
				 * in the station's cached copy. */
				st->cached_roadstop_anim_triggers |= roadstopspec->animation.triggers;
			}

			RoadStop *road_stop = new RoadStop(cur_tile);
			/* Insert into linked list of RoadStops. */
			RoadStop **currstop = FindRoadStopSpot(is_truck_stop, st);
			*currstop = road_stop;

			if (is_truck_stop) {
				st->truck_station.Add(cur_tile);
			} else {
				st->bus_station.Add(cur_tile);
			}

			/* Initialize an empty station. */
			st->AddFacility(is_truck_stop ? FACIL_TRUCK_STOP : FACIL_BUS_STOP, cur_tile);

			st->rect.BeforeAddTile(cur_tile, StationRect::ADD_TRY);

			if (is_drive_through) {
				/* Update company infrastructure counts. If the current tile is a normal road tile, remove the old
				 * bits first. */
				if (IsNormalRoadTile(cur_tile)) {
					UpdateCompanyRoadInfrastructure(road_rt, road_owner, -(int)CountBits(GetRoadBits(cur_tile, RTT_ROAD)));
					UpdateCompanyRoadInfrastructure(tram_rt, tram_owner, -(int)CountBits(GetRoadBits(cur_tile, RTT_TRAM)));
				}

				if (road_rt == INVALID_ROADTYPE && RoadTypeIsRoad(rt)) road_rt = rt;
				if (tram_rt == INVALID_ROADTYPE && RoadTypeIsTram(rt)) tram_rt = rt;

				MakeDriveThroughRoadStop(cur_tile, st->owner, road_owner, tram_owner, st->index, station_type, road_rt, tram_rt, axis);
				SetDriveThroughStopDisallowedRoadDirections(cur_tile, drd);
				road_stop->MakeDriveThrough();
			} else {
				if (road_rt == INVALID_ROADTYPE && RoadTypeIsRoad(rt)) road_rt = rt;
				if (tram_rt == INVALID_ROADTYPE && RoadTypeIsTram(rt)) tram_rt = rt;
				MakeRoadStop(cur_tile, st->owner, st->index, stop_type, road_rt, tram_rt, ddir);
			}
			UpdateCompanyRoadInfrastructure(road_rt, road_owner, ROAD_STOP_TRACKBIT_FACTOR);
			UpdateCompanyRoadInfrastructure(tram_rt, tram_owner, ROAD_STOP_TRACKBIT_FACTOR);
			Company::Get(st->owner)->infrastructure.station++;

			SetCustomRoadStopSpecIndex(cur_tile, specindex);
			if (roadstopspec != nullptr) {
				st->SetRoadStopRandomBits(cur_tile, GB(Random(), 0, 8));
				TriggerRoadStopAnimation(st, cur_tile, SAT_BUILT);
			}

			MarkTileDirtyByTile(cur_tile);
			UpdateRoadCachedOneWayStatesAroundTile(cur_tile);
		}
		ZoningMarkDirtyStationCoverageArea(st);
		NotifyRoadLayoutChanged(true);

		if (st != nullptr) {
			st->AfterStationTileSetChange(true, station_type);
		}
	}
	return cost;
}


static Vehicle *ClearRoadStopStatusEnum(Vehicle *v, void *)
{
	/* Okay... we are a road vehicle on a drive through road stop.
	 * But that road stop has just been removed, so we need to make
	 * sure we are in a valid state... however, vehicles can also
	 * turn on road stop tiles, so only clear the 'road stop' state
	 * bits and only when the state was 'in road stop', otherwise
	 * we'll end up clearing the turn around bits. */
	RoadVehicle *rv = RoadVehicle::From(v);
	if (HasBit(rv->state, RVS_IN_DT_ROAD_STOP)) rv->state &= RVSB_ROAD_STOP_TRACKDIR_MASK;

	return nullptr;
}

CommandCost RemoveRoadWaypointStop(TileIndex tile, DoCommandFlag flags, int replacement_spec_index)
{
	Waypoint *wp = Waypoint::GetByTile(tile);

	if (_current_company != OWNER_WATER) {
		CommandCost ret = CheckOwnership(wp->owner);
		if (ret.Failed()) return ret;
	}

	/* don't do the check for drive-through road stops when company bankrupts */
	if (!(flags & DC_BANKRUPT)) {
		CommandCost ret = EnsureNoVehicleOnGround(tile);
		if (ret.Failed()) return ret;
	}

	const RoadStopSpec *spec = GetRoadStopSpec(tile);

	if (flags & DC_EXEC) {
		/* Update company infrastructure counts. */
		for (RoadTramType rtt : _roadtramtypes) {
			RoadType rt = GetRoadType(tile, rtt);
			UpdateCompanyRoadInfrastructure(rt, GetRoadOwner(tile, rtt), -static_cast<int>(ROAD_STOP_TRACKBIT_FACTOR));
		}

		Company::Get(wp->owner)->infrastructure.station--;
		DirtyCompanyInfrastructureWindows(wp->owner);

		DeleteAnimatedTile(tile);

		uint specindex = GetCustomRoadStopSpecIndex(tile);

		DeleteNewGRFInspectWindow(GSF_ROADSTOPS, tile.base());

		DoClearSquare(tile);

		wp->rect.AfterRemoveTile(wp, tile);

		wp->RemoveRoadStopTileData(tile);
		if ((int)specindex != replacement_spec_index) DeallocateRoadStopSpecFromStation(wp, specindex);

		if (replacement_spec_index < 0) {
			MakeRoadWaypointStationAreaSmaller(wp, wp->road_waypoint_area);

			UpdateStationSignCoord(wp);

			/* if we deleted the whole waypoint, delete the road facility. */
			if (wp->road_waypoint_area.tile == INVALID_TILE) {
				wp->facilities &= ~(FACIL_BUS_STOP | FACIL_TRUCK_STOP);
				SetWindowWidgetDirty(WC_STATION_VIEW, wp->index, WID_SV_ROADVEHS);
				wp->UpdateVirtCoord();
				DeleteStationIfEmpty(wp);
			}
		}

		NotifyRoadLayoutChanged(false);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, spec != nullptr ? spec->GetClearCost(PR_CLEAR_STATION_TRUCK) : _price[PR_CLEAR_STATION_TRUCK]);
}

/**
 * Remove a bus station/truck stop
 * @param tile TileIndex been queried
 * @param flags operation to perform
 * @param replacement_spec_index replacement spec index to avoid deallocating, if < 0, tile is not being replaced
 * @return cost or failure of operation
 */
CommandCost RemoveRoadStop(TileIndex tile, DoCommandFlag flags, int replacement_spec_index)
{
	if (IsRoadWaypoint(tile)) {
		return RemoveRoadWaypointStop(tile, flags, replacement_spec_index);
	}

	Station *st = Station::GetByTile(tile);

	if (_current_company != OWNER_WATER) {
		CommandCost ret = CheckOwnership(st->owner);
		if (ret.Failed()) return ret;
	}

	bool is_truck = IsTruckStop(tile);

	RoadStop **primary_stop;
	RoadStop *cur_stop;
	if (is_truck) { // truck stop
		primary_stop = &st->truck_stops;
		cur_stop = RoadStop::GetByTile(tile, RoadStopType::Truck);
	} else {
		primary_stop = &st->bus_stops;
		cur_stop = RoadStop::GetByTile(tile, RoadStopType::Bus);
	}

	assert(cur_stop != nullptr);

	/* don't do the check for drive-through road stops when company bankrupts */
	if (IsDriveThroughStopTile(tile) && (flags & DC_BANKRUPT)) {
		/* remove the 'going through road stop' status from all vehicles on that tile */
		if (flags & DC_EXEC) FindVehicleOnPos(tile, VEH_ROAD, nullptr, &ClearRoadStopStatusEnum);
	} else {
		CommandCost ret = EnsureNoVehicleOnGround(tile);
		if (ret.Failed()) return ret;
	}

	const RoadStopSpec *spec = GetRoadStopSpec(tile);

	if (flags & DC_EXEC) {
		ZoningMarkDirtyStationCoverageArea(st);
		if (*primary_stop == cur_stop) {
			/* removed the first stop in the list */
			*primary_stop = cur_stop->next;
			/* removed the only stop? */
			if (*primary_stop == nullptr) {
				st->facilities &= (is_truck ? ~FACIL_TRUCK_STOP : ~FACIL_BUS_STOP);
				SetWindowClassesDirty(WC_VEHICLE_ORDERS);
			}
		} else {
			/* tell the predecessor in the list to skip this stop */
			RoadStop *pred = *primary_stop;
			while (pred->next != cur_stop) pred = pred->next;
			pred->next = cur_stop->next;
		}

		/* Update company infrastructure counts. */
		for (RoadTramType rtt : _roadtramtypes) {
			RoadType rt = GetRoadType(tile, rtt);
			UpdateCompanyRoadInfrastructure(rt, GetRoadOwner(tile, rtt), -static_cast<int>(ROAD_STOP_TRACKBIT_FACTOR));
		}

		Company::Get(st->owner)->infrastructure.station--;
		DirtyCompanyInfrastructureWindows(st->owner);

		DeleteAnimatedTile(tile);

		uint specindex = GetCustomRoadStopSpecIndex(tile);

		DeleteNewGRFInspectWindow(GSF_ROADSTOPS, tile.base());

		if (IsDriveThroughStopTile(tile)) {
			/* Clears the tile for us */
			cur_stop->ClearDriveThrough();
		} else {
			DoClearSquare(tile);
		}

		delete cur_stop;

		/* Make sure no vehicle is going to the old roadstop */
		for (RoadVehicle *v : RoadVehicle::IterateFrontOnly()) {
			if (v->current_order.IsType(OT_GOTO_STATION) && v->dest_tile == tile) {
				v->SetDestTile(v->GetOrderStationLocation(st->index));
			}
		}

		st->rect.AfterRemoveTile(st, tile);

		if (replacement_spec_index < 0) st->AfterStationTileSetChange(false, is_truck ? StationType::Truck: StationType::Bus);

		st->RemoveRoadStopTileData(tile);
		if ((int)specindex != replacement_spec_index) DeallocateRoadStopSpecFromStation(st, specindex);

		/* Update the tile area of the truck/bus stop */
		if (is_truck) {
			st->truck_station.Clear();
			for (const RoadStop *rs = st->truck_stops; rs != nullptr; rs = rs->next) st->truck_station.Add(rs->xy);
		} else {
			st->bus_station.Clear();
			for (const RoadStop *rs = st->bus_stops; rs != nullptr; rs = rs->next) st->bus_station.Add(rs->xy);
		}

		NotifyRoadLayoutChanged(false);
	}

	Price category = is_truck ? PR_CLEAR_STATION_TRUCK : PR_CLEAR_STATION_BUS;
	return CommandCost(EXPENSES_CONSTRUCTION, spec != nullptr ? spec->GetClearCost(category) : _price[category]);
}

/**
 * Remove a tile area of road stop or road waypoints
 * @param flags operation to perform
 * @param roadstop_area tile area of road stop or road waypoint tiles to remove
 * @param road_waypoint Whether to remove road waypoints or road stops
 * @param remove_road Remove roads of drive-through stops?
 * @return the cost of this operation or an error
 */
static CommandCost RemoveGenericRoadStop(DoCommandFlag flags, const TileArea &roadstop_area, bool road_waypoint, bool remove_road)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);
	CommandCost last_error(STR_ERROR_THERE_IS_NO_STATION);
	bool had_success = false;

	for (TileIndex cur_tile : roadstop_area) {
		/* Make sure the specified tile is a road stop of the correct type */
		if (!IsTileType(cur_tile, MP_STATION) || !IsAnyRoadStop(cur_tile) || IsRoadWaypoint(cur_tile) != road_waypoint) continue;

		/* Save information on to-be-restored roads before the stop is removed. */
		RoadBits road_bits = ROAD_NONE;
		RoadType road_type[] = { INVALID_ROADTYPE, INVALID_ROADTYPE };
		Owner road_owner[] = { OWNER_NONE, OWNER_NONE };
		DisallowedRoadDirections drd = DRD_NONE;
		if (IsDriveThroughStopTile(cur_tile)) {
			for (RoadTramType rtt : _roadtramtypes) {
				road_type[rtt] = GetRoadType(cur_tile, rtt);
				if (road_type[rtt] == INVALID_ROADTYPE) continue;
				road_owner[rtt] = GetRoadOwner(cur_tile, rtt);
				/* If we don't want to preserve our roads then restore only roads of others. */
				if (remove_road && road_owner[rtt] == _current_company) road_type[rtt] = INVALID_ROADTYPE;
			}
			road_bits = AxisToRoadBits(GetDriveThroughStopAxis(cur_tile));
			drd = GetDriveThroughStopDisallowedRoadDirections(cur_tile);
		}

		CommandCost ret = RemoveRoadStop(cur_tile, flags);
		if (ret.Failed()) {
			last_error = ret;
			continue;
		}
		cost.AddCost(ret);
		had_success = true;

		/* Restore roads. */
		if ((flags & DC_EXEC) && (road_type[RTT_ROAD] != INVALID_ROADTYPE || road_type[RTT_TRAM] != INVALID_ROADTYPE)) {
			MakeRoadNormal(cur_tile, road_bits, road_type[RTT_ROAD], road_type[RTT_TRAM], ClosestTownFromTile(cur_tile, UINT_MAX)->index,
					road_owner[RTT_ROAD], road_owner[RTT_TRAM]);
			if (drd != DRD_NONE) SetDisallowedRoadDirections(cur_tile, drd);

			/* Update company infrastructure counts. */
			int count = CountBits(road_bits);
			UpdateCompanyRoadInfrastructure(road_type[RTT_ROAD], road_owner[RTT_ROAD], count);
			UpdateCompanyRoadInfrastructure(road_type[RTT_TRAM], road_owner[RTT_TRAM], count);
		}
		if (flags & DC_EXEC) UpdateRoadCachedOneWayStatesAroundTile(cur_tile);
	}

	return had_success ? cost : last_error;
}

/**
 * Remove bus or truck stops.
 * @param flags Operation to perform.
 * @param tile Northernmost tile of the removal area.
 * @param width Width of the removal area.
 * @param height Height of the removal area.
 * @param stop_type Type of stop (bus/truck).
 * @param remove_road Remove roads of drive-through stops?
 * @return The cost of this operation or an error.
 */
CommandCost CmdRemoveRoadStop(DoCommandFlag flags, TileIndex tile, uint8_t width, uint8_t height, RoadStopType stop_type, bool remove_road)
{
	if (stop_type >= RoadStopType::End) return CMD_ERROR;
	/* Check for incorrect width / height. */
	if (width == 0 || height == 0) return CMD_ERROR;
	/* Check if the first tile and the last tile are valid */
	if (!IsValidTile(tile) || TileAddWrap(tile, width - 1, height - 1) == INVALID_TILE) return CMD_ERROR;
	/* Bankrupting company is not supposed to remove roads, there may be road vehicles. */
	if (remove_road && (flags & DC_BANKRUPT)) return CMD_ERROR;

	TileArea roadstop_area(tile, width, height);

	return RemoveGenericRoadStop(flags, roadstop_area, false, remove_road);
}

/**
 * Remove road waypoints.
 * @param flags operation to perform
 * @param start tile of road waypoint piece to remove
 * @param end other edge of the rect to remove
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveFromRoadWaypoint(DoCommandFlag flags, TileIndex start, TileIndex end)
{
	if (end == 0) end = start;
	if (start >= Map::Size() || end >= Map::Size()) return CMD_ERROR;

	TileArea roadstop_area(start, end);

	return RemoveGenericRoadStop(flags, roadstop_area, true, false);
}

/**
 * Get a possible noise reduction factor based on distance from town center.
 * The further you get, the less noise you generate.
 * So all those folks at city council can now happily slee...  work in their offices
 * @param as airport information
 * @param distance minimum distance between town and airport
 * @return the noise that will be generated, according to distance
 */
uint8_t GetAirportNoiseLevelForDistance(const AirportSpec *as, uint distance)
{
	/* 0 cannot be accounted, and 1 is the lowest that can be reduced from town.
	 * So no need to go any further*/
	if (as->noise_level < 2) return as->noise_level;

	auto tolerance = _settings_game.difficulty.town_council_tolerance;
	if (tolerance == TOWN_COUNCIL_PERMISSIVE) tolerance = TOWN_COUNCIL_LENIENT;

	/* The steps for measuring noise reduction are based on the "magical" (and arbitrary) 8 base distance
	 * adding the town_council_tolerance 4 times, as a way to graduate, depending of the tolerance.
	 * Basically, it says that the less tolerant a town is, the bigger the distance before
	 * an actual decrease can be granted */
	uint8_t town_tolerance_distance = 8 + (tolerance * 4);

	/* now, we want to have the distance segmented using the distance judged bareable by town
	 * This will give us the coefficient of reduction the distance provides. */
	uint noise_reduction = distance / town_tolerance_distance;

	/* If the noise reduction equals the airport noise itself, don't give it for free.
	 * Otherwise, simply reduce the airport's level. */
	return noise_reduction >= as->noise_level ? 1 : as->noise_level - noise_reduction;
}

/**
 * Finds the town nearest to given airport. Based on minimal manhattan distance to any airport's tile.
 * If two towns have the same distance, town with lower index is returned.
 * @param as airport's description
 * @param rotation airport's rotation
 * @param tile origin tile (top corner of the airport)
 * @param it An iterator over all airport tiles (consumed)
 * @param[out] mindist Minimum distance to town
 * @return nearest town to airport
 */
Town *AirportGetNearestTown(const AirportSpec *as, Direction rotation, TileIndex tile, TileIterator &&it, uint &mindist)
{
	assert(Town::GetNumItems() > 0);

	Town *nearest = nullptr;

	auto width = as->size_x;
	auto height = as->size_y;
	if (rotation == DIR_E || rotation == DIR_W) std::swap(width, height);

	uint perimeter_min_x = TileX(tile);
	uint perimeter_min_y = TileY(tile);
	uint perimeter_max_x = perimeter_min_x + width - 1;
	uint perimeter_max_y = perimeter_min_y + height - 1;

	mindist = UINT_MAX - 1; // prevent overflow

	for (TileIndex cur_tile = *it; cur_tile != INVALID_TILE; cur_tile = ++it) {
		assert(IsInsideBS(TileX(cur_tile), perimeter_min_x, width));
		assert(IsInsideBS(TileY(cur_tile), perimeter_min_y, height));
		if (TileX(cur_tile) == perimeter_min_x || TileX(cur_tile) == perimeter_max_x || TileY(cur_tile) == perimeter_min_y || TileY(cur_tile) == perimeter_max_y) {
			Town *t = CalcClosestTownFromTile(cur_tile, mindist + 1);
			if (t == nullptr) continue;

			uint dist = DistanceManhattan(t->xy, cur_tile);
			if (dist == mindist && t->index < nearest->index) nearest = t;
			if (dist < mindist) {
				nearest = t;
				mindist = dist;
			}
		}
	}

	return nearest;
}

/**
 * Finds the town nearest to given existing airport. Based on minimal manhattan distance to any airport's tile.
 * If two towns have the same distance, town with lower index is returned.
 * @param station existing station with airport
 * @param[out] mindist Minimum distance to town
 * @return nearest town to airport
 */
static Town *AirportGetNearestTown(const Station *st, uint &mindist)
{
	return AirportGetNearestTown(st->airport.GetSpec(), st->airport.rotation, st->airport.tile, AirportTileIterator(st), mindist);
}


/** Recalculate the noise generated by the airports of each town */
void UpdateAirportsNoise()
{
	if (_town_noise_no_update) return;

	for (Town *t : Town::Iterate()) t->noise_reached = 0;

	for (const Station *st : Station::Iterate()) {
		if (st->airport.tile != INVALID_TILE && st->airport.type != AT_OILRIG) {
			uint dist;
			Town *nearest = AirportGetNearestTown(st, dist);
			nearest->noise_reached += GetAirportNoiseLevelForDistance(st->airport.GetSpec(), dist);
		}
	}
}


/**
 * Checks if an airport can be removed (no aircraft on it or landing)
 * @param st Station whose airport is to be removed
 * @param flags Operation to perform
 * @return Cost or failure of operation
 */
static CommandCost CanRemoveAirport(Station *st, DoCommandFlag flags)
{
	for (const Aircraft *a : Aircraft::Iterate()) {
		if (!a->IsNormalAircraft()) continue;
		if (a->targetairport == st->index && a->state != FLYING)
			return CommandCost(STR_ERROR_AIRCRAFT_IN_THE_WAY);
	}

	CommandCost cost(EXPENSES_CONSTRUCTION);

	for (TileIndex tile_cur : st->airport) {
		if (!st->TileBelongsToAirport(tile_cur)) continue;

		CommandCost ret = EnsureNoVehicleOnGround(tile_cur);
		if (ret.Failed()) return ret;

		cost.AddCost(_price[PR_CLEAR_STATION_AIRPORT]);
	}

	return cost;
}


/**
 * Place an Airport.
 * @param flags operation to perform
 * @param tile tile where airport will be built
 * @param airport_type airport type, @see airport.h
 * @param layout airport layout
 * @param station_to_join station ID to join (NEW_STATION if build new one)
 * @param allow_adjacent allow airports directly adjacent to other airports.
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildAirport(DoCommandFlag flags, TileIndex tile, uint8_t airport_type, uint8_t layout, StationID station_to_join, bool allow_adjacent)
{
	bool reuse = (station_to_join != NEW_STATION);
	if (!reuse) station_to_join = INVALID_STATION;
	bool distant_join = (station_to_join != INVALID_STATION);

	if (distant_join && (!_settings_game.station.distant_join_stations || !Station::IsValidID(station_to_join))) return CMD_ERROR;

	if (airport_type >= NUM_AIRPORTS) return CMD_ERROR;

	CommandCost ret = CheckIfAuthorityAllowsNewStation(tile, flags);
	if (ret.Failed()) return ret;

	/* Check if a valid, buildable airport was chosen for construction */
	const AirportSpec *as = AirportSpec::Get(airport_type);
	if (!as->IsAvailable() || layout >= as->layouts.size()) return CMD_ERROR;
	if (!as->IsWithinMapBounds(layout, tile)) return CMD_ERROR;

	Direction rotation = as->layouts[layout].rotation;
	int w = as->size_x;
	int h = as->size_y;
	if (rotation == DIR_E || rotation == DIR_W) Swap(w, h);
	TileArea airport_area = TileArea(tile, w, h);

	if (w > _settings_game.station.station_spread || h > _settings_game.station.station_spread) {
		return CommandCost(STR_ERROR_STATION_TOO_SPREAD_OUT);
	}

	StationID est = INVALID_STATION;
	AirportTileTableIterator iter(as->layouts[layout].tiles.data(), tile);
	CommandCost cost = CheckFlatLandAirport(iter, flags, &est);
	if (cost.Failed()) return cost;

	Station *st = nullptr;
	ret = FindJoiningStation(est, station_to_join, allow_adjacent, airport_area, &st, STR_ERROR_MUST_DEMOLISH_AIRPORT_FIRST);
	if (ret.Failed()) return ret;

	/* Distant join */
	if (st == nullptr && distant_join) st = Station::GetIfValid(station_to_join);

	ret = BuildStationPart(&st, flags, reuse, airport_area, (GetAirport(airport_type)->flags & AirportFTAClass::AIRPLANES) ? STATIONNAMING_AIRPORT : STATIONNAMING_HELIPORT);
	if (ret.Failed()) return ret;

	/* action to be performed */
	enum {
		AIRPORT_NEW,      // airport is a new station
		AIRPORT_ADD,      // add an airport to an existing station
		AIRPORT_UPGRADE,  // upgrade the airport in a station
	} action =
		(est != INVALID_STATION) ? AIRPORT_UPGRADE :
		(st != nullptr) ? AIRPORT_ADD : AIRPORT_NEW;

	if (action == AIRPORT_ADD && st->airport.tile != INVALID_TILE) {
		return CommandCost(STR_ERROR_TOO_CLOSE_TO_ANOTHER_AIRPORT);
	}

	if (action == AIRPORT_UPGRADE && airport_type == st->airport.type && layout == st->airport.layout && st->airport.tile == tile) {
		return CommandCost(STR_ERROR_ALREADY_BUILT);
	}

	/* The noise level is the noise from the airport and reduce it to account for the distance to the town center. */
	AirportTileTableIterator nearest_town_iter = iter;
	uint dist;
	Town *nearest = AirportGetNearestTown(as, rotation, tile, std::move(nearest_town_iter), dist);
	uint newnoise_level = nearest->noise_reached + GetAirportNoiseLevelForDistance(as, dist);

	if (action == AIRPORT_UPGRADE) {
		uint old_dist;
		Town *old_nearest = AirportGetNearestTown(st, old_dist);
		if (old_nearest == nearest) {
			newnoise_level -= GetAirportNoiseLevelForDistance(st->airport.GetSpec(), old_dist);
		}
	}

	/* Check if local auth would allow a new airport */
	StringID authority_refuse_message = STR_NULL;
	Town *authority_refuse_town = nullptr;

	if (_settings_game.economy.station_noise_level) {
		/* do not allow to build a new airport if this raise the town noise over the maximum allowed by town */
		if (newnoise_level > nearest->MaxTownNoise()) {
			authority_refuse_message = STR_ERROR_LOCAL_AUTHORITY_REFUSES_NOISE;
			authority_refuse_town = nearest;
		}
	} else if (_settings_game.difficulty.town_council_tolerance != TOWN_COUNCIL_PERMISSIVE && action != AIRPORT_UPGRADE) {
		Town *t = ClosestTownFromTile(tile, UINT_MAX);
		uint num = 0;
		for (const Station *st : Station::Iterate()) {
			if (st->town == t && (st->facilities & FACIL_AIRPORT) && st->airport.type != AT_OILRIG) num++;
		}
		if (num >= 2) {
			authority_refuse_message = STR_ERROR_LOCAL_AUTHORITY_REFUSES_AIRPORT;
			authority_refuse_town = t;
		}
	}

	if (authority_refuse_message != STR_NULL) {
		SetDParam(0, authority_refuse_town->index);
		return CommandCost(authority_refuse_message);
	}

	if (action == AIRPORT_UPGRADE) {
		/* check that the old airport can be removed */
		CommandCost r = CanRemoveAirport(st, flags);
		if (r.Failed()) return r;
		cost.AddCost(r);
	}

	for (AirportTileTableIterator iter(as->layouts[layout].tiles.data(), tile); iter != INVALID_TILE; ++iter) {
		cost.AddCost(_price[PR_BUILD_STATION_AIRPORT]);
	}

	if (flags & DC_EXEC) {
		if (action == AIRPORT_UPGRADE) {
			/* delete old airport if upgrading */

			ZoningMarkDirtyStationCoverageArea(st);

			for (uint i = 0; i < st->airport.GetNumHangars(); ++i) {
				TileIndex tile_cur = st->airport.GetHangarTile(i);
				OrderBackup::Reset(tile_cur, false);
				CloseWindowById(WC_VEHICLE_DEPOT, tile_cur.base());
			}

			uint old_dist;
			Town *old_nearest = AirportGetNearestTown(st, old_dist);

			if (old_nearest != nearest) {
				old_nearest->noise_reached -= GetAirportNoiseLevelForDistance(st->airport.GetSpec(), old_dist);
				if (_settings_game.economy.station_noise_level) {
					SetWindowDirty(WC_TOWN_VIEW, st->town->index);
				}
			}

			for (TileIndex tile_cur : st->airport) {
				DeleteAnimatedTile(tile_cur);
				DoClearSquare(tile_cur);
				DeleteNewGRFInspectWindow(GSF_AIRPORTTILES, tile_cur.base());
			}

			st->rect.AfterRemoveRect(st, st->airport);
			st->airport.Clear();
		}

		/* Always add the noise, so there will be no need to recalculate when option toggles */
		nearest->noise_reached = newnoise_level;

		st->AddFacility(FACIL_AIRPORT, tile);
		st->airport.type = airport_type;
		st->airport.layout = layout;
		st->airport.flags = 0;
		st->airport.rotation = rotation;

		st->rect.BeforeAddRect(tile, w, h, StationRect::ADD_TRY);

		for (AirportTileTableIterator iter(as->layouts[layout].tiles.data(), tile); iter != INVALID_TILE; ++iter) {
			MakeAirport(iter, st->owner, st->index, iter.GetStationGfx(), WATER_CLASS_INVALID);
			SetStationTileRandomBits(iter, GB(Random(), 0, 4));
			st->airport.Add(iter);

			if (AirportTileSpec::Get(GetTranslatedAirportTileID(iter.GetStationGfx()))->animation.status != ANIM_STATUS_NO_ANIMATION) AddAnimatedTile(iter);
		}

		/* Only call the animation trigger after all tiles have been built */
		for (AirportTileTableIterator iter(as->layouts[layout].tiles.data(), tile); iter != INVALID_TILE; ++iter) {
			AirportTileAnimationTrigger(st, iter, AAT_BUILT);
		}

		if (action != AIRPORT_NEW) UpdateAirplanesOnNewStation(st);

		if (action == AIRPORT_UPGRADE) {
			UpdateStationSignCoord(st);
		} else {
			Company::Get(st->owner)->infrastructure.airport++;
		}

		st->AfterStationTileSetChange(true, StationType::Airport);
		ZoningMarkDirtyStationCoverageArea(st);
		InvalidateWindowData(WC_STATION_VIEW, st->index, -1);

		if (_settings_game.economy.station_noise_level) {
			SetWindowDirty(WC_TOWN_VIEW, nearest->index);
		}
	}

	return cost;
}

/**
 * Remove an airport
 * @param tile TileIndex been queried
 * @param flags operation to perform
 * @return cost or failure of operation
 */
static CommandCost RemoveAirport(TileIndex tile, DoCommandFlag flags)
{
	Station *st = Station::GetByTile(tile);

	if (_current_company != OWNER_WATER) {
		CommandCost ret = CheckOwnership(st->owner);
		if (ret.Failed()) return ret;
	}

	CommandCost cost = CanRemoveAirport(st, flags);
	if (cost.Failed()) return cost;

	if (flags & DC_EXEC) {
		for (uint i = 0; i < st->airport.GetNumHangars(); ++i) {
			TileIndex tile_cur = st->airport.GetHangarTile(i);
			OrderBackup::Reset(tile_cur, false);
			CloseWindowById(WC_VEHICLE_DEPOT, tile_cur.base());
		}

		ZoningMarkDirtyStationCoverageArea(st);
		/* The noise level is the noise from the airport and reduce it to account for the distance to the town center.
		 * And as for construction, always remove it, even if the setting is not set, in order to avoid the
		 * need of recalculation */
		uint dist;
		Town *nearest = AirportGetNearestTown(st, dist);
		nearest->noise_reached -= GetAirportNoiseLevelForDistance(st->airport.GetSpec(), dist);

		if (_settings_game.economy.station_noise_level) {
			SetWindowDirty(WC_TOWN_VIEW, nearest->index);
		}

		for (TileIndex tile_cur : st->airport) {
			if (!st->TileBelongsToAirport(tile_cur)) continue;

			DeleteAnimatedTile(tile_cur);
			DoClearSquare(tile_cur);
			DeleteNewGRFInspectWindow(GSF_AIRPORTTILES, tile_cur.base());
		}

		/* Clear the persistent storage. */
		delete st->airport.psa;

		st->rect.AfterRemoveRect(st, st->airport);

		st->airport.Clear();
		st->facilities &= ~FACIL_AIRPORT;
		SetWindowClassesDirty(WC_VEHICLE_ORDERS);

		InvalidateWindowData(WC_STATION_VIEW, st->index, -1);

		Company::Get(st->owner)->infrastructure.airport--;

		st->AfterStationTileSetChange(false, StationType::Airport);

		DeleteNewGRFInspectWindow(GSF_AIRPORTS, st->index);
	}

	return cost;
}

/**
 * Open/close an airport to incoming aircraft.
 * @param flags Operation to perform.
 * @param station_id Station ID of the airport.
 * @return the cost of this operation or an error
 */
CommandCost CmdOpenCloseAirport(DoCommandFlag flags, StationID station_id)
{
	if (!Station::IsValidID(station_id)) return CMD_ERROR;
	Station *st = Station::Get(station_id);

	if (!(st->facilities & FACIL_AIRPORT) || st->owner == OWNER_NONE) return CMD_ERROR;

	CommandCost ret = CheckOwnership(st->owner);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		st->airport.flags ^= AIRPORT_CLOSED_block;
		SetWindowWidgetDirty(WC_STATION_VIEW, st->index, WID_SV_CLOSE_AIRPORT);
	}
	return CommandCost();
}

/**
 * Tests whether the company's vehicles have this station in orders
 * @param station station ID
 * @param include_company If true only check vehicles of \a company, if false only check vehicles of other companies
 * @param company company ID
 */
bool HasStationInUse(StationID station, bool include_company, CompanyID company)
{
	bool found = false;
	IterateOrderRefcountMapForDestinationID(station, [&](CompanyID cid, OrderType order_type, VehicleType veh_type, uint32_t refcount) {
		if ((cid == company) == include_company) {
			if (order_type == OT_GOTO_STATION || order_type == OT_GOTO_WAYPOINT) {
				found = true;
				return false;
			}
		}
		return true;
	});
	return found;
}

static const TileIndexDiffC _dock_tileoffs_chkaround[] = {
	{-1,  0},
	{ 0,  0},
	{ 0,  0},
	{ 0, -1}
};
static const uint8_t _dock_w_chk[4] = { 2, 1, 2, 1 };
static const uint8_t _dock_h_chk[4] = { 1, 2, 1, 2 };

/**
 * Build a dock/haven.
 * @param flags operation to perform
 * @param tile tile where dock will be built
 * @param station_to_join station ID to join (NEW_STATION if build new one)
 * @param adjacent allow docks directly adjacent to other docks.
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildDock(DoCommandFlag flags, TileIndex tile, StationID station_to_join, bool adjacent)
{
	bool reuse = (station_to_join != NEW_STATION);
	if (!reuse) station_to_join = INVALID_STATION;
	bool distant_join = (station_to_join != INVALID_STATION);

	if (distant_join && (!_settings_game.station.distant_join_stations || !Station::IsValidID(station_to_join))) return CMD_ERROR;

	DiagDirection direction = GetInclinedSlopeDirection(GetTileSlope(tile));
	if (direction == INVALID_DIAGDIR) return CommandCost(STR_ERROR_SITE_UNSUITABLE);
	direction = ReverseDiagDir(direction);

	/* Docks cannot be placed on rapids */
	if (HasTileWaterGround(tile)) return CommandCost(STR_ERROR_SITE_UNSUITABLE);

	CommandCost ret = CheckIfAuthorityAllowsNewStation(tile, flags);
	if (ret.Failed()) return ret;

	if (IsBridgeAbove(tile) && !_settings_game.construction.allow_docks_under_bridges) return CommandCost(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

	CommandCost cost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_STATION_DOCK]);
	ret = Command<CMD_LANDSCAPE_CLEAR>::Do(flags, tile);
	if (ret.Failed()) return ret;
	cost.AddCost(ret);

	TileIndex flat_tile = tile + TileOffsByDiagDir(direction);

	if (!HasTileWaterGround(flat_tile) || !IsTileFlat(flat_tile)) {
		return CommandCost(STR_ERROR_SITE_UNSUITABLE);
	}

	if (IsBridgeAbove(flat_tile) && !_settings_game.construction.allow_docks_under_bridges) return CommandCost(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

	/* Get the water class of the water tile before it is cleared.*/
	WaterClass wc = GetWaterClass(flat_tile);

	bool add_cost = !IsWaterTile(flat_tile);
	ret = Command<CMD_LANDSCAPE_CLEAR>::Do(flags | DC_ALLOW_REMOVE_WATER, flat_tile);
	if (ret.Failed()) return ret;
	if (add_cost) cost.AddCost(ret);

	TileIndex adjacent_tile = flat_tile + TileOffsByDiagDir(direction);
	if (!IsTileType(adjacent_tile, MP_WATER) || !IsTileFlat(adjacent_tile)) {
		return CommandCost(STR_ERROR_SITE_UNSUITABLE);
	}

	TileArea dock_area = TileArea(tile + ToTileIndexDiff(_dock_tileoffs_chkaround[direction]),
			_dock_w_chk[direction], _dock_h_chk[direction]);

	/* middle */
	Station *st = nullptr;
	ret = FindJoiningStation(INVALID_STATION, station_to_join, adjacent, dock_area, &st);
	if (ret.Failed()) return ret;

	/* Distant join */
	if (st == nullptr && distant_join) st = Station::GetIfValid(station_to_join);

	ret = BuildStationPart(&st, flags, reuse, dock_area, STATIONNAMING_DOCK);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		st->ship_station.Add(tile);
		st->ship_station.Add(flat_tile);
		st->AddFacility(FACIL_DOCK, tile);

		st->rect.BeforeAddRect(dock_area.tile, dock_area.w, dock_area.h, StationRect::ADD_TRY);

		/* If the water part of the dock is on a canal, update infrastructure counts.
		 * This is needed as we've cleared that tile before.
		 * Clearing object tiles may result in water tiles which are already accounted for in the water infrastructure total.
		 * See: MakeWaterKeepingClass() */
		if (wc == WATER_CLASS_CANAL && !(HasTileWaterClass(flat_tile) && GetWaterClass(flat_tile) == WATER_CLASS_CANAL && IsTileOwner(flat_tile, _current_company))) {
			Company::Get(st->owner)->infrastructure.water++;
		}
		Company::Get(st->owner)->infrastructure.station += 2;

		MakeDock(tile, st->owner, st->index, direction, wc);
		UpdateStationDockingTiles(st);

		st->AfterStationTileSetChange(true, StationType::Dock);
		ZoningMarkDirtyStationCoverageArea(st);
	}

	return cost;
}

void RemoveDockingTile(TileIndex t)
{
	for (DiagDirection d = DIAGDIR_BEGIN; d != DIAGDIR_END; d++) {
		TileIndex tile = t + TileOffsByDiagDir(d);
		if (!IsValidTile(tile)) continue;

		if (IsTileType(tile, MP_STATION)) {
			Station *st = Station::GetByTile(tile);
			if (st != nullptr) UpdateStationDockingTiles(st);
		} else if (IsTileType(tile, MP_INDUSTRY)) {
			Station *neutral = Industry::GetByTile(tile)->neutral_station;
			if (neutral != nullptr) UpdateStationDockingTiles(neutral);
		}
	}
}

/**
 * Clear docking tile status from tiles around a removed dock, if the tile has
 * no neighbours which would keep it as a docking tile.
 * @param tile Ex-dock tile to check.
 */
void ClearDockingTilesCheckingNeighbours(TileIndex tile)
{
	assert(IsValidTile(tile));

	/* Clear and maybe re-set docking tile */
	for (DiagDirection d = DIAGDIR_BEGIN; d != DIAGDIR_END; d++) {
		TileIndex docking_tile = tile + TileOffsByDiagDir(d);
		if (!IsValidTile(docking_tile)) continue;

		if (IsPossibleDockingTile(docking_tile)) {
			SetDockingTile(docking_tile, false);
			CheckForDockingTile(docking_tile);
		}
	}
}

/**
 * Find the part of a dock that is land-based
 * @param t Dock tile to find land part of
 * @return tile of land part of dock
 */
static TileIndex FindDockLandPart(TileIndex t)
{
	assert(IsDockTile(t));

	StationGfx gfx = GetStationGfx(t);
	if (gfx < GFX_DOCK_BASE_WATER_PART) return t;

	for (DiagDirection d = DIAGDIR_BEGIN; d != DIAGDIR_END; d++) {
		TileIndex tile = t + TileOffsByDiagDir(d);
		if (!IsValidTile(tile)) continue;
		if (!IsDockTile(tile)) continue;
		if (GetStationGfx(tile) < GFX_DOCK_BASE_WATER_PART && tile + TileOffsByDiagDir(GetDockDirection(tile)) == t) return tile;
	}

	return INVALID_TILE;
}

/**
 * Remove a dock
 * @param tile TileIndex been queried
 * @param flags operation to perform
 * @return cost or failure of operation
 */
static CommandCost RemoveDock(TileIndex tile, DoCommandFlag flags)
{
	Station *st = Station::GetByTile(tile);
	CommandCost ret = CheckOwnership(st->owner);
	if (ret.Failed()) return ret;

	if (!IsDockTile(tile)) return CMD_ERROR;

	TileIndex tile1 = FindDockLandPart(tile);
	if (tile1 == INVALID_TILE) return CMD_ERROR;
	TileIndex tile2 = tile1 + TileOffsByDiagDir(GetDockDirection(tile1));

	ret = EnsureNoVehicleOnGround(tile1);
	if (ret.Succeeded()) ret = EnsureNoVehicleOnGround(tile2);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		ZoningMarkDirtyStationCoverageArea(st);

		DoClearSquare(tile1);
		MarkTileDirtyByTile(tile1);
		MakeWaterKeepingClass(tile2, st->owner);

		st->rect.AfterRemoveTile(st, tile1);
		st->rect.AfterRemoveTile(st, tile2);

		MakeShipStationAreaSmaller(st);
		if (st->ship_station.tile == INVALID_TILE) {
			st->ship_station.Clear();
			st->docking_station.Clear();
			st->docking_tiles.clear();
			st->facilities &= ~FACIL_DOCK;
			SetWindowClassesDirty(WC_VEHICLE_ORDERS);
		}

		Company::Get(st->owner)->infrastructure.station -= 2;

		st->AfterStationTileSetChange(false, StationType::Dock);

		ClearDockingTilesCheckingNeighbours(tile1);
		ClearDockingTilesCheckingNeighbours(tile2);

		for (Ship *s : Ship::IterateFrontOnly()) {
			/* Find all ships going to our dock. */
			if (s->current_order.GetDestination() != st->index) {
				continue;
			}

			/* Find ships that are marked as "loading" but are no longer on a
			 * docking tile. Force them to leave the station (as they were loading
			 * on the removed dock). */
			if (s->current_order.IsType(OT_LOADING) && !(IsDockingTile(s->tile) && IsShipDestinationTile(s->tile, st->index))) {
				s->LeaveStation();
			}

			/* If we no longer have a dock, mark the order as invalid and send
			 * the ship to the next order (or, if there is none, make it
			 * wander the world). */
			if (s->current_order.IsType(OT_GOTO_STATION) && !(st->facilities & FACIL_DOCK)) {
				s->SetDestTile(s->GetOrderStationLocation(st->index));
			}
		}
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_STATION_DOCK]);
}

#include "table/station_land.h"

/**
 * Get station tile layout for a station type and its station gfx.
 * @param st Station type to draw.
 * @param gfx StationGfx of tile to draw.
 * @return Tile layout to draw.
 */
const DrawTileSprites *GetStationTileLayout(StationType st, uint8_t gfx)
{
	const auto &layouts = _station_display_datas[to_underlying(st)];
	if (gfx >= layouts.size()) gfx &= 1;
	return layouts.data() + gfx;
}

/**
 * Check whether a sprite is a track sprite, which can be replaced by a non-track ground sprite and a rail overlay.
 * If the ground sprite is suitable, \a ground is replaced with the new non-track ground sprite, and \a overlay_offset
 * is set to the overlay to draw.
 * @param         ti             Positional info for the tile to decide snowyness etc. May be nullptr.
 * @param[in,out] ground         Groundsprite to draw.
 * @param[out]    overlay_offset Overlay to draw.
 * @return true if overlay can be drawn.
 */
bool SplitGroundSpriteForOverlay(const TileInfo *ti, SpriteID *ground, RailTrackOffset *overlay_offset)
{
	bool snow_desert;
	switch (*ground) {
		case SPR_RAIL_TRACK_X:
		case SPR_MONO_TRACK_X:
		case SPR_MGLV_TRACK_X:
			snow_desert = false;
			*overlay_offset = RTO_X;
			break;

		case SPR_RAIL_TRACK_Y:
		case SPR_MONO_TRACK_Y:
		case SPR_MGLV_TRACK_Y:
			snow_desert = false;
			*overlay_offset = RTO_Y;
			break;

		case SPR_RAIL_TRACK_X_SNOW:
		case SPR_MONO_TRACK_X_SNOW:
		case SPR_MGLV_TRACK_X_SNOW:
			snow_desert = true;
			*overlay_offset = RTO_X;
			break;

		case SPR_RAIL_TRACK_Y_SNOW:
		case SPR_MONO_TRACK_Y_SNOW:
		case SPR_MGLV_TRACK_Y_SNOW:
			snow_desert = true;
			*overlay_offset = RTO_Y;
			break;

		default:
			return false;
	}

	if (ti != nullptr) {
		/* Decide snow/desert from tile */
		switch (_settings_game.game_creation.landscape) {
			case LandscapeType::Arctic:
				snow_desert = (uint)ti->z > GetSnowLine() * TILE_HEIGHT;
				break;

			case LandscapeType::Tropic:
				snow_desert = GetTropicZone(ti->tile) == TROPICZONE_DESERT;
				break;

			default:
				break;
		}
	}

	*ground = snow_desert ? SPR_FLAT_SNOW_DESERT_TILE : SPR_FLAT_GRASS_TILE;
	return true;
}

static void DrawTile_Station(TileInfo *ti, DrawTileProcParams params)
{
	const NewGRFSpriteLayout *layout = nullptr;
	DrawTileSprites tmp_rail_layout;
	const DrawTileSprites *t = nullptr;
	int32_t total_offset;
	const RailTypeInfo *rti = nullptr;
	uint32_t relocation = 0;
	uint32_t ground_relocation = 0;
	BaseStation *st = nullptr;
	const StationSpec *statspec = nullptr;
	uint tile_layout = 0;

	if (HasStationRail(ti->tile)) {
		rti = GetRailTypeInfo(GetRailType(ti->tile));
		total_offset = rti->GetRailtypeSpriteOffset();

		if (IsCustomStationSpecIndex(ti->tile)) {
			/* look for customization */
			st = BaseStation::GetByTile(ti->tile);
			statspec = st->speclist[GetCustomStationSpecIndex(ti->tile)].spec;

			if (statspec != nullptr) {
				tile_layout = GetStationGfx(ti->tile);

				if (statspec->callback_mask.Test(StationCallbackMask::DrawTileLayout)) {
					uint16_t callback = GetStationCallback(CBID_STATION_DRAW_TILE_LAYOUT, 0, 0, statspec, st, ti->tile, INVALID_RAILTYPE);
					if (callback != CALLBACK_FAILED) tile_layout = (callback & ~1) + GetRailStationAxis(ti->tile);
				}

				/* Ensure the chosen tile layout is valid for this custom station */
				if (!statspec->renderdata.empty()) {
					layout = &statspec->renderdata[tile_layout < statspec->renderdata.size() ? tile_layout : (uint)GetRailStationAxis(ti->tile)];
					if (!layout->NeedsPreprocessing()) {
						t = layout;
						layout = nullptr;
					}
				}
			}
		}
	} else {
		total_offset = 0;
	}

	StationGfx gfx = GetStationGfx(ti->tile);
	if (IsAirport(ti->tile)) {
		gfx = GetAirportGfx(ti->tile);
		if (gfx >= NEW_AIRPORTTILE_OFFSET) {
			const AirportTileSpec *ats = AirportTileSpec::Get(gfx);
			if (ats->grf_prop.GetSpriteGroup() != nullptr && DrawNewAirportTile(ti, Station::GetByTile(ti->tile), ats)) {
				return;
			}
			/* No sprite group (or no valid one) found, meaning no graphics associated.
			 * Use the substitute one instead */
			assert(ats->grf_prop.subst_id != INVALID_AIRPORTTILE);
			gfx = ats->grf_prop.subst_id;
		}
		switch (gfx) {
			case APT_RADAR_GRASS_FENCE_SW:
				t = &_station_display_datas_airport_radar_grass_fence_sw[GetAnimationFrame(ti->tile)];
				break;
			case APT_GRASS_FENCE_NE_FLAG:
				t = &_station_display_datas_airport_flag_grass_fence_ne[GetAnimationFrame(ti->tile)];
				break;
			case APT_RADAR_FENCE_SW:
				t = &_station_display_datas_airport_radar_fence_sw[GetAnimationFrame(ti->tile)];
				break;
			case APT_RADAR_FENCE_NE:
				t = &_station_display_datas_airport_radar_fence_ne[GetAnimationFrame(ti->tile)];
				break;
			case APT_GRASS_FENCE_NE_FLAG_2:
				t = &_station_display_datas_airport_flag_grass_fence_ne_2[GetAnimationFrame(ti->tile)];
				break;
		}
	}

	Owner owner = GetTileOwner(ti->tile);

	PaletteID palette;
	if (Company::IsValidID(owner)) {
		palette = COMPANY_SPRITE_COLOUR(owner);
	} else {
		/* Some stations are not owner by a company, namely oil rigs */
		palette = PALETTE_TO_GREY;
	}

	if (layout == nullptr && (t == nullptr || t->seq == nullptr)) t = GetStationTileLayout(GetStationType(ti->tile), gfx);

	/* don't show foundation for docks */
	if (ti->tileh != SLOPE_FLAT && !IsDock(ti->tile)) {
		if (statspec != nullptr && statspec->flags.Test(StationSpecFlag::CustomFoundations)) {
			/* Station has custom foundations.
			 * Check whether the foundation continues beyond the tile's upper sides. */
			uint edge_info = 0;
			auto [slope, z] = GetFoundationPixelSlope(ti->tile);
			if (!HasFoundationNW(ti->tile, slope, z)) SetBit(edge_info, 0);
			if (!HasFoundationNE(ti->tile, slope, z)) SetBit(edge_info, 1);
			SpriteID image = GetCustomStationFoundationRelocation(statspec, st, ti->tile, tile_layout, edge_info);
			if (image == 0) goto draw_default_foundation;

			if (statspec->flags.Test(StationSpecFlag::ExtendedFoundations)) {
				/* Station provides extended foundations. */

				static const uint8_t foundation_parts[] = {
					0, 0, 0, 0, // Invalid,  Invalid,   Invalid,   SLOPE_SW
					0, 1, 2, 3, // Invalid,  SLOPE_EW,  SLOPE_SE,  SLOPE_WSE
					0, 4, 5, 6, // Invalid,  SLOPE_NW,  SLOPE_NS,  SLOPE_NWS
					7, 8, 9     // SLOPE_NE, SLOPE_ENW, SLOPE_SEN
				};

				AddSortableSpriteToDraw(image + foundation_parts[ti->tileh], PAL_NONE, ti->x, ti->y, 16, 16, 7, ti->z);
			} else {
				/* Draw simple foundations, built up from 8 possible foundation sprites. */

				/* Each set bit represents one of the eight composite sprites to be drawn.
				 * 'Invalid' entries will not drawn but are included for completeness. */
				static const uint8_t composite_foundation_parts[] = {
					/* Invalid  (00000000), Invalid   (11010001), Invalid   (11100100), SLOPE_SW  (11100000) */
					   0x00,                0xD1,                 0xE4,                 0xE0,
					/* Invalid  (11001010), SLOPE_EW  (11001001), SLOPE_SE  (11000100), SLOPE_WSE (11000000) */
					   0xCA,                0xC9,                 0xC4,                 0xC0,
					/* Invalid  (11010010), SLOPE_NW  (10010001), SLOPE_NS  (11100100), SLOPE_NWS (10100000) */
					   0xD2,                0x91,                 0xE4,                 0xA0,
					/* SLOPE_NE (01001010), SLOPE_ENW (00001001), SLOPE_SEN (01000100) */
					   0x4A,                0x09,                 0x44
				};

				uint8_t parts = composite_foundation_parts[ti->tileh];

				/* If foundations continue beyond the tile's upper sides then
				 * mask out the last two pieces. */
				if (HasBit(edge_info, 0)) ClrBit(parts, 6);
				if (HasBit(edge_info, 1)) ClrBit(parts, 7);

				if (parts == 0) {
					/* We always have to draw at least one sprite to make sure there is a boundingbox and a sprite with the
					 * correct offset for the childsprites.
					 * So, draw the (completely empty) sprite of the default foundations. */
					goto draw_default_foundation;
				}

				StartSpriteCombine();
				for (int i = 0; i < 8; i++) {
					if (HasBit(parts, i)) {
						AddSortableSpriteToDraw(image + i, PAL_NONE, ti->x, ti->y, 16, 16, 7, ti->z);
					}
				}
				EndSpriteCombine();
			}

			OffsetGroundSprite(0, -8);
			ti->z += ApplyPixelFoundationToSlope(FOUNDATION_LEVELED, ti->tileh);
		} else {
draw_default_foundation:
			DrawFoundation(ti, FOUNDATION_LEVELED);
		}
	}

	bool draw_ground = false;

	if (IsBuoy(ti->tile)) {
		DrawWaterClassGround(ti);
		SpriteID sprite = GetCanalSprite(CF_BUOY, ti->tile);
		if (sprite != 0) total_offset = sprite - SPR_IMG_BUOY;
	} else if (IsDock(ti->tile) || (IsOilRig(ti->tile) && IsTileOnWater(ti->tile))) {
		if (ti->tileh == SLOPE_FLAT) {
			DrawWaterClassGround(ti);
		} else {
			assert_tile(IsDock(ti->tile), ti->tile);
			TileIndex water_tile = ti->tile + TileOffsByDiagDir(GetDockDirection(ti->tile));
			WaterClass wc = HasTileWaterClass(water_tile) ? GetWaterClass(water_tile) : WATER_CLASS_INVALID;
			if (wc == WATER_CLASS_SEA) {
				DrawShoreTile(ti->tileh);
			} else {
				DrawClearLandTile(ti, 3);
			}
		}
	} else if (IsRoadWaypointTile(ti->tile)) {
		RoadBits bits = AxisToRoadBits(GetDriveThroughStopAxis(ti->tile));
		extern void DrawRoadBits(TileInfo *ti, RoadBits road, RoadBits tram, Roadside roadside, bool snow_or_desert, bool draw_catenary);
		DrawRoadBits(ti, GetRoadTypeRoad(ti->tile) != INVALID_ROADTYPE ? bits : ROAD_NONE,
				GetRoadTypeTram(ti->tile) != INVALID_ROADTYPE ? bits : ROAD_NONE,
				GetRoadWaypointRoadside(ti->tile), IsRoadWaypointOnSnowOrDesert(ti->tile), false);
	} else {
		if (layout != nullptr) {
			/* Sprite layout which needs preprocessing */
			bool separate_ground = statspec->flags.Test(StationSpecFlag::SeparateGround);
			uint32_t var10_values = layout->PrepareLayout(total_offset, rti->fallback_railtype, 0, 0, separate_ground);
			for (uint8_t var10 : SetBitIterator(var10_values)) {
				uint32_t var10_relocation = GetCustomStationRelocation(statspec, st, ti->tile, INVALID_RAILTYPE, var10);
				layout->ProcessRegisters(var10, var10_relocation, separate_ground);
			}
			tmp_rail_layout.seq = layout->GetLayout(&tmp_rail_layout.ground);
			t = &tmp_rail_layout;
			total_offset = 0;
		} else if (statspec != nullptr) {
			/* Simple sprite layout */
			ground_relocation = relocation = GetCustomStationRelocation(statspec, st, ti->tile, INVALID_RAILTYPE, 0);
			if (statspec->flags.Test(StationSpecFlag::SeparateGround)) {
				ground_relocation = GetCustomStationRelocation(statspec, st, ti->tile, INVALID_RAILTYPE, 1);
			}
			ground_relocation += rti->fallback_railtype;
		}

		draw_ground = true;
	}

	if (draw_ground && !IsAnyRoadStop(ti->tile)) {
		SpriteID image = t->ground.sprite;
		PaletteID pal  = t->ground.pal;
		RailTrackOffset overlay_offset;
		if (rti != nullptr && rti->UsesOverlay() && SplitGroundSpriteForOverlay(ti, &image, &overlay_offset)) {
			SpriteID ground = GetCustomRailSprite(rti, ti->tile, RTSG_GROUND);
			DrawGroundSprite(image, PAL_NONE);
			DrawGroundSprite(ground + overlay_offset, PAL_NONE);

			if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && HasStationReservation(ti->tile)) {
				SpriteID overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY);
				DrawGroundSprite(overlay + overlay_offset, PALETTE_CRASH);
			}
		} else {
			image += HasBit(image, SPRITE_MODIFIER_CUSTOM_SPRITE) ? ground_relocation : total_offset;
			if (HasBit(pal, SPRITE_MODIFIER_CUSTOM_SPRITE)) pal += ground_relocation;
			DrawGroundSprite(image, GroundSpritePaletteTransform(image, pal, palette));

			/* PBS debugging, draw reserved tracks darker */
			if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && HasStationRail(ti->tile) && HasStationReservation(ti->tile)) {
				DrawGroundSprite(GetRailStationAxis(ti->tile) == AXIS_X ? rti->base_sprites.single_x : rti->base_sprites.single_y, PALETTE_CRASH);
			}
		}
	}

	if (HasStationRail(ti->tile) && HasRailCatenaryDrawn(GetRailType(ti->tile))) DrawRailCatenary(ti);

	if (IsAnyRoadStop(ti->tile)) {
		RoadType road_rt = GetRoadTypeRoad(ti->tile);
		RoadType tram_rt = GetRoadTypeTram(ti->tile);
		const RoadTypeInfo *road_rti = road_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(road_rt);
		const RoadTypeInfo *tram_rti = tram_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(tram_rt);

		StationGfx view = GetStationGfx(ti->tile);
		StationType type = GetStationType(ti->tile);

		const RoadStopSpec *stopspec = GetRoadStopSpec(ti->tile);
		RoadStopDrawModes stop_draw_mode{};
		if (stopspec != nullptr) {
			stop_draw_mode = stopspec->draw_mode;
			st = BaseStation::GetByTile(ti->tile);
			RoadStopResolverObject object(stopspec, st, ti->tile, INVALID_ROADTYPE, type, view);
			const SpriteGroup *group = object.Resolve();
			if (group != nullptr && group->type == SGT_TILELAYOUT) {
				const DrawTileSprites *dts = ((const TileLayoutSpriteGroup *)group)->ProcessRegisters(nullptr);
				if (stopspec->flags.Test(RoadStopSpecFlag::DrawModeRegister)) {
					stop_draw_mode = (RoadStopDrawMode)GetRegister(0x100);
				}
				t = dts;
				if (type == StationType::RoadWaypoint && stop_draw_mode.Test(RoadStopDrawMode::WaypGround)) {
					draw_ground = true;
				}
			}
		}

		/* Draw ground sprite */
		if (draw_ground) {
			SpriteID image = t->ground.sprite;
			PaletteID pal  = t->ground.pal;
			image += HasBit(image, SPRITE_MODIFIER_CUSTOM_SPRITE) ? ground_relocation : total_offset;
			if (GB(image, 0, SPRITE_WIDTH) != 0) {
				if (HasBit(pal, SPRITE_MODIFIER_CUSTOM_SPRITE)) pal += ground_relocation;
				DrawGroundSprite(image, GroundSpritePaletteTransform(image, pal, palette));
			}
		}

		if (IsDriveThroughStopTile(ti->tile)) {
			if (type != StationType::RoadWaypoint && (stopspec == nullptr || stop_draw_mode.Test(RoadStopDrawMode::Overlay))) {
				uint sprite_offset = GetDriveThroughStopAxis(ti->tile) == AXIS_X ? 1 : 0;
				DrawRoadOverlays(ti, PAL_NONE, road_rti, tram_rti, sprite_offset, sprite_offset);
			}

			DisallowedRoadDirections drd = GetDriveThroughStopDisallowedRoadDirections(ti->tile);
			if (drd != DRD_NONE && (stopspec == nullptr || !stopspec->flags.Test(RoadStopSpecFlag::NoOneWayOverlay)) && road_rt != INVALID_ROADTYPE) {
				SpriteID oneway = GetCustomRoadSprite(road_rti, ti->tile, ROTSG_ONEWAY);
				if (oneway == 0) oneway = SPR_ONEWAY_BASE;
				DrawGroundSpriteAt(oneway + drd - 1 + ((GetDriveThroughStopAxis(ti->tile) == AXIS_X) ? 0 : 3), PAL_NONE, 8, 8, 0);
			}
		} else {
			/* Non-drivethrough road stops are only valid for roads. */
			assert_tile(road_rt != INVALID_ROADTYPE && tram_rt == INVALID_ROADTYPE, ti->tile);

			if ((stopspec == nullptr || stop_draw_mode.Test(RoadStopDrawMode::Road)) && road_rti->UsesOverlay()) {
				SpriteID ground = GetCustomRoadSprite(road_rti, ti->tile, ROTSG_ROADSTOP);
				DrawGroundSprite(ground + view, PAL_NONE);
			}
		}

		if (stopspec == nullptr || !stopspec->flags.Test(RoadStopSpecFlag::NoCatenary)) {
			/* Draw road, tram catenary */
			DrawRoadCatenary(ti);
		}
	}

	if (IsRailWaypoint(ti->tile)) {
		/* Don't offset the waypoint graphics; they're always the same. */
		total_offset = 0;
	}

	DrawRailTileSeq(ti, t, TO_BUILDINGS, total_offset, relocation, palette);
	DrawBridgeMiddle(ti);
}

void StationPickerDrawSprite(int x, int y, StationType st, RailType railtype, RoadType roadtype, int image)
{
	int32_t total_offset = 0;
	PaletteID pal = COMPANY_SPRITE_COLOUR(_local_company);
	const DrawTileSprites *t = GetStationTileLayout(st, image);
	const RailTypeInfo *railtype_info = nullptr;

	if (railtype != INVALID_RAILTYPE) {
		railtype_info = GetRailTypeInfo(railtype);
		total_offset = railtype_info->GetRailtypeSpriteOffset();
	}

	SpriteID img = t->ground.sprite;
	RailTrackOffset overlay_offset;
	if (railtype_info != nullptr && railtype_info->UsesOverlay() && SplitGroundSpriteForOverlay(nullptr, &img, &overlay_offset)) {
		SpriteID ground = GetCustomRailSprite(railtype_info, INVALID_TILE, RTSG_GROUND);
		DrawSprite(img, PAL_NONE, x, y);
		DrawSprite(ground + overlay_offset, PAL_NONE, x, y);
	} else {
		DrawSprite(img + total_offset, HasBit(img, PALETTE_MODIFIER_COLOUR) ? pal : PAL_NONE, x, y);
	}

	if (roadtype != INVALID_ROADTYPE) {
		const RoadTypeInfo *roadtype_info = GetRoadTypeInfo(roadtype);
		if (image >= 4) {
			/* Drive-through stop */
			uint sprite_offset = 5 - image;

			/* Road underlay takes precedence over tram */
			if (roadtype_info->UsesOverlay()) {
				SpriteID ground = GetCustomRoadSprite(roadtype_info, INVALID_TILE, ROTSG_GROUND);
				DrawSprite(ground + sprite_offset, PAL_NONE, x, y);

				SpriteID overlay = GetCustomRoadSprite(roadtype_info, INVALID_TILE, ROTSG_OVERLAY);
				if (overlay) DrawSprite(overlay + sprite_offset, PAL_NONE, x, y);
			} else if (RoadTypeIsTram(roadtype)) {
				DrawSprite(SPR_TRAMWAY_TRAM + sprite_offset, PAL_NONE, x, y);
			}
		} else {
			/* Bay stop */
			if (RoadTypeIsRoad(roadtype) && roadtype_info->UsesOverlay()) {
				SpriteID ground = GetCustomRoadSprite(roadtype_info, INVALID_TILE, ROTSG_ROADSTOP);
				DrawSprite(ground + image, PAL_NONE, x, y);
			}
		}
	}

	/* Default waypoint has no railtype specific sprites */
	DrawRailTileSeqInGUI(x, y, t, (st == StationType::RailWaypoint || st == StationType::RoadWaypoint) ? 0 : total_offset, 0, pal);
}

static int GetSlopePixelZ_Station(TileIndex tile, uint, uint, bool)
{
	return GetTileMaxPixelZ(tile);
}

static Foundation GetFoundation_Station(TileIndex, Slope tileh)
{
	return FlatteningFoundation(tileh);
}

static void FillTileDescRoadStop(TileIndex tile, TileDesc *td)
{
	RoadType road_rt = GetRoadTypeRoad(tile);
	RoadType tram_rt = GetRoadTypeTram(tile);
	Owner road_owner = INVALID_OWNER;
	Owner tram_owner = INVALID_OWNER;
	if (road_rt != INVALID_ROADTYPE) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(road_rt);
		td->roadtype = rti->strings.name;
		td->road_speed = rti->max_speed / 2;
		road_owner = GetRoadOwner(tile, RTT_ROAD);
	}

	if (tram_rt != INVALID_ROADTYPE) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(tram_rt);
		td->tramtype = rti->strings.name;
		td->tram_speed = rti->max_speed / 2;
		tram_owner = GetRoadOwner(tile, RTT_TRAM);
	}

	if (IsDriveThroughStopTile(tile)) {
		/* Is there a mix of owners? */
		if ((tram_owner != INVALID_OWNER && tram_owner != td->owner[0]) ||
				(road_owner != INVALID_OWNER && road_owner != td->owner[0])) {
			uint i = 1;
			if (road_owner != INVALID_OWNER) {
				td->owner_type[i] = STR_LAND_AREA_INFORMATION_ROAD_OWNER;
				td->owner[i] = road_owner;
				i++;
			}
			if (tram_owner != INVALID_OWNER) {
				td->owner_type[i] = STR_LAND_AREA_INFORMATION_TRAM_OWNER;
				td->owner[i] = tram_owner;
			}
		}
	}
}

void FillTileDescRailStation(TileIndex tile, TileDesc *td)
{
	const StationSpec *spec = GetStationSpec(tile);

	if (spec != nullptr) {
		td->station_class = StationClass::Get(spec->class_index)->name;
		td->station_name  = spec->name;

		if (spec->grf_prop.HasGrfFile()) {
			const GRFConfig *gc = GetGRFConfig(spec->grf_prop.grfid);
			td->grf = gc->GetName();
		}
	}

	const RailTypeInfo *rti = GetRailTypeInfo(GetRailType(tile));
	td->rail_speed = rti->max_speed;
	td->railtype = rti->strings.name;
}

void FillTileDescAirport(TileIndex tile, TileDesc *td)
{
	const AirportSpec *as = Station::GetByTile(tile)->airport.GetSpec();
	td->airport_class = AirportClass::Get(as->class_index)->name;
	td->airport_name = as->name;

	const AirportTileSpec *ats = AirportTileSpec::GetByTile(tile);
	td->airport_tile_name = ats->name;

	if (as->grf_prop.HasGrfFile()) {
		const GRFConfig *gc = GetGRFConfig(as->grf_prop.grfid);
		td->grf = gc->GetName();
	} else if (ats->grf_prop.HasGrfFile()) {
		const GRFConfig *gc = GetGRFConfig(ats->grf_prop.grfid);
		td->grf = gc->GetName();
	}
}

static void GetTileDesc_Station(TileIndex tile, TileDesc *td)
{
	td->owner[0] = GetTileOwner(tile);
	td->build_date = BaseStation::GetByTile(tile)->build_date;

	if (IsAnyRoadStopTile(tile)) FillTileDescRoadStop(tile, td);
	if (HasStationRail(tile)) FillTileDescRailStation(tile, td);
	if (IsAirport(tile)) FillTileDescAirport(tile, td);

	StringID str;
	switch (GetStationType(tile)) {
		default: NOT_REACHED();
		case StationType::Rail:     str = STR_LAI_STATION_DESCRIPTION_RAILROAD_STATION; break;
		case StationType::Airport:
			str = (IsHangar(tile) ? STR_LAI_STATION_DESCRIPTION_AIRCRAFT_HANGAR : STR_LAI_STATION_DESCRIPTION_AIRPORT);
			break;
		case StationType::Truck:    str = STR_LAI_STATION_DESCRIPTION_TRUCK_LOADING_AREA; break;
		case StationType::Bus:      str = STR_LAI_STATION_DESCRIPTION_BUS_STATION; break;
		case StationType::Oilrig: {
			const Industry *i = Station::GetByTile(tile)->industry;
			const IndustrySpec *is = GetIndustrySpec(i->type);
			td->owner[0] = i->owner;
			str = is->name;
			if (is->grf_prop.HasGrfFile()) td->grf = GetGRFConfig(is->grf_prop.grfid)->GetName();
			break;
		}
		case StationType::Dock:         str = STR_LAI_STATION_DESCRIPTION_SHIP_DOCK; break;
		case StationType::Buoy:         str = STR_LAI_STATION_DESCRIPTION_BUOY; break;
		case StationType::RailWaypoint: str = STR_LAI_STATION_DESCRIPTION_WAYPOINT; break;
		case StationType::RoadWaypoint: str = STR_LAI_STATION_DESCRIPTION_WAYPOINT; break;
	}
	td->str = str;
}


static TrackStatus GetTileTrackStatus_Station(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	TrackdirBits trackdirbits = TRACKDIR_BIT_NONE;

	switch (mode) {
		case TRANSPORT_RAIL:
			if (HasStationRail(tile) && !IsStationTileBlocked(tile)) {
				trackdirbits = TrackToTrackdirBits(GetRailStationTrack(tile));
			}
			break;

		case TRANSPORT_WATER:
			/* buoy is coded as a station, it is always on open water */
			if (IsBuoy(tile)) {
				TrackBits trackbits = TRACK_BIT_ALL;
				/* remove tracks that connect NE map edge */
				if (TileX(tile) == 0) trackbits &= ~(TRACK_BIT_X | TRACK_BIT_UPPER | TRACK_BIT_RIGHT);
				/* remove tracks that connect NW map edge */
				if (TileY(tile) == 0) trackbits &= ~(TRACK_BIT_Y | TRACK_BIT_LEFT | TRACK_BIT_UPPER);
				trackdirbits = TrackBitsToTrackdirBits(trackbits);
			}
			break;

		case TRANSPORT_ROAD:
			if (IsAnyRoadStop(tile)) {
				RoadTramType rtt = (RoadTramType)GB(sub_mode, 0, 8);
				if (!HasTileRoadType(tile, rtt)) break;

				if (IsBayRoadStopTile(tile)) {
					DiagDirection dir = GetBayRoadStopDir(tile);
					if (side != INVALID_DIAGDIR && dir != side) break;
					TrackBits trackbits = DiagDirToDiagTrackBits(dir);
					trackdirbits = TrackBitsToTrackdirBits(trackbits);
				} else {
					Axis axis = GetDriveThroughStopAxis(tile);
					if (side != INVALID_DIAGDIR && axis != DiagDirToAxis(side)) break;
					TrackBits trackbits = AxisToTrackBits(axis);
					const uint drd_to_multiplier[DRD_END] = { 0x101, 0x100, 0x1, 0x0 };
					trackdirbits = (TrackdirBits)(trackbits * drd_to_multiplier[GetDriveThroughStopDisallowedRoadDirections(tile)]);
				}
			}
			break;

		default:
			break;
	}

	return CombineTrackStatus(trackdirbits, TRACKDIR_BIT_NONE);
}


static void TileLoop_Station(TileIndex tile)
{
	/* FIXME -- GetTileTrackStatus_Station -> animated stationtiles
	 * hardcoded.....not good */
	switch (GetStationType(tile)) {
		case StationType::Airport:
			AirportTileAnimationTrigger(Station::GetByTile(tile), tile, AAT_TILELOOP);
			break;

		case StationType::Dock:
			if (!IsTileFlat(tile)) break; // only handle water part
			[[fallthrough]];

		case StationType::Oilrig: //(station part)
		case StationType::Buoy:
			TileLoop_Water(tile);
			break;

		case StationType::RoadWaypoint: {
			switch (_settings_game.game_creation.landscape) {
				case LandscapeType::Arctic:
					if (IsRoadWaypointOnSnowOrDesert(tile) != (GetTileZ(tile) > GetSnowLine())) {
						ToggleRoadWaypointOnSnowOrDesert(tile);
						MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
					}
					break;

				case LandscapeType::Tropic:
					if (GetTropicZone(tile) == TROPICZONE_DESERT && !IsRoadWaypointOnSnowOrDesert(tile)) {
						ToggleRoadWaypointOnSnowOrDesert(tile);
						MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
					}
					break;

				default: break;
			}

			HouseZonesBits grp = HZB_TOWN_EDGE;
			const Town *t = ClosestTownFromTile(tile, UINT_MAX);
			if (t != nullptr) {
				grp = GetTownRadiusGroup(t, tile);
			}

			/* Adjust road ground type depending on 'grp' (grp is the distance to the center) */
			Roadside new_rs = grp > HZB_TOWN_EDGE ? ROADSIDE_PAVED : ROADSIDE_GRASS;
			Roadside cur_rs = GetRoadWaypointRoadside(tile);

			if (new_rs != cur_rs) {
				SetRoadWaypointRoadside(tile, cur_rs == ROADSIDE_BARREN ? new_rs : ROADSIDE_BARREN);
				MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
			}
			break;
		}

		default: break;
	}
}


void AnimateTile_Station(TileIndex tile)
{
	if (HasStationRail(tile)) {
		AnimateStationTile(tile);
		return;
	}

	if (IsAirport(tile)) {
		AnimateAirportTile(tile);
		return;
	}

	if (IsAnyRoadStopTile(tile)) {
		AnimateRoadStopTile(tile);
		return;
	}
}

uint8_t GetAnimatedTileSpeed_Station(TileIndex tile)
{
	if (HasStationRail(tile)) {
		return GetStationTileAnimationSpeed(tile);
	}

	if (IsAirport(tile)) {
		return GetAirportTileAnimationSpeed(tile);
	}

	if (IsAnyRoadStopTile(tile)) {
		return GetRoadStopTileAnimationSpeed(tile);
	}
	return 0;
}


static bool ClickTile_Station(TileIndex tile)
{
	const BaseStation *bst = BaseStation::GetByTile(tile);

	if (bst->facilities & FACIL_WAYPOINT) {
		ShowWaypointWindow(Waypoint::From(bst));
	} else if (IsHangar(tile)) {
		const Station *st = Station::From(bst);
		ShowDepotWindow(st->airport.GetHangarTile(st->airport.GetHangarNum(tile)), VEH_AIRCRAFT);
	} else {
		ShowStationViewWindow(bst->index);
	}
	return true;
}

static VehicleEnterTileStatus VehicleEnter_Station(Vehicle *v, TileIndex tile, int x, int y)
{
	if (v->type == VEH_TRAIN) {
		StationID station_id = GetStationIndex(tile);
		if (v->current_order.IsType(OT_GOTO_WAYPOINT) && v->current_order.GetDestination() == station_id && v->current_order.GetWaypointFlags() & OWF_REVERSE) {
			Train *t = Train::From(v);
			// reverse at waypoint
			if (t->reverse_distance == 0) {
				t->reverse_distance = t->gcache.cached_total_length;
				if (t->current_order.IsWaitTimetabled()) {
					t->DeleteUnreachedImplicitOrders();
					UpdateVehicleTimetable(t, true);
					t->last_station_visited = station_id;
					SetWindowDirty(WC_VEHICLE_VIEW, t->index);
					t->current_order.MakeWaiting();
					t->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
					return VETSB_CONTINUE;
				}
			}
		}
		if (HasBit(Train::From(v)->flags, VRF_BEYOND_PLATFORM_END)) return VETSB_CONTINUE;
		Train *front = Train::From(v)->First();
		if (!front->IsFrontEngine()) return VETSB_CONTINUE;
		if (!(v == front || HasBit(Train::From(v)->Previous()->flags, VRF_BEYOND_PLATFORM_END))) return VETSB_CONTINUE;
		if (!HasStationTileRail(tile)) return VETSB_CONTINUE;
		if (!front->current_order.ShouldStopAtStation(front, station_id, IsRailWaypoint(tile))) return VETSB_CONTINUE;

		int station_ahead;
		int station_length;
		int stop = GetTrainStopLocation(station_id, tile, Train::From(v), true, &station_ahead, &station_length);

		/* Stop whenever that amount of station ahead + the distance from the
		 * begin of the platform to the stop location is longer than the length
		 * of the platform. Station ahead 'includes' the current tile where the
		 * vehicle is on, so we need to subtract that. */
		if (stop + station_ahead - (int)TILE_SIZE >= station_length) return VETSB_CONTINUE;

		DiagDirection dir = DirToDiagDir(v->direction);

		x &= 0xF;
		y &= 0xF;

		if (DiagDirToAxis(dir) != AXIS_X) Swap(x, y);
		if (y == TILE_SIZE / 2) {
			if (dir != DIAGDIR_SE && dir != DIAGDIR_SW) x = TILE_SIZE - 1 - x;
			stop &= TILE_SIZE - 1;

			if (x == stop) {
				if (front->UsingRealisticBraking() && front->cur_speed > 15 && !(front->lookahead != nullptr && front->lookahead->flags.Test(TrainReservationLookAheadFlag::ApplyAdvisory))) {
					/* Travelling too fast, do not stop and report overshoot to player */
					if (front->owner == _local_company) {
						SetDParam(0, front->index);
						SetDParam(1, IsRailWaypointTile(tile) ? STR_WAYPOINT_NAME : STR_STATION_NAME);
						SetDParam(2, station_id);
						AddNewsItem(STR_NEWS_TRAIN_OVERSHOT_STATION, NewsType::Advice, NewsStyle::Small, {NewsFlag::InColour, NewsFlag::VehicleParam0},
								NewsReferenceType::Vehicle, v->index,
								NewsReferenceType::Station, station_id);
					}
					for (Train *u = front; u != nullptr; u = u->Next()) {
						ClrBit(u->flags, VRF_BEYOND_PLATFORM_END);
					}
					return VETSB_CONTINUE;
				}
				return VETSB_ENTERED_STATION | (VehicleEnterTileStatus)(station_id << VETS_STATION_ID_OFFSET); // enter station
			} else if (x < stop) {
				if (front->UsingRealisticBraking() && front->cur_speed > 30) {
					/* Travelling too fast, take no action */
					return VETSB_CONTINUE;
				}
				front->vehstatus |= VS_TRAIN_SLOWING;
				uint16_t spd = std::max(0, (stop - x) * 20 - 15);
				if (spd < front->cur_speed) front->cur_speed = spd;
			}
		}
	} else if (v->type == VEH_ROAD) {
		RoadVehicle *rv = RoadVehicle::From(v);
		if (rv->state < RVSB_IN_ROAD_STOP && !IsReversingRoadTrackdir((Trackdir)rv->state) && rv->frame == 0) {
			if (IsStationRoadStop(tile) && rv->IsFrontEngine()) {
				/* Attempt to allocate a parking bay in a road stop */
				return RoadStop::GetByTile(tile, GetRoadStopType(tile))->Enter(rv) ? VETSB_CONTINUE : VETSB_CANNOT_ENTER;
			}
		}
	}

	return VETSB_CONTINUE;
}

/**
 * Run the watched cargo callback for all houses in the catchment area.
 * @param st Station.
 */
void TriggerWatchedCargoCallbacks(Station *st)
{
	/* Collect cargoes accepted since the last big tick. */
	CargoTypes cargoes = 0;
	for (CargoType cargo_type = 0; cargo_type < NUM_CARGO; cargo_type++) {
		if (HasBit(st->goods[cargo_type].status, GoodsEntry::GES_ACCEPTED_BIGTICK)) SetBit(cargoes, cargo_type);
	}

	/* Anything to do? */
	if (cargoes == 0) return;

	/* Loop over all houses in the catchment. */
	BitmapTileIterator it(st->catchment_tiles);
	for (TileIndex tile = it; tile != INVALID_TILE; tile = ++it) {
		if (IsTileType(tile, MP_HOUSE)) {
			WatchedCargoCallback(tile, cargoes);
		}
	}
}

/**
 * This function is called for each station once every 250 ticks.
 * Not all stations will get the tick at the same time.
 * @param st the station receiving the tick.
 * @return true if the station is still valid (wasn't deleted)
 */
static bool StationHandleBigTick(BaseStation *st)
{
	if (!st->IsInUse()) {
		if (++st->delete_ctr >= 8) delete st;
		return false;
	}

	if (Station::IsExpected(st)) {
		TriggerWatchedCargoCallbacks(Station::From(st));

		for (GoodsEntry &ge : Station::From(st)->goods) {
			ClrBit(ge.status, GoodsEntry::GES_ACCEPTED_BIGTICK);
		}
	}


	if ((st->facilities & FACIL_WAYPOINT) == 0) UpdateStationAcceptance(Station::From(st), true);

	return true;
}

static inline void byte_inc_sat(uint8_t *p)
{
	uint8_t b = *p + 1;
	if (b != 0) *p = b;
}

/**
 * Truncate the cargo by a specific amount.
 * @param cs The type of cargo to perform the truncation for.
 * @param ge The goods entry, of the station, to truncate.
 * @param amount The amount to truncate the cargo by.
 */
static void TruncateCargo(const CargoSpec *cs, GoodsEntry *ge, uint amount = UINT_MAX)
{
	/* If truncating also punish the source stations' ratings to
	 * decrease the flow of incoming cargo. */

	if (ge->data == nullptr) return;

	StationCargoAmountMap waiting_per_source;
	ge->data->cargo.Truncate(amount, &waiting_per_source);
	for (StationCargoAmountMap::iterator i(waiting_per_source.begin()); i != waiting_per_source.end(); ++i) {
		Station *source_station = Station::GetIfValid(i->first);
		if (source_station == nullptr) continue;

		GoodsEntry &source_ge = source_station->goods[cs->Index()];
		if (i->second > source_ge.max_waiting_cargo) {
			source_ge.max_waiting_cargo += (i->second - source_ge.max_waiting_cargo) / 4;
		}
	}
}

bool GetNewGrfRating(const Station *st, const CargoSpec *cs, const GoodsEntry *ge, int *new_grf_rating)
{
	*new_grf_rating = 0;
	bool is_using_newgrf_rating = false;

	/* Perform custom station rating. If it succeeds the speed, days in transit and
	 * waiting cargo ratings must not be executed. */

	/* NewGRFs expect last speed to be 0xFF when no vehicle has arrived yet. */
	uint last_speed = ge->HasVehicleEverTriedLoading() && ge->IsSupplyAllowed() ? ge->last_speed : 0xFF;

	uint32_t var18 = std::min<uint>(ge->time_since_pickup, 0xFFu)
		| (std::min<uint>(ge->max_waiting_cargo, 0xFFFFu) << 8)
		| (std::min<uint>(last_speed, 0xFFu) << 24);
	/* Convert to the 'old' vehicle types */
	uint32_t var10 = (ge->last_vehicle_type == VEH_INVALID) ? 0x0 : (ge->last_vehicle_type + 0x10);
	uint16_t callback = GetCargoCallback(CBID_CARGO_STATION_RATING_CALC, var10, var18, cs);
	if (callback != CALLBACK_FAILED) {
		is_using_newgrf_rating = true;
		*new_grf_rating = GB(callback, 0, 14);

		/* Simulate a 15 bit signed value */
		if (HasBit(callback, 14)) *new_grf_rating -= 0x4000;
	}

	return is_using_newgrf_rating;
}

int GetSpeedRating(const GoodsEntry *ge)
{
	const int b = ge->last_speed - 85;

	return (b >= 0) ? (b >> 2) : 0;
}

int GetWaitTimeRating(const CargoSpec *cs, const GoodsEntry *ge)
{
	int rating = 0;

	uint wait_time = ge->time_since_pickup;

	if (_settings_game.station.cargo_class_rating_wait_time) {
		if (cs->classes.Test(CargoClass::Passengers)) {
			wait_time *= 3;
		} else if (cs->classes.Test(CargoClass::Refrigerated)) {
			wait_time *= 2;
		} else if (cs->classes.Any({CargoClass::Mail, CargoClass::Armoured, CargoClass::Express})) {
			wait_time += (wait_time >> 1);
		} else if (cs->classes.Any({CargoClass::Bulk, CargoClass::Liquid})) {
			wait_time >>= 2;
		}
	}

	if (ge->last_vehicle_type == VEH_SHIP) wait_time >>= 2;
	if (wait_time <= 21) rating += 25;
	if (wait_time <= 12) rating += 25;
	if (wait_time <= 6) rating += 45;
	if (wait_time <= 3) rating += 35;

	return rating;
}

int GetWaitingCargoRating(const Station *st, const GoodsEntry *ge)
{
	int rating = -90;

	uint normalised_max_waiting_cargo = ge->max_waiting_cargo;

	if (_settings_game.station.station_size_rating_cargo_amount) {
		normalised_max_waiting_cargo *= 8;
		if (st->station_tiles > 1) normalised_max_waiting_cargo /= st->station_tiles;
	}

	if (normalised_max_waiting_cargo <= 1500) rating += 55;
	if (normalised_max_waiting_cargo <= 1000) rating += 35;
	if (normalised_max_waiting_cargo <= 600) rating += 10;
	if (normalised_max_waiting_cargo <= 300) rating += 20;
	if (normalised_max_waiting_cargo <= 100) rating += 10;

	return rating;
}

int GetStatueRating(const Station *st)
{
	return Company::IsValidID(st->owner) && st->town->statues.Test(st->owner) ? 26 : 0;
}

int GetVehicleAgeRating(const GoodsEntry *ge)
{
	int rating = 0;

	const uint8_t age = ge->last_age;

	if (age < 30) rating += 10;
	if (age < 20) rating += 10;
	if (age < 10) rating += 13;

	return rating;
}

int GetTargetRating(const Station *st, const CargoSpec *cs, const GoodsEntry *ge)
{
	bool skip = false;
	int rating = 0;

	if (_cheats.station_rating.value) {
		rating = 255;
		skip = true;
	} else if (cs->callback_mask.Test(CargoCallbackMask::StationRatingCalc)) {
		int new_grf_rating;

		if (GetNewGrfRating(st, cs, ge, &new_grf_rating)) {
			skip = true;
			rating = new_grf_rating;
		}
	}

	if (!skip) {
		rating += GetSpeedRating(ge);
		rating += GetWaitTimeRating(cs, ge);
		rating += GetWaitingCargoRating(st, ge);
	}

	rating += GetStatueRating(st);
	rating += GetVehicleAgeRating(ge);

	return ClampTo<uint8_t>(rating);
}

static void UpdateStationRating(Station *st)
{
	bool waiting_changed = false;

	byte_inc_sat(&st->time_since_load);
	byte_inc_sat(&st->time_since_unload);

	for (const CargoSpec *cs : CargoSpec::Iterate()) {
		GoodsEntry *ge = &st->goods[cs->Index()];

		/* Slowly increase the rating back to its original level in the case we
		 *  didn't deliver cargo yet to this station. This happens when a bribe
		 *  failed while you didn't moved that cargo yet to a station. */
		if (!ge->HasRating() && ge->rating < INITIAL_STATION_RATING) {
			ge->rating++;
		}

		/* Only change the rating if we are moving this cargo */
		if (ge->HasRating()) {
			byte_inc_sat(&ge->time_since_pickup);

			if (ge->time_since_pickup == 255 && _settings_game.order.selectgoods) {
				ClrBit(ge->status, GoodsEntry::GES_RATING);
				ge->last_speed = 0;
				TruncateCargo(cs, ge);
				waiting_changed = true;
				continue;
			}

			{
				int rating = GetTargetRating(st, cs, ge);

				uint waiting = ge->CargoAvailableCount();

				/* num_dests is at least 1 if there is any cargo as
				 * INVALID_STATION is also a destination.
				 */
				const uint num_dests = ge->data != nullptr ? (uint)ge->data->cargo.Packets()->MapSize() : 0;

				/* Average amount of cargo per next hop, but prefer solitary stations
				 * with only one or two next hops. They are allowed to have more
				 * cargo waiting per next hop.
				 * With manual cargo distribution waiting_avg = waiting / 2 as then
				 * INVALID_STATION is the only destination.
				 */
				const uint waiting_avg = waiting / (num_dests + 1);

				const int old_rating = ge->rating; // old rating

				/* only modify rating in steps of -2, -1, 0, 1 or 2 */
				ge->rating = rating = old_rating + Clamp(rating - old_rating, -2, 2);

				/* if rating is <= 64 and more than 100 items waiting on average per destination,
				 * remove some random amount of goods from the station */
				if (rating <= 64 && waiting_avg >= 100) {
					int dec = Random() & 0x1F;
					if (waiting_avg < 200) dec &= 7;
					waiting -= (dec + 1) * num_dests;
					waiting_changed = true;
				}

				/* if rating is <= 127 and there are any items waiting, maybe remove some goods. */
				if (rating <= 127 && waiting != 0) {
					uint32_t r = Random();
					if (rating <= (int)GB(r, 0, 7)) {
						/* Need to have int, otherwise it will just overflow etc. */
						waiting = std::max((int)waiting - (int)((GB(r, 8, 2) - 1) * num_dests), 0);
						waiting_changed = true;
					}
				}

				/* At some point we really must cap the cargo. Previously this
				 * was a strict 4095, but now we'll have a less strict, but
				 * increasingly aggressive truncation of the amount of cargo. */
				static const uint WAITING_CARGO_THRESHOLD  = 1 << 12;
				static const uint WAITING_CARGO_CUT_FACTOR = 1 <<  6;
				static const uint MAX_WAITING_CARGO        = 1 << 15;

				uint normalised_waiting_cargo_threshold = WAITING_CARGO_THRESHOLD;
				if (_settings_game.station.station_size_rating_cargo_amount) {
					if (st->station_tiles > 1) normalised_waiting_cargo_threshold *= st->station_tiles;
					normalised_waiting_cargo_threshold /= 8;
				}

				if (waiting > normalised_waiting_cargo_threshold) {
					const uint difference = waiting - normalised_waiting_cargo_threshold;
					waiting -= (difference / WAITING_CARGO_CUT_FACTOR);
					const uint normalised_max_waiting_cargo = normalised_waiting_cargo_threshold * (MAX_WAITING_CARGO / WAITING_CARGO_THRESHOLD);
					waiting = std::min(waiting, normalised_max_waiting_cargo);
					waiting_changed = true;
				}

				/* We can't truncate cargo that's already reserved for loading.
				 * Thus StoredCount() here. */
				if (waiting_changed && waiting < ge->CargoAvailableCount()) {
					/* Feed back the exact own waiting cargo at this station for the
					 * next rating calculation. */
					ge->max_waiting_cargo = 0;

					TruncateCargo(cs, ge, ge->CargoAvailableCount() - waiting);
				} else {
					/* If the average number per next hop is low, be more forgiving. */
					ge->max_waiting_cargo = waiting_avg;
				}
			}
		}
	}

	StationID index = st->index;

	if (waiting_changed) {
		SetWindowDirty(WC_STATION_VIEW, index); // update whole window
	} else {
		SetWindowWidgetDirty(WC_STATION_VIEW, index, WID_SV_ACCEPT_RATING_LIST); // update only ratings list
	}
}

/**
 * Reroute cargo of type c at station st or in any vehicles unloading there.
 * Make sure the cargo's new next hop is neither "avoid" nor "avoid2".
 * @param st Station to be rerouted at.
 * @param c Type of cargo.
 * @param avoid Original next hop of cargo, avoid this.
 * @param avoid2 Another station to be avoided when rerouting.
 */
void RerouteCargo(Station *st, CargoType c, StationID avoid, StationID avoid2)
{
	GoodsEntry &ge = st->goods[c];

	/* Reroute cargo in station. */
	if (ge.data != nullptr) ge.data->cargo.Reroute(UINT_MAX, &ge.data->cargo, avoid, avoid2, &ge);

	/* Reroute cargo staged to be transferred. */
	for (Vehicle *v : st->loading_vehicles) {
		for (Vehicle *u = v; u != nullptr; u = u->Next()) {
			if (u->cargo_type != c) continue;
			u->cargo.Reroute(UINT_MAX, &u->cargo, avoid, avoid2, &ge);
		}
	}
}

/**
 * Reroute cargo of type c from source at station st or in any vehicles unloading there.
 * Make sure the cargo's new next hop is neither "avoid" nor "avoid2".
 * @param st Station to be rerouted at.
 * @param c Type of cargo.
 * @param source Source station.
 * @param avoid Original next hop of cargo, avoid this.
 * @param avoid2 Another station to be avoided when rerouting.
 */
void RerouteCargoFromSource(Station *st, CargoType c, StationID source, StationID avoid, StationID avoid2)
{
	GoodsEntry &ge = st->goods[c];

	/* Reroute cargo in station. */
	if (ge.data != nullptr) ge.data->cargo.RerouteFromSource(UINT_MAX, &ge.data->cargo, source, avoid, avoid2, &ge);

	/* Reroute cargo staged to be transferred. */
	for (Vehicle *v : st->loading_vehicles) {
		for (; v != nullptr; v = v->Next()) {
			if (v->cargo_type != c) continue;
			v->cargo.RerouteFromSource(UINT_MAX, &v->cargo, source, avoid, avoid2, &ge);
		}
	}
}

robin_hood::unordered_flat_set<VehicleID> _delete_stale_links_vehicle_cache;

void ClearDeleteStaleLinksVehicleCache()
{
	_delete_stale_links_vehicle_cache.clear();
}

/**
 * Check all next hops of cargo packets in this station for existence of a
 * a valid link they may use to travel on. Reroute any cargo not having a valid
 * link and remove timed out links found like this from the linkgraph. We're
 * not all links here as that is expensive and useless. A link no one is using
 * doesn't hurt either.
 * @param from Station to check.
 */
void DeleteStaleLinks(Station *from)
{
	for (CargoType c = 0; c < NUM_CARGO; ++c) {
		const bool auto_distributed = (_settings_game.linkgraph.GetDistributionType(c) != DT_MANUAL);
		GoodsEntry &ge = from->goods[c];
		LinkGraph *lg = LinkGraph::GetIfValid(ge.link_graph);
		if (lg == nullptr) continue;
		lg->MutableIterateEdgesFromNode(ge.node, [&](LinkGraph::EdgeIterationHelper edge_helper) -> LinkGraph::EdgeIterationResult {
			Edge edge = edge_helper.GetEdge();
			NodeID to_id = edge_helper.to_id;

			LinkGraph::EdgeIterationResult result = LinkGraph::EdgeIterationResult::None;

			Station *to = Station::Get((*lg)[to_id].Station());
			assert(to->goods[c].node == to_id);
			assert(EconTime::CurDate() >= edge.LastUpdate());
			const EconTime::DateDelta timeout{std::max<int>((LinkGraph::MIN_TIMEOUT_DISTANCE + (DistanceManhattan(from->xy, to->xy) >> 3)) / DayLengthFactor(), 1)};
			if (edge.LastAircraftUpdate() != EconTime::INVALID_DATE && (EconTime::CurDate() - edge.LastAircraftUpdate()) > timeout) {
				edge.ClearAircraft();
			}
			if ((EconTime::CurDate() - edge.LastUpdate()) > timeout) {
				bool updated = false;

				if (auto_distributed) {
					/* Have all vehicles refresh their next hops before deciding to
					 * remove the node. */
					std::vector<Vehicle *> vehicles;
					for (const OrderList *l : OrderList::Iterate()) {
						bool found_from = false;
						bool found_to = false;
						for (const Order *order : l->Orders()) {
							if (!order->IsType(OT_GOTO_STATION) && !order->IsType(OT_IMPLICIT)) continue;
							if (order->GetDestination() == from->index) {
								found_from = true;
								if (found_to) break;
							} else if (order->GetDestination() == to->index) {
								found_to = true;
								if (found_from) break;
							}
						}
						if (!found_to || !found_from) continue;
						vehicles.push_back(l->GetFirstSharedVehicle());
					}

					auto iter = vehicles.begin();
					while (iter != vehicles.end()) {
						Vehicle *v = *iter;

						auto res = _delete_stale_links_vehicle_cache.insert(v->index);
						// Only run LinkRefresher if vehicle was not already in the cache
						if (res.second) {
							/* Do not refresh links of vehicles that have been stopped in depot for a long time. */
							if (!v->IsStoppedInDepot() || (EconTime::CurDate() - v->date_of_last_service) <=
									LinkGraph::STALE_LINK_DEPOT_TIMEOUT) {
								edge_helper.RecordSize();
								LinkRefresher::Run(v, false); // Don't allow merging. Otherwise lg might get deleted.
								if (edge_helper.RefreshIterationIfSizeChanged()) {
									edge = edge_helper.GetEdge();
								}
							}
						}
						if (edge.LastUpdate() == EconTime::CurDate()) {
							updated = true;
							break;
						}

						Vehicle *next_shared = v->NextShared();
						if (next_shared) {
							*iter = next_shared;
							++iter;
						} else {
							iter = vehicles.erase(iter);
						}

						if (iter == vehicles.end()) iter = vehicles.begin();
					}
				}

				if (!updated) {
					/* If it's still considered dead remove it. */
					result = LinkGraph::EdgeIterationResult::EraseEdge;
					if (ge.data != nullptr) ge.data->flows.DeleteFlows(to->index);
					RerouteCargo(from, c, to->index, from->index);
				}
			} else if (edge.LastUnrestrictedUpdate() != EconTime::INVALID_DATE && (EconTime::CurDate() - edge.LastUnrestrictedUpdate()) > timeout) {
				edge.Restrict();
				if (ge.data != nullptr) ge.data->flows.RestrictFlows(to->index);
				RerouteCargo(from, c, to->index, from->index);
			} else if (edge.LastRestrictedUpdate() != EconTime::INVALID_DATE && (EconTime::CurDate() - edge.LastRestrictedUpdate()) > timeout) {
				edge.Release();
			}

			return result;
		});
		assert(_scaled_tick_counter >= lg->LastCompression());
		if ((_scaled_tick_counter - lg->LastCompression()) > LinkGraph::COMPRESSION_INTERVAL) {
			lg->Compress();
		}
	}
}

/**
 * Increase capacity for a link stat given by station cargo and next hop.
 * @param st Station to get the link stats from.
 * @param cargo Cargo to increase stat for.
 * @param next_station_id Station the consist will be travelling to next.
 * @param capacity Capacity to add to link stat.
 * @param usage Usage to add to link stat.
 * @param mode Update mode to be applied.
 */
void IncreaseStats(Station *st, CargoType cargo, StationID next_station_id, uint capacity, uint usage, uint32_t time, EdgeUpdateMode mode)
{
	GoodsEntry &ge1 = st->goods[cargo];
	Station *st2 = Station::Get(next_station_id);
	GoodsEntry &ge2 = st2->goods[cargo];
	LinkGraph *lg = nullptr;
	if (ge1.link_graph == INVALID_LINK_GRAPH) {
		if (ge2.link_graph == INVALID_LINK_GRAPH) {
			if (LinkGraph::CanAllocateItem()) {
				lg = new LinkGraph(cargo);
				LinkGraphSchedule::instance.Queue(lg);
				ge2.link_graph = lg->index;
				ge2.node = lg->AddNode(st2);
			} else {
				Debug(misc, 0, "Can't allocate link graph");
			}
		} else {
			lg = LinkGraph::Get(ge2.link_graph);
		}
		if (lg) {
			ge1.link_graph = lg->index;
			ge1.node = lg->AddNode(st);
		}
	} else if (ge2.link_graph == INVALID_LINK_GRAPH) {
		lg = LinkGraph::Get(ge1.link_graph);
		ge2.link_graph = lg->index;
		ge2.node = lg->AddNode(st2);
	} else {
		lg = LinkGraph::Get(ge1.link_graph);
		if (ge1.link_graph != ge2.link_graph) {
			LinkGraph *lg2 = LinkGraph::Get(ge2.link_graph);
			if (lg->Size() < lg2->Size()) {
				LinkGraphSchedule::instance.Unqueue(lg);
				lg2->Merge(lg); // Updates GoodsEntries of lg
				lg = lg2;
			} else {
				LinkGraphSchedule::instance.Unqueue(lg2);
				lg->Merge(lg2); // Updates GoodsEntries of lg2
			}
		}
	}
	if (lg != nullptr) {
		lg->UpdateEdge(ge1.node, ge2.node, capacity, usage, time, mode);
	}
}

/* called for every station each tick */
static void StationHandleSmallTick(BaseStation *st)
{
	if ((st->facilities & FACIL_WAYPOINT) != 0 || !st->IsInUse()) return;

	uint8_t b = st->delete_ctr + 1;
	if (b >= STATION_RATING_TICKS) b = 0;
	st->delete_ctr = b;

	if (b == 0) UpdateStationRating(Station::From(st));
}

void UpdateAllStationRatings()
{
	for (Station *st : Station::Iterate()) {
		if (!st->IsInUse()) continue;
		UpdateStationRating(st);
	}
}

void OnTick_Station()
{
	if (_game_mode == GM_EDITOR) return;

	ClearDeleteStaleLinksVehicleCache();

	for (BaseStation *st : BaseStation::Iterate()) {
		StationHandleSmallTick(st);

		/* Clean up the link graph about once a week. */
		if (Station::IsExpected(st) && (_tick_counter + st->index) % STATION_LINKGRAPH_TICKS == 0) {
			DeleteStaleLinks(Station::From(st));
		};

		/* Run STATION_ACCEPTANCE_TICKS = 250 tick interval trigger for station animation.
		 * Station index is included so that triggers are not all done
		 * at the same time. */
		if ((_tick_counter + st->index) % STATION_ACCEPTANCE_TICKS == 0) {
			/* Stop processing this station if it was deleted */
			if (!StationHandleBigTick(st)) continue;
			TriggerStationAnimation(st, st->xy, SAT_250_TICKS);
			TriggerRoadStopAnimation(st, st->xy, SAT_250_TICKS);
			if (Station::IsExpected(st)) AirportAnimationTrigger(Station::From(st), AAT_STATION_250_TICKS);
		}
	}
}

/** Daily loop for stations. */
void StationDailyLoop()
{
	// Only record cargo history every second day.
	if (EconTime::CurDate().base() % 2 != 0) {
		for (Station *st : Station::Iterate()) {
			st->UpdateCargoHistory();
		}
		InvalidateWindowClassesData(WC_STATION_CARGO);
	}
}

/** Monthly loop for stations. */
void StationMonthlyLoop()
{
	for (Station *st : Station::Iterate()) {
		for (GoodsEntry &ge : st->goods) {
			SB(ge.status, GoodsEntry::GES_LAST_MONTH, 1, GB(ge.status, GoodsEntry::GES_CURRENT_MONTH, 1));
			ClrBit(ge.status, GoodsEntry::GES_CURRENT_MONTH);
		}
	}
}


void ModifyStationRatingAround(TileIndex tile, Owner owner, int amount, uint radius)
{
	ForAllStationsRadius(tile, radius, [&](Station *st) {
		if (st->owner == owner && DistanceManhattan(tile, st->xy) <= radius) {
			for (GoodsEntry &ge : st->goods) {
				if (ge.status != 0) {
					ge.rating = ClampTo<uint8_t>(ge.rating + amount);
				}
			}
		}
	});
}

static uint UpdateStationWaiting(Station *st, CargoType type, uint amount, Source source)
{
	/* We can't allocate a CargoPacket? Then don't do anything
	 * at all; i.e. just discard the incoming cargo. */
	if (!CargoPacket::CanAllocateItem()) return 0;

	GoodsEntry &ge = st->goods[type];
	amount += ge.amount_fract;
	ge.amount_fract = GB(amount, 0, 8);

	amount >>= 8;
	/* No new "real" cargo item yet. */
	if (amount == 0) return 0;

	StationID next = ge.GetVia(st->index);
	ge.CreateData().cargo.Append(new CargoPacket(st->index, amount, source), next);
	LinkGraph *lg = nullptr;
	if (ge.link_graph == INVALID_LINK_GRAPH) {
		if (LinkGraph::CanAllocateItem()) {
			lg = new LinkGraph(type);
			LinkGraphSchedule::instance.Queue(lg);
			ge.link_graph = lg->index;
			ge.node = lg->AddNode(st);
		} else {
			Debug(misc, 0, "Can't allocate link graph");
		}
	} else {
		lg = LinkGraph::Get(ge.link_graph);
	}
	if (lg != nullptr) (*lg)[ge.node].UpdateSupply(amount);

	if (!ge.HasRating()) {
		InvalidateWindowData(WC_STATION_LIST, st->owner);
		SetBit(ge.status, GoodsEntry::GES_RATING);
	}

	TriggerStationRandomisation(st, st->xy, SRT_NEW_CARGO, type);
	TriggerStationAnimation(st, st->xy, SAT_NEW_CARGO, type);
	AirportAnimationTrigger(st, AAT_STATION_NEW_CARGO, type);
	TriggerRoadStopAnimation(st, st->xy, SAT_NEW_CARGO, type);
	TriggerRoadStopRandomisation(st, st->xy, RSRT_NEW_CARGO, type);

	SetWindowDirty(WC_STATION_VIEW, st->index);
	st->MarkTilesDirty(true);
	return amount;
}

static bool IsUniqueStationName(std::string_view name)
{
	for (const Station *st : Station::Iterate()) {
		if (!st->name.empty() && st->name == name) return false;
	}

	return true;
}

/**
 * Rename a station
 * @param flags operation to perform
 * @param station_id station ID that is to be renamed
 * @param generate whether to generate a new default name, if resetting name
 * @param text the new name or an empty string when resetting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdRenameStation(DoCommandFlag flags, StationID station_id, bool generate, const std::string &text)
{
	Station *st = Station::GetIfValid(station_id);
	if (st == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(st->owner);
	if (ret.Failed()) return ret;

	bool reset = text.empty();

	if (!reset) {
		if (Utf8StringLength(text) >= MAX_LENGTH_STATION_NAME_CHARS) return CMD_ERROR;
		if (!IsUniqueStationName(text)) return CommandCost(STR_ERROR_NAME_MUST_BE_UNIQUE);
	}

	if (flags & DC_EXEC) {
		st->cached_name.clear();
		if (reset) {
			st->name.clear();
			if (generate && st->industry == nullptr) {
				StationNaming name_class;
				if (st->facilities & FACIL_AIRPORT) {
					name_class = STATIONNAMING_AIRPORT;
				} else if (st->facilities & FACIL_DOCK) {
					name_class = STATIONNAMING_DOCK;
				} else if (st->facilities & FACIL_TRAIN) {
					name_class = STATIONNAMING_RAIL;
				} else if (st->facilities & (FACIL_BUS_STOP | FACIL_TRUCK_STOP)) {
					name_class = STATIONNAMING_ROAD;
				} else {
					name_class = STATIONNAMING_RAIL;
				}
				Random(); // Advance random seed each time this is called
				st->string_id = GenerateStationName(st, st->xy, name_class, true);
			}
		} else {
			st->name = text;
		}

		st->UpdateVirtCoord();
		InvalidateWindowData(WC_STATION_LIST, st->owner, 1);
	}

	return CommandCost();
}

/**
 * Exchange station names
 * @param flags operation to perform
 * @param station_id1 station ID to exchange name with
 * @param station_id2 station ID to exchange name with
 * @return the cost of this operation or an error
 */
CommandCost CmdExchangeStationNames(DoCommandFlag flags, StationID station_id1, StationID station_id2)
{
	Station *st = Station::GetIfValid(station_id1);
	if (st == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(st->owner);
	if (ret.Failed()) return ret;

	if (st->industry != nullptr) return CommandCost(STR_ERROR_STATION_ATTACHED_TO_INDUSTRY);

	Station *st2 = Station::GetIfValid(station_id2);
	if (st2 == nullptr) return CMD_ERROR;

	ret = CheckOwnership(st2->owner);
	if (ret.Failed()) return ret;

	if (st2->industry != nullptr) return CommandCost(STR_ERROR_STATION_ATTACHED_TO_INDUSTRY);

	if (st->town != st2->town) return CommandCost(STR_ERROR_STATIONS_NOT_IN_SAME_TOWN);

	if (flags & DC_EXEC) {
		st->cached_name.clear();
		st2->cached_name.clear();
		std::swap(st->name, st2->name);
		std::swap(st->string_id, st2->string_id);
		std::swap(st->indtype, st2->indtype);
		std::swap(st->extra_name_index, st2->extra_name_index);
		st->UpdateVirtCoord();
		st2->UpdateVirtCoord();
		InvalidateWindowData(WC_STATION_LIST, st->owner, 1);
	}

	return CommandCost();
}

/**
 * Change whether a cargo may be supplied to a station
 * @param flags operation to perform
 * @param station_id station ID
 * @param cargo cargo ID
 * @param allow whether to allow supply
 * @return the cost of this operation or an error
 */
CommandCost CmdSetStationCargoAllowedSupply(DoCommandFlag flags, StationID station_id, CargoType cargo, bool allow)
{
	Station *st = Station::GetIfValid(station_id);
	if (st == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(st->owner);
	if (ret.Failed()) return ret;

	if (cargo >= NUM_CARGO) return CMD_ERROR;

	if (flags & DC_EXEC) {
		GoodsEntry &ge = st->goods[cargo];
		AssignBit(ge.status, GoodsEntry::GES_NO_CARGO_SUPPLY, !allow);
		InvalidateWindowData(WC_STATION_VIEW, st->index, -1);
	}

	return CommandCost();
}

static void AddNearbyStationsByCatchment(TileIndex tile, StationList &stations, StationList &nearby)
{
	for (Station *st : nearby) {
		if (st->TileIsInCatchment(tile)) stations.insert(st);
	}
}

/**
 * Run a tile loop to find stations around a tile, on demand. Cache the result for further requests
 * @return pointer to a StationList containing all stations found
 */
const StationList &StationFinder::GetStations()
{
	if (this->tile != INVALID_TILE) {
		if (IsTileType(this->tile, MP_HOUSE)) {
			/* Town nearby stations need to be filtered per tile. */
			assert(this->w == 1 && this->h == 1);
			AddNearbyStationsByCatchment(this->tile, this->stations, Town::GetByTile(this->tile)->stations_near);
		} else {
			ForAllStationsAroundTiles(*this, [this](Station *st, TileIndex) {
				this->stations.insert(st);
				return true;
			});
		}
		this->tile = INVALID_TILE;
	}
	return this->stations;
}


static bool CanMoveGoodsToStation(const Station *st, CargoType type)
{
	/* Is the station reserved exclusively for somebody else? */
	if (st->owner != OWNER_NONE && st->town->exclusive_counter > 0 && st->town->exclusivity != st->owner) return false;

	/* Lowest possible rating, better not to give cargo anymore. */
	if (st->goods[type].rating == 0) return false;

	if (!st->goods[type].IsSupplyAllowed()) return false;

	/* Selectively servicing stations, and not this one. */
	if (_settings_game.order.selectgoods && !st->goods[type].HasVehicleEverTriedLoading()) return false;

	if (IsCargoInClass(type, CargoClass::Passengers)) {
		/* Passengers are never served by just a truck stop. */
		if (st->facilities == FACIL_TRUCK_STOP) return false;
	} else {
		/* Non-passengers are never served by just a bus stop. */
		if (st->facilities == FACIL_BUS_STOP) return false;
	}
	return true;
}

uint MoveGoodsToStation(CargoType type, uint amount, Source source, const StationList &all_stations, Owner exclusivity)
{
	/* Return if nothing to do. Also the rounding below fails for 0. */
	if (all_stations.empty()) return 0;
	if (amount == 0) return 0;

	Station *first_station = nullptr;
	typedef std::pair<Station *, uint> StationInfo;
	std::vector<StationInfo> used_stations;

	for (Station *st : all_stations) {
		if (exclusivity != INVALID_OWNER && exclusivity != st->owner) continue;
		if (!CanMoveGoodsToStation(st, type)) continue;

		/* Avoid allocating a vector if there is only one station to significantly
		 * improve performance in this common case. */
		if (first_station == nullptr) {
			first_station = st;
			continue;
		}
		if (used_stations.empty()) {
			used_stations.reserve(2);
			used_stations.emplace_back(first_station, 0);
		}
		used_stations.emplace_back(st, 0);
	}

	/* no stations around at all? */
	if (first_station == nullptr) return 0;

	if (used_stations.empty()) {
		/* only one station around */
		amount *= first_station->goods[type].rating + 1;
		return UpdateStationWaiting(first_station, type, amount, source);
	}

	uint company_best[OWNER_NONE + 1] = {};  // best rating for each company, including OWNER_NONE
	uint company_sum[OWNER_NONE + 1] = {};   // sum of ratings for each company
	uint best_rating = 0;
	uint best_sum = 0;  // sum of best ratings for each company

	for (auto &p : used_stations) {
		auto owner = p.first->owner;
		auto rating = p.first->goods[type].rating;
		if (rating > company_best[owner]) {
			best_sum += rating - company_best[owner];  // it's usually faster than iterating companies later
			company_best[owner] = rating;
			if (rating > best_rating) best_rating = rating;
		}
		company_sum[owner] += rating;
	}

	/* From now we'll calculate with fractional cargo amounts.
	 * First determine how much cargo we really have. */
	amount *= best_rating + 1;

	uint moving = 0;
	for (auto &p : used_stations) {
		uint owner = p.first->owner;
		/* Multiply the amount by (company best / sum of best for each company) to get cargo allocated to a company
		 * and by (station rating / sum of ratings in a company) to get the result for a single station. */
		p.second = ((uint64_t) amount) * ((uint64_t) company_best[owner]) * ((uint64_t) p.first->goods[type].rating) / (best_sum * company_sum[owner]);
		moving += p.second;
	}

	/* If there is some cargo left due to rounding issues distribute it among the best rated stations. */
	if (amount > moving) {
		std::stable_sort(used_stations.begin(), used_stations.end(), [type](const StationInfo &a, const StationInfo &b) {
			return b.first->goods[type].rating < a.first->goods[type].rating;
		});

		uint to_deliver = amount - moving;
		uint step_size = CeilDivT<uint>(to_deliver, (uint)used_stations.size());
		for (uint i = 0; i < used_stations.size() && to_deliver > 0; i++) {
			uint delivery = std::min<uint>(to_deliver, step_size);
			used_stations[i].second += delivery;
			to_deliver -= delivery;
		}
	}

	uint moved = 0;
	for (auto &p : used_stations) {
		moved += UpdateStationWaiting(p.first, type, p.second, source);
	}

	return moved;
}

void UpdateStationDockingTiles(Station *st)
{
	st->docking_station.Clear();
	st->docking_tiles.clear();

	/* For neutral stations, start with the industry area instead of dock area */
	const TileArea *area = st->industry != nullptr ? &st->industry->location : &st->ship_station;

	if (area->tile == INVALID_TILE) return;

	int x = TileX(area->tile);
	int y = TileY(area->tile);

	/* Expand the area by a tile on each side while
	 * making sure that we remain inside the map. */
	int x2 = std::min<int>(x + area->w + 1, Map::SizeX());
	int x1 = std::max<int>(x - 1, 0);

	int y2 = std::min<int>(y + area->h + 1, Map::SizeY());
	int y1 = std::max<int>(y - 1, 0);

	TileArea ta(TileXY(x1, y1), TileXY(x2 - 1, y2 - 1));
	for (TileIndex tile : ta) {
		if (IsValidTile(tile) && IsPossibleDockingTile(tile)) CheckForDockingTile(tile);
	}
}

void BuildOilRig(TileIndex tile)
{
	if (!Station::CanAllocateItem()) {
		Debug(misc, 0, "Can't allocate station for oilrig at 0x{:X}, reverting to oilrig only", tile);
		return;
	}

	Station *st = new Station(tile);
	_station_kdtree.Insert(st->index);
	st->town = ClosestTownFromTile(tile, UINT_MAX);

	st->string_id = GenerateStationName(st, tile, STATIONNAMING_OILRIG);

	assert_tile(IsTileType(tile, MP_INDUSTRY), tile);
	/* Mark industry as associated both ways */
	st->industry = Industry::GetByTile(tile);
	st->industry->neutral_station = st;
	DeleteAnimatedTile(tile);
	MakeOilrig(tile, st->index, GetWaterClass(tile));

	st->owner = OWNER_NONE;
	st->airport.type = AT_OILRIG;
	st->airport.Add(tile);
	st->ship_station.Add(tile);
	st->facilities = FACIL_AIRPORT | FACIL_DOCK;
	st->build_date = CalTime::CurDate();
	UpdateStationDockingTiles(st);

	st->rect.BeforeAddTile(tile, StationRect::ADD_FORCE);

	st->UpdateVirtCoord();

	/* An industry tile has now been replaced with a station tile, this may change the overlap between station catchments and industry tiles.
	 * Recalculate the station catchment for all stations currently in the industry's nearby list.
	 * Clear the industry's station nearby list first because Station::RecomputeCatchment cannot remove nearby industries in this case. */
	if (_settings_game.station.serve_neutral_industries) {
		StationList nearby = std::move(st->industry->stations_near);
		st->industry->stations_near.clear();
		for (Station *st_near : nearby) {
			st_near->RecomputeCatchment(true);
			UpdateStationAcceptance(st_near, true);
		}
	}

	st->RecomputeCatchment();
	UpdateStationAcceptance(st, false);
	ZoningMarkDirtyStationCoverageArea(st);
}

void DeleteOilRig(TileIndex tile)
{
	Station *st = Station::GetByTile(tile);
	ZoningMarkDirtyStationCoverageArea(st);

	MakeWaterKeepingClass(tile, OWNER_NONE);

	assert(st->facilities == (FACIL_AIRPORT | FACIL_DOCK) && st->airport.type == AT_OILRIG);
	if (st->industry != nullptr && st->industry->neutral_station == st) {
		/* Don't leave dangling neutral station pointer */
		st->industry->neutral_station = nullptr;
	}
	delete st;
}

static void ChangeTileOwner_Station(TileIndex tile, Owner old_owner, Owner new_owner)
{
	if (IsAnyRoadStopTile(tile)) {
		for (RoadTramType rtt : _roadtramtypes) {
			/* Update all roadtypes, no matter if they are present */
			if (GetRoadOwner(tile, rtt) == old_owner) {
				RoadType rt = GetRoadType(tile, rtt);
				if (rt != INVALID_ROADTYPE) {
					/* A drive-through road-stop has always two road bits. No need to dirty windows here, we'll redraw the whole screen anyway. */
					Company::Get(old_owner)->infrastructure.road[rt] -= 2;
					if (new_owner != INVALID_OWNER) Company::Get(new_owner)->infrastructure.road[rt] += 2;
				}
				SetRoadOwner(tile, rtt, new_owner == INVALID_OWNER ? OWNER_NONE : new_owner);
			}
		}
	}

	if (!IsTileOwner(tile, old_owner)) return;

	if (new_owner != INVALID_OWNER) {
		/* Update company infrastructure counts. Only do it here
		 * if the new owner is valid as otherwise the clear
		 * command will do it for us. No need to dirty windows
		 * here, we'll redraw the whole screen anyway.*/
		Company *old_company = Company::Get(old_owner);
		Company *new_company = Company::Get(new_owner);

		/* Update counts for underlying infrastructure. */
		switch (GetStationType(tile)) {
			case StationType::Rail:
			case StationType::RailWaypoint:
				if (!IsStationTileBlocked(tile)) {
					old_company->infrastructure.rail[GetRailType(tile)]--;
					new_company->infrastructure.rail[GetRailType(tile)]++;
				}
				break;

			case StationType::Bus:
			case StationType::Truck:
			case StationType::RoadWaypoint:
				/* Road stops were already handled above. */
				break;

			case StationType::Buoy:
			case StationType::Dock:
				if (GetWaterClass(tile) == WATER_CLASS_CANAL) {
					old_company->infrastructure.water--;
					new_company->infrastructure.water++;
				}
				break;

			default:
				break;
		}

		/* Update station tile count. */
		if (!IsBuoy(tile) && !IsAirport(tile)) {
			old_company->infrastructure.station--;
			new_company->infrastructure.station++;
		}

		/* for buoys, owner of tile is owner of water, st->owner == OWNER_NONE */
		SetTileOwner(tile, new_owner);
		InvalidateWindowClassesData(WC_STATION_LIST, 0);
	} else {
		if (IsDriveThroughStopTile(tile)) {
			/* Remove the drive-through road stop */
			if (IsRoadWaypoint(tile)) {
				Command<CMD_REMOVE_FROM_ROAD_WAYPOINT>::Do(DC_EXEC | DC_BANKRUPT, tile, tile);
			} else {
				Command<CMD_REMOVE_ROAD_STOP>::Do(DC_EXEC | DC_BANKRUPT, tile, 1, 1, (GetStationType(tile) == StationType::Truck) ? RoadStopType::Truck : RoadStopType::Bus, false);
			}
			assert_tile(IsTileType(tile, MP_ROAD), tile);
			/* Change owner of tile and all roadtypes */
			ChangeTileOwner(tile, old_owner, new_owner);
		} else {
			Command<CMD_LANDSCAPE_CLEAR>::Do(DC_EXEC | DC_BANKRUPT, tile);
			/* Set tile owner of water under (now removed) buoy and dock to OWNER_NONE.
			 * Update owner of buoy if it was not removed (was in orders).
			 * Do not update when owned by OWNER_WATER (sea and rivers). */
			if ((IsTileType(tile, MP_WATER) || IsBuoyTile(tile)) && IsTileOwner(tile, old_owner)) SetTileOwner(tile, OWNER_NONE);
		}
	}
}

/**
 * Check if a drive-through road stop tile can be cleared.
 * Road stops built on town-owned roads check the conditions
 * that would allow clearing of the original road.
 * @param tile The road stop tile to check.
 * @param flags Command flags.
 * @return A succeeded command if the road can be removed, a failed command with the relevant error message otherwise.
 */
static CommandCost CanRemoveRoadWithStop(TileIndex tile, DoCommandFlag flags)
{
	/* Water flooding can always clear road stops. */
	if (_current_company == OWNER_WATER) return CommandCost();

	CommandCost ret;

	if (GetRoadTypeTram(tile) != INVALID_ROADTYPE) {
		Owner tram_owner = GetRoadOwner(tile, RTT_TRAM);
		if (tram_owner != OWNER_NONE) {
			ret = CheckOwnership(tram_owner);
			if (ret.Failed()) return ret;
		}
	}

	if (GetRoadTypeRoad(tile) != INVALID_ROADTYPE) {
		Owner road_owner = GetRoadOwner(tile, RTT_ROAD);
		if (road_owner == OWNER_TOWN) {
			ret = CheckAllowRemoveRoad(tile, GetAnyRoadBits(tile, RTT_ROAD), OWNER_TOWN, RTT_ROAD, flags);
			if (ret.Failed()) return ret;
		} else if (road_owner != OWNER_NONE) {
			ret = CheckOwnership(road_owner);
			if (ret.Failed()) return ret;
		}
	}

	return CommandCost();
}

static CommandCost RemoveRoadStopAndUpdateRoadCachedOneWayState(TileIndex tile, DoCommandFlag flags)
{
	CommandCost cost = RemoveRoadStop(tile, flags);
	if ((flags & DC_EXEC) && cost.Succeeded()) UpdateRoadCachedOneWayStatesAroundTile(tile);
	return cost;
}

/**
 * Clear a single tile of a station.
 * @param tile The tile to clear.
 * @param flags The DoCommandOld flags related to the "command".
 * @return The cost, or error of clearing.
 */
CommandCost ClearTile_Station(TileIndex tile, DoCommandFlag flags)
{
	if (flags & DC_AUTO) {
		switch (GetStationType(tile)) {
			default: break;
			case StationType::Rail:         return CommandCost(STR_ERROR_MUST_DEMOLISH_RAILROAD);
			case StationType::RailWaypoint: return CommandCost(STR_ERROR_BUILDING_MUST_BE_DEMOLISHED);
			case StationType::RoadWaypoint: return CommandCost(STR_ERROR_BUILDING_MUST_BE_DEMOLISHED);
			case StationType::Airport:      return CommandCost(STR_ERROR_MUST_DEMOLISH_AIRPORT_FIRST);
			case StationType::Truck:        return CommandCost(HasTileRoadType(tile, RTT_TRAM) ? STR_ERROR_MUST_DEMOLISH_CARGO_TRAM_STATION_FIRST : STR_ERROR_MUST_DEMOLISH_TRUCK_STATION_FIRST);
			case StationType::Bus:          return CommandCost(HasTileRoadType(tile, RTT_TRAM) ? STR_ERROR_MUST_DEMOLISH_PASSENGER_TRAM_STATION_FIRST : STR_ERROR_MUST_DEMOLISH_BUS_STATION_FIRST);
			case StationType::Buoy:         return CommandCost(STR_ERROR_BUOY_IN_THE_WAY);
			case StationType::Dock:         return CommandCost(STR_ERROR_MUST_DEMOLISH_DOCK_FIRST);

			case StationType::Oilrig:
				SetDParam(1, STR_INDUSTRY_NAME_OIL_RIG);
				return CommandCost(STR_ERROR_GENERIC_OBJECT_IN_THE_WAY);
		}
	}

	switch (GetStationType(tile)) {
		case StationType::Rail:         return RemoveRailStation(tile, flags);
		case StationType::RailWaypoint: return RemoveRailWaypoint(tile, flags);
		case StationType::Airport:      return RemoveAirport(tile, flags);

		case StationType::Truck:
		case StationType::Bus:
			if (IsDriveThroughStopTile(tile)) {
				CommandCost remove_road = CanRemoveRoadWithStop(tile, flags);
				if (remove_road.Failed()) return remove_road;
			}
			return RemoveRoadStopAndUpdateRoadCachedOneWayState(tile, flags);

		case StationType::Buoy:     return RemoveBuoy(tile, flags);
		case StationType::Dock:     return RemoveDock(tile, flags);

		case StationType::RoadWaypoint:
			if (IsDriveThroughStopTile(tile)) {
				CommandCost remove_road = CanRemoveRoadWithStop(tile, flags);
				if (remove_road.Failed()) return remove_road;
			}
			return RemoveRoadStopAndUpdateRoadCachedOneWayState(tile, flags);

		default:
			break;
	}

	return CMD_ERROR;
}

static CommandCost TerraformTile_Station(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	if (_settings_game.construction.build_on_slopes && AutoslopeEnabled()) {
		/* TODO: If you implement newgrf callback 149 'land slope check', you have to decide what to do with it here.
		 *       TTDP does not call it.
		 */
		if (GetTileMaxZ(tile) == z_new + GetSlopeMaxZ(tileh_new)) {
			switch (GetStationType(tile)) {
				case StationType::RailWaypoint:
				case StationType::Rail: {
					if (!AutoslopeCheckForAxis(tile, z_new, tileh_new, GetRailStationAxis(tile))) break;
					return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
				}

				case StationType::Airport:
					return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);

				case StationType::Truck:
				case StationType::Bus:
				case StationType::RoadWaypoint: {
					if (IsDriveThroughStopTile(tile)) {
						if (!AutoslopeCheckForAxis(tile, z_new, tileh_new, GetDriveThroughStopAxis(tile))) break;
					} else {
						if (!AutoslopeCheckForEntranceEdge(tile, z_new, tileh_new, GetBayRoadStopDir(tile))) break;
					}
					return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
				}

				default: break;
			}
		}
	}
	return Command<CMD_LANDSCAPE_CLEAR>::Do(flags, tile);
}

FlowStat::iterator FlowStat::erase_item(FlowStat::iterator iter, uint flow_reduction)
{
	assert(!this->empty());
	const uint offset = iter - this->begin();
	const iterator last = this->end() - 1;
	for (; iter < last; ++iter) {
		*iter = { (iter + 1)->first - flow_reduction, (iter + 1)->second };
	}
	--this->count;
	if (this->count == 2) {
		// transition from external to internal storage
		ShareEntry *ptr = this->storage.ptr_shares.buffer;
		this->storage.inline_shares[0] = ptr[0];
		this->storage.inline_shares[1] = ptr[1];
		free(ptr);
	}
	return this->begin() + offset;
}

/**
 * Get flow for a station.
 * @param st Station to get flow for.
 * @return Flow for st.
 */
uint FlowStat::GetShare(StationID st) const
{
	uint32_t prev = 0;
	for (const_iterator it = this->begin(); it != this->end(); ++it) {
		if (it->second == st) {
			return it->first - prev;
		} else {
			prev = it->first;
		}
	}
	return 0;
}

/**
 * Get a station a package can be routed to, but exclude the given ones.
 * @param excluded StationID not to be selected.
 * @param excluded2 Another StationID not to be selected.
 * @return A station ID from the shares map.
 */
StationID FlowStat::GetVia(StationID excluded, StationID excluded2) const
{
	if (this->unrestricted == 0) return INVALID_STATION;
	assert(!this->empty());
	const_iterator it = std::upper_bound(this->data(), this->data() + this->count, RandomRange(this->unrestricted));
	assert(it != this->end() && it->first <= this->unrestricted);
	if (it->second != excluded && it->second != excluded2) return it->second;

	/* We've hit one of the excluded stations.
	 * Draw another share, from outside its range. */

	uint end = it->first;
	uint begin = (it == this->begin() ? 0 : (--it)->first);
	uint interval = end - begin;
	if (interval >= this->unrestricted) return INVALID_STATION; // Only one station in the map.
	uint new_max = this->unrestricted - interval;
	uint rand = RandomRange(new_max);
	const_iterator it2 = (rand < begin) ? this->upper_bound(rand) :
			this->upper_bound(rand + interval);
	assert(it2 != this->end() && it2->first <= this->unrestricted);
	if (it2->second != excluded && it2->second != excluded2) return it2->second;

	/* We've hit the second excluded station.
	 * Same as before, only a bit more complicated. */

	uint end2 = it2->first;
	uint begin2 = (it2 == this->begin() ? 0 : (--it2)->first);
	uint interval2 = end2 - begin2;
	if (interval2 >= new_max) return INVALID_STATION; // Only the two excluded stations in the map.
	new_max -= interval2;
	if (begin > begin2) {
		Swap(begin, begin2);
		Swap(end, end2);
		Swap(interval, interval2);
	}
	rand = RandomRange(new_max);
	const_iterator it3 = this->upper_bound(this->unrestricted);
	if (rand < begin) {
		it3 = this->upper_bound(rand);
	} else if (rand < begin2 - interval) {
		it3 = this->upper_bound(rand + interval);
	} else {
		it3 = this->upper_bound(rand + interval + interval2);
	}
	assert(it3 != this->end() && it3->first <= this->unrestricted);
	return it3->second;
}

/**
 * Change share for specified station. By specifying INT_MIN as parameter you
 * can erase a share. Newly added flows will be unrestricted.
 * @param st Next Hop to be removed.
 * @param flow Share to be added or removed.
 */
void FlowStat::ChangeShare(StationID st, int flow)
{
	/* We assert only before changing as afterwards the shares can actually
	 * be empty. In that case the whole flow stat must be deleted then. */
	assert(!this->empty());

	uint last_share = 0;
	for (iterator it(this->begin()); it != this->end(); ++it) {
		if (it->second == st) {
			uint share = it->first - last_share;
			if (flow < 0 && (flow == INT_MIN || (uint)(-flow) >= share)) {
				if (it->first <= this->unrestricted) this->unrestricted -= share;
				this->erase_item(it, share);
				break; // remove the whole share
			}
			if (it->first <= this->unrestricted) this->unrestricted += flow;
			for (; it != this->end(); ++it) {
				it->first += flow;
			}
			flow = 0;
			break;
		}
		last_share = it->first;
	}
	if (flow > 0) {
		// must be non-empty here
		last_share = (this->end() - 1)->first;
		this->AppendShare(st, (uint)flow, true); // true to avoid changing this->unrestricted, which we fixup below
		if (this->unrestricted < last_share) {
			// Move to front to unrestrict
			this->ReleaseShare(st);
		} else {
			// First restricted item, so bump unrestricted count
			this->unrestricted += flow;
		}
	}
}

/**
 * Restrict a flow by moving it to the end of the map and decreasing the amount
 * of unrestricted flow.
 * @param st Station of flow to be restricted.
 */
void FlowStat::RestrictShare(StationID st)
{
	assert(!this->empty());
	iterator it = this->begin();
	const iterator end = this->end();
	uint last_share = 0;
	for (; it != end; ++it) {
		if (it->first > this->unrestricted) return; // Not present or already restricted.
		if (it->second == st) {
			uint flow = it->first - last_share;
			this->unrestricted -= flow;
			if (this->unrestricted == last_share) return; // No further action required
			const iterator last = end - 1;
			for (iterator jt = it; jt != last; ++jt) {
				*jt = { (jt + 1)->first - flow, (jt + 1)->second };
			}
			*last = { flow + (last - 1)->first, st };
			return;
		}
		last_share = it->first;
	}
}

/**
 * Release ("unrestrict") a flow by moving it to the begin of the map and
 * increasing the amount of unrestricted flow.
 * @param st Station of flow to be released.
 */
void FlowStat::ReleaseShare(StationID st)
{
	assert(!this->empty());
	iterator it = this->end() - 1;
	const iterator start = this->begin();
	for (; it >= start; --it) {
		if (it->first < this->unrestricted) return; // Already unrestricted
		if (it->second == st) {
			if (it - 1 >= start) {
				uint flow = it->first - (it - 1)->first;
				this->unrestricted += flow;
				if (it->first == this->unrestricted) return; // No further action required
				for (iterator jt = it; jt != start; --jt) {
					*jt = { (jt - 1)->first + flow, (jt - 1)->second };
				}
				*start = { flow, st };
			} else {
				// already at start
				this->unrestricted = it->first;
			}
			return;
		}
	}
}

/**
 * Scale all shares from link graph's runtime to monthly values.
 * @param runtime Time the link graph has been running without compression, in scaled ticks.
 * @param day_length_factor Day length factor to use.
 * @pre runtime must be greater than 0 as we don't want infinite flow values.
 */
void FlowStat::ScaleToMonthly(uint runtime, uint8_t day_length_factor)
{
	assert(runtime > 0);
	uint share = 0;
	for (iterator i = this->begin(); i != this->end(); ++i) {
		share = std::max(share + 1, ClampTo<uint>((static_cast<uint64_t>(i->first) * 30 * DAY_TICKS * day_length_factor) / runtime));
		if (this->unrestricted == i->first) this->unrestricted = share;
		i->first = share;
	}
}

/**
 * Add some flow from "origin", going via "via".
 * @param origin Origin of the flow.
 * @param via Next hop.
 * @param flow Amount of flow to be added.
 */
void FlowStatMap::AddFlow(StationID origin, StationID via, uint flow)
{
	FlowStatMap::iterator origin_it = this->find(origin);
	if (origin_it == this->end()) {
		this->insert(FlowStat(origin, via, flow));
	} else {
		origin_it->ChangeShare(via, flow);
		assert(!origin_it->empty());
	}
}

/**
 * Pass on some flow, remembering it as invalid, for later subtraction from
 * locally consumed flow. This is necessary because we can't have negative
 * flows and we don't want to sort the flows before adding them up.
 * @param origin Origin of the flow.
 * @param via Next hop.
 * @param flow Amount of flow to be passed.
 */
void FlowStatMap::PassOnFlow(StationID origin, StationID via, uint flow)
{
	FlowStatMap::iterator prev_it = this->find(origin);
	if (prev_it == this->end()) {
		FlowStat fs(origin, via, flow);
		fs.AppendShare(INVALID_STATION, flow);
		this->insert(std::move(fs));
	} else {
		prev_it->ChangeShare(via, flow);
		prev_it->ChangeShare(INVALID_STATION, flow);
		assert(!prev_it->empty());
	}
}

/**
 * Subtract invalid flows from locally consumed flow.
 * @param self ID of own station.
 */
void FlowStatMap::FinalizeLocalConsumption(StationID self)
{
	for (FlowStat &fs : *this) {
		uint local = fs.GetShare(INVALID_STATION);
		if (local > INT_MAX) { // make sure it fits in an int
			fs.ChangeShare(self, -INT_MAX);
			fs.ChangeShare(INVALID_STATION, -INT_MAX);
			local -= INT_MAX;
		}
		fs.ChangeShare(self, -(int)local);
		fs.ChangeShare(INVALID_STATION, -(int)local);

		/* If the local share is used up there must be a share for some
		 * remote station. */
		assert(!fs.empty());
	}
}

/**
 * Delete all flows at a station for specific cargo and destination.
 * @param via Remote station of flows to be deleted.
 * @return IDs of source stations for which the complete FlowStat, not only a
 *         share, has been erased.
 */
StationIDStack FlowStatMap::DeleteFlows(StationID via)
{
	StationIDStack ret;
	for (FlowStatMap::iterator f_it = this->begin(); f_it != this->end();) {
		FlowStat &s_flows = *f_it;
		s_flows.ChangeShare(via, INT_MIN);
		if (s_flows.empty()) {
			ret.Push(f_it->GetOrigin());
			f_it = this->erase(f_it);
		} else {
			++f_it;
		}
	}
	return ret;
}

/**
 * Restrict all flows at a station for specific cargo and destination.
 * @param via Remote station of flows to be restricted.
 */
void FlowStatMap::RestrictFlows(StationID via)
{
	for (FlowStat &it : *this) {
		it.RestrictShare(via);
	}
}

/**
 * Get the sum of all flows from this FlowStatMap.
 * @return sum of all flows.
 */
uint FlowStatMap::GetFlow() const
{
	uint ret = 0;
	for (const FlowStat &it : this->IterateUnordered()) {
		if (it.IsInvalid()) continue;
		ret += (it.end() - 1)->first;
	}
	return ret;
}

/**
 * Get the sum of flows via a specific station from this FlowStatMap.
 * @param via Remote station to look for.
 * @return all flows for 'via' added up.
 */
uint FlowStatMap::GetFlowVia(StationID via) const
{
	uint ret = 0;
	for (const FlowStat &it : this->IterateUnordered()) {
		if (it.IsInvalid()) continue;
		ret += it.GetShare(via);
	}
	return ret;
}

/**
 * Get the sum of flows from a specific station from this FlowStatMap.
 * @param from Origin station to look for.
 * @return all flows from 'from' added up.
 */
uint FlowStatMap::GetFlowFrom(StationID from) const
{
	FlowStatMap::const_iterator i = this->find(from);
	if (i == this->end()) return 0;
	if (i->IsInvalid()) return 0;
	return (i->end() - 1)->first;
}

/**
 * Get the flow from a specific station via a specific other station.
 * @param from Origin station to look for.
 * @param via Remote station to look for.
 * @return flow share originating at 'from' and going to 'via'.
 */
uint FlowStatMap::GetFlowFromVia(StationID from, StationID via) const
{
	FlowStatMap::const_iterator i = this->find(from);
	if (i == this->end()) return 0;
	if (i->IsInvalid()) return 0;
	return i->GetShare(via);
}

void FlowStatMap::SortStorage()
{
	assert(this->flows_storage.size() == this->flows_index.size());
	std::sort(this->flows_storage.begin(), this->flows_storage.end(), [](const FlowStat &a, const FlowStat &b) -> bool {
		return a.origin < b.origin;
	});
	uint16_t index = 0;
	for (auto &it : this->flows_index) {
		it.second = index;
		index++;
	}
}

void DumpStationFlowStats(format_target &buffer)
{
	btree::btree_map<uint, uint> count_map;
	btree::btree_map<uint, uint> invalid_map;
	for (const Station *st : Station::Iterate()) {
		for (CargoType i = 0; i < NUM_CARGO; i++) {
			const GoodsEntry &ge = st->goods[i];
			if (ge.data == nullptr) continue;
			for (FlowStatMap::const_iterator it(ge.data->flows.begin()); it != ge.data->flows.end(); ++it) {
				count_map[(uint32_t)it->size()]++;
				invalid_map[it->GetRawFlags() & 0x1F]++;
			}
		}
	}
	buffer.append("Flow state shares size distribution:\n");
	for (const auto &it : count_map) {
		buffer.format("{:<5} {:<5}\n", it.first, it.second);
	}
	buffer.append("Flow state shares invalid state distribution:\n");
	for (const auto &it : invalid_map) {
		buffer.format("{:<2} {:<5}\n", it.first, it.second);
	}
}

extern const TileTypeProcs _tile_type_station_procs = {
	DrawTile_Station,           // draw_tile_proc
	GetSlopePixelZ_Station,     // get_slope_z_proc
	ClearTile_Station,          // clear_tile_proc
	nullptr,                       // add_accepted_cargo_proc
	GetTileDesc_Station,        // get_tile_desc_proc
	GetTileTrackStatus_Station, // get_tile_track_status_proc
	ClickTile_Station,          // click_tile_proc
	AnimateTile_Station,        // animate_tile_proc
	TileLoop_Station,           // tile_loop_proc
	ChangeTileOwner_Station,    // change_tile_owner_proc
	nullptr,                       // add_produced_cargo_proc
	VehicleEnter_Station,       // vehicle_enter_tile_proc
	GetFoundation_Station,      // get_foundation_proc
	TerraformTile_Station,      // terraform_tile_proc
};
