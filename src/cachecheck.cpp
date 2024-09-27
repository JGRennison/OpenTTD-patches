/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cachecheck.cpp Check caches. */

#include "stdafx.h"
#include "aircraft.h"
#include "command_func.h"
#include "company_base.h"
#include "crashlog.h"
#include "debug.h"
#include "debug_desync.h"
#include "debug_settings.h"
#include "industry.h"
#include "roadstop_base.h"
#include "roadveh.h"
#include "scope_info.h"
#include "ship.h"
#include "station_base.h"
#include "station_map.h"
#include "string_func_extra.h"
#include "subsidy_func.h"
#include "tbtr_template_vehicle.h"
#include "tbtr_template_vehicle_func.h"
#include "town.h"
#include "tracerestrict.h"
#include "train.h"
#include "tunnelbridge.h"
#include "vehicle_base.h"

#include "safeguards.h"

extern void AfterLoadCompanyStats();
extern void RebuildTownCaches(bool cargo_update_required, bool old_map_position);
extern void WriteVehicleInfo(char *&p, const char *last, const Vehicle *u, const Vehicle *v, uint length);

static bool SignalInfraTotalMatches()
{
	std::array<int, MAX_COMPANIES> old_signal_totals = {};
	for (const Company *c : Company::Iterate()) {
		old_signal_totals[c->index] = c->infrastructure.signal;
	}

	std::array<int, MAX_COMPANIES> new_signal_totals = {};
	for (TileIndex tile = 0; tile < MapSize(); tile++) {
		switch (GetTileType(tile)) {
			case MP_RAILWAY:
				if (HasSignals(tile)) {
					const Company *c = Company::GetIfValid(GetTileOwner(tile));
					if (c != nullptr) new_signal_totals[c->index] += CountBits(GetPresentSignals(tile));
				}
				break;

			case MP_TUNNELBRIDGE: {
				/* Only count the tunnel/bridge if we're on the northern end tile. */
				DiagDirection dir = GetTunnelBridgeDirection(tile);
				if (dir == DIAGDIR_NE || dir == DIAGDIR_NW) break;

				if (IsTunnelBridgeWithSignalSimulation(tile)) {
					const Company *c = Company::GetIfValid(GetTileOwner(tile));
					if (c != nullptr) new_signal_totals[c->index] += GetTunnelBridgeSignalSimulationSignalCount(tile, GetOtherTunnelBridgeEnd(tile));
				}
				break;
			}

			default:
				break;
		}
	}

	return old_signal_totals == new_signal_totals;
}

/**
 * Check the validity of some of the caches.
 * Especially in the sense of desyncs between
 * the cached value and what the value would
 * be when calculated from the 'base' data.
 */
void CheckCaches(bool force_check, std::function<void(const char *)> log, CheckCachesFlags flags)
{
	if (!force_check) {
		int desync_level = GetDebugLevel(DebugLevelID::desync);

		if (unlikely(HasChickenBit(DCBF_DESYNC_CHECK_PERIODIC)) && desync_level < 1) {
			desync_level = 1;
			if (HasChickenBit(DCBF_DESYNC_CHECK_NO_GENERAL)) flags &= ~CHECK_CACHE_GENERAL;
		}
		if (unlikely(HasChickenBit(DCBF_DESYNC_CHECK_PERIODIC_SIGNALS)) && desync_level < 2 && _state_ticks.base() % 256 == 0) {
			if (!SignalInfraTotalMatches()) desync_level = 2;
		}

		/* Return here so it is easy to add checks that are run
		 * always to aid testing of caches. */
		if (desync_level < 1) return;

		if (desync_level == 1 && _state_ticks.base() % 500 != 0) return;
	}

	SCOPE_INFO_FMT([flags], "CheckCaches: %X", flags);

	std::vector<std::string> saved_messages;
	std::function<void(const char *)> log_orig;
	if (flags & CHECK_CACHE_EMIT_LOG) {
		log_orig = std::move(log);
		log = [&saved_messages, &log_orig](const char *str) {
			if (log_orig) log_orig(str);
			saved_messages.emplace_back(str);
		};
	}

	char cclog_buffer[1024];
	auto cclog_common = [&]() {
		DEBUG(desync, 0, "%s", cclog_buffer);
		if (log) {
			log(cclog_buffer);
		} else {
			LogDesyncMsg(cclog_buffer);
		}
	};

#define CCLOG(...) { \
	seprintf(cclog_buffer, lastof(cclog_buffer), __VA_ARGS__); \
	cclog_common(); \
}

	auto output_veh_info = [&](char *&p, const Vehicle *u, const Vehicle *v, uint length) {
		WriteVehicleInfo(p, lastof(cclog_buffer), u, v, length);
	};
	auto output_veh_info_single = [&](char *&p, const Vehicle *v) {
		uint length = 0;
		for (const Vehicle *u = v->First(); u != v; u = u->Next()) {
			length++;
		}
		WriteVehicleInfo(p, lastof(cclog_buffer), v, v->First(), length);
	};

#define CCLOGV(...) { \
	char *p = cclog_buffer + seprintf(cclog_buffer, lastof(cclog_buffer), __VA_ARGS__); \
	output_veh_info(p, u, v, length); \
	cclog_common(); \
}

#define CCLOGV1(...) { \
	char *p = cclog_buffer + seprintf(cclog_buffer, lastof(cclog_buffer), __VA_ARGS__); \
	output_veh_info_single(p, v); \
	cclog_common(); \
}

	if (flags & CHECK_CACHE_GENERAL) {
		/* Check the town caches. */
		std::vector<TownCache> old_town_caches;
		std::vector<StationList> old_town_stations_nears;
		for (const Town *t : Town::Iterate()) {
			old_town_caches.push_back(t->cache);
			old_town_stations_nears.push_back(t->stations_near);
		}

		std::vector<IndustryList> old_station_industries_nears;
		std::vector<BitmapTileArea> old_station_catchment_tiles;
		std::vector<uint> old_station_tiles;
		for (Station *st : Station::Iterate()) {
			old_station_industries_nears.push_back(st->industries_near);
			old_station_catchment_tiles.push_back(st->catchment_tiles);
			old_station_tiles.push_back(st->station_tiles);
		}

		std::vector<StationList> old_industry_stations_nears;
		for (Industry *ind : Industry::Iterate()) {
			old_industry_stations_nears.push_back(ind->stations_near);
		}

		RebuildTownCaches(false, false);
		RebuildSubsidisedSourceAndDestinationCache();

		Station::RecomputeCatchmentForAll();

		uint i = 0;
		for (Town *t : Town::Iterate()) {
			if (old_town_caches[i].num_houses != t->cache.num_houses) {
				CCLOG("town cache num_houses mismatch: town %i, (old size: %u, new size: %u)", (int)t->index, old_town_caches[i].num_houses, t->cache.num_houses);
			}
			if (old_town_caches[i].population != t->cache.population) {
				CCLOG("town cache population mismatch: town %i, (old size: %u, new size: %u)", (int)t->index, old_town_caches[i].population, t->cache.population);
			}
			if (old_town_caches[i].part_of_subsidy != t->cache.part_of_subsidy) {
				CCLOG("town cache population mismatch: town %i, (old size: %u, new size: %u)", (int)t->index, old_town_caches[i].part_of_subsidy, t->cache.part_of_subsidy);
			}
			if (old_town_caches[i].squared_town_zone_radius != t->cache.squared_town_zone_radius) {
				CCLOG("town cache squared_town_zone_radius mismatch: town %i", (int)t->index);
			}
			if (old_town_caches[i].building_counts != t->cache.building_counts) {
				CCLOG("town cache building_counts mismatch: town %i", (int)t->index);
			}
			if (old_town_stations_nears[i] != t->stations_near) {
				CCLOG("town stations_near mismatch: town %i, (old size: %u, new size: %u)", (int)t->index, (uint)old_town_stations_nears[i].size(), (uint)t->stations_near.size());
			}
			i++;
		}
		i = 0;
		for (Station *st : Station::Iterate()) {
			if (old_station_industries_nears[i] != st->industries_near) {
				CCLOG("station industries_near mismatch: st %i, (old size: %u, new size: %u)", (int)st->index, (uint)old_station_industries_nears[i].size(), (uint)st->industries_near.size());
			}
			if (!(old_station_catchment_tiles[i] == st->catchment_tiles)) {
				CCLOG("station catchment_tiles mismatch: st %i", (int)st->index);
			}
			if (!(old_station_tiles[i] == st->station_tiles)) {
				CCLOG("station station_tiles mismatch: st %i, (old: %u, new: %u)", (int)st->index, old_station_tiles[i], st->station_tiles);
			}
			i++;
		}
		i = 0;
		for (Industry *ind : Industry::Iterate()) {
			if (old_industry_stations_nears[i] != ind->stations_near) {
				CCLOG("industry stations_near mismatch: ind %i, (old size: %u, new size: %u)", (int)ind->index, (uint)old_industry_stations_nears[i].size(), (uint)ind->stations_near.size());
			}
			StationList stlist;
			if (ind->neutral_station != nullptr && !_settings_game.station.serve_neutral_industries) {
				stlist.insert(ind->neutral_station);
				if (ind->stations_near != stlist) {
					CCLOG("industry neutral station stations_near mismatch: ind %i, (recalc size: %u, neutral size: %u)", (int)ind->index, (uint)ind->stations_near.size(), (uint)stlist.size());
				}
			} else {
				ForAllStationsAroundTiles(ind->location, [ind, &stlist](Station *st, TileIndex tile) {
					if (!IsTileType(tile, MP_INDUSTRY) || GetIndustryIndex(tile) != ind->index) return false;
					stlist.insert(st);
					return true;
				});
				if (ind->stations_near != stlist) {
					CCLOG("industry FindStationsAroundTiles mismatch: ind %i, (recalc size: %u, find size: %u)", (int)ind->index, (uint)ind->stations_near.size(), (uint)stlist.size());
				}
			}
			i++;
		}
	}

	if (flags & CHECK_CACHE_INFRA_TOTALS) {
		/* Check company infrastructure cache. */
		std::vector<CompanyInfrastructure> old_infrastructure;
		for (const Company *c : Company::Iterate()) old_infrastructure.push_back(c->infrastructure);

		AfterLoadCompanyStats();

		uint i = 0;
		for (const Company *c : Company::Iterate()) {
			if (old_infrastructure[i] != c->infrastructure) {
				CCLOG("infrastructure cache mismatch: company %i", (int)c->index);
				char buffer[4096];
				old_infrastructure[i].Dump(buffer, lastof(buffer));
				CCLOG("Previous:");
				ProcessLineByLine(buffer, [&](const char *line) {
					CCLOG("  %s", line);
				});
				c->infrastructure.Dump(buffer, lastof(buffer));
				CCLOG("Recalculated:");
				ProcessLineByLine(buffer, [&](const char *line) {
					CCLOG("  %s", line);
				});
				if (old_infrastructure[i].signal != c->infrastructure.signal && _network_server && !HasChickenBit(DCBF_DESYNC_CHECK_PERIODIC_SIGNALS)) {
					DoCommandP(0, 0, _settings_game.debug.chicken_bits | (1 << DCBF_DESYNC_CHECK_PERIODIC_SIGNALS), CMD_CHANGE_SETTING, nullptr, "debug.chicken_bits");
				}
			}
			i++;
		}
	}

	if (flags & CHECK_CACHE_GENERAL) {
		/* Strict checking of the road stop cache entries */
		for (const RoadStop *rs : RoadStop::Iterate()) {
			if (IsBayRoadStopTile(rs->xy)) continue;

			assert(rs->GetEntry(DIAGDIR_NE) != rs->GetEntry(DIAGDIR_NW));
			rs->GetEntry(DIAGDIR_NE)->CheckIntegrity(rs);
			rs->GetEntry(DIAGDIR_NW)->CheckIntegrity(rs);
		}

		std::vector<NewGRFCache> grf_cache;
		std::vector<VehicleCache> veh_cache;
		std::vector<GroundVehicleCache> gro_cache;
		std::vector<TrainCache> tra_cache;
		std::vector<AircraftCache> air_cache;
		std::vector<std::unique_ptr<Vehicle, FreeDeleter>> veh_old;

		for (Vehicle *v : Vehicle::Iterate()) {
			extern bool ValidateVehicleTileHash(const Vehicle *v);
			if (!ValidateVehicleTileHash(v)) {
				CCLOG("vehicle tile hash mismatch: type %i, vehicle %i, company %i, unit number %i", (int)v->type, v->index, (int)v->owner, v->unitnumber);
			}

			extern void FillNewGRFVehicleCache(const Vehicle *v);
			if (v != v->First() || v->vehstatus & VS_CRASHED || !v->IsPrimaryVehicle()) continue;

			uint length = 0;
			for (const Vehicle *u = v; u != nullptr; u = u->Next(), length++) {
				if (u->IsGroundVehicle() && (HasBit(u->GetGroundVehicleFlags(), GVF_GOINGUP_BIT) || HasBit(u->GetGroundVehicleFlags(), GVF_GOINGDOWN_BIT)) && u->GetGroundVehicleCache()->cached_slope_resistance && HasBit(v->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST)) {
					CCLOGV("VCF_GV_ZERO_SLOPE_RESIST set incorrectly (1)");
				}
				if (u->type == VEH_TRAIN && u->breakdown_ctr != 0 && !HasBit(Train::From(v)->flags, VRF_CONSIST_BREAKDOWN) && (Train::From(u)->IsEngine() || Train::From(u)->IsMultiheaded())) {
					CCLOGV("VRF_CONSIST_BREAKDOWN incorrectly not set");
				}
				if (u->type == VEH_TRAIN && ((Train::From(u)->track & TRACK_BIT_WORMHOLE && !(Train::From(u)->vehstatus & VS_HIDDEN)) || Train::From(u)->track == TRACK_BIT_DEPOT) && !HasBit(Train::From(v)->flags, VRF_CONSIST_SPEED_REDUCTION)) {
					CCLOGV("VRF_CONSIST_SPEED_REDUCTION incorrectly not set");
				}
			}

			for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
				FillNewGRFVehicleCache(u);
				grf_cache.push_back(u->grf_cache);
				veh_cache.push_back(u->vcache);
				switch (u->type) {
					case VEH_TRAIN:
						gro_cache.push_back(Train::From(u)->gcache);
						tra_cache.push_back(Train::From(u)->tcache);
						veh_old.emplace_back(CallocT<Train>(1));
						memcpy((void *) veh_old.back().get(), (const void *) Train::From(u), sizeof(Train));
						break;
					case VEH_ROAD:
						gro_cache.push_back(RoadVehicle::From(u)->gcache);
						veh_old.emplace_back(CallocT<RoadVehicle>(1));
						memcpy((void *) veh_old.back().get(), (const void *) RoadVehicle::From(u), sizeof(RoadVehicle));
						break;
					case VEH_AIRCRAFT:
						air_cache.push_back(Aircraft::From(u)->acache);
						veh_old.emplace_back(CallocT<Aircraft>(1));
						memcpy((void *) veh_old.back().get(), (const void *) Aircraft::From(u), sizeof(Aircraft));
						break;
					default:
						veh_old.emplace_back(CallocT<Vehicle>(1));
						memcpy((void *) veh_old.back().get(), (const void *) u, sizeof(Vehicle));
						break;
				}
			}

			switch (v->type) {
				case VEH_TRAIN:    Train::From(v)->ConsistChanged(CCF_TRACK); break;
				case VEH_ROAD:     RoadVehUpdateCache(RoadVehicle::From(v)); break;
				case VEH_AIRCRAFT: UpdateAircraftCache(Aircraft::From(v));   break;
				case VEH_SHIP:     Ship::From(v)->UpdateCache();             break;
				default: break;
			}

			length = 0;
			for (const Vehicle *u = v; u != nullptr; u = u->Next(), length++) {
				FillNewGRFVehicleCache(u);
				if (grf_cache[length] != u->grf_cache) {
					CCLOGV("newgrf cache mismatch");
				}
				if (veh_cache[length].cached_max_speed != u->vcache.cached_max_speed || veh_cache[length].cached_cargo_age_period != u->vcache.cached_cargo_age_period ||
						veh_cache[length].cached_vis_effect != u->vcache.cached_vis_effect || HasBit(veh_cache[length].cached_veh_flags ^ u->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT)) {
					CCLOGV("vehicle cache mismatch: %c%c%c%c",
							veh_cache[length].cached_max_speed != u->vcache.cached_max_speed ? 'm' : '-',
							veh_cache[length].cached_cargo_age_period != u->vcache.cached_cargo_age_period ? 'c' : '-',
							veh_cache[length].cached_vis_effect != u->vcache.cached_vis_effect ? 'v' : '-',
							HasBit(veh_cache[length].cached_veh_flags ^ u->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT) ? 'l' : '-');
				}
				if (u->IsGroundVehicle() && (HasBit(u->GetGroundVehicleFlags(), GVF_GOINGUP_BIT) || HasBit(u->GetGroundVehicleFlags(), GVF_GOINGDOWN_BIT)) && u->GetGroundVehicleCache()->cached_slope_resistance && HasBit(v->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST)) {
					CCLOGV("VCF_GV_ZERO_SLOPE_RESIST set incorrectly (2)");
				}
				if (veh_old[length]->acceleration != u->acceleration) {
					CCLOGV("acceleration mismatch");
				}
				if (veh_old[length]->breakdown_chance != u->breakdown_chance) {
					CCLOGV("breakdown_chance mismatch");
				}
				if (veh_old[length]->breakdown_ctr != u->breakdown_ctr) {
					CCLOGV("breakdown_ctr mismatch");
				}
				if (veh_old[length]->breakdown_delay != u->breakdown_delay) {
					CCLOGV("breakdown_delay mismatch");
				}
				if (veh_old[length]->breakdowns_since_last_service != u->breakdowns_since_last_service) {
					CCLOGV("breakdowns_since_last_service mismatch");
				}
				if (veh_old[length]->breakdown_severity != u->breakdown_severity) {
					CCLOGV("breakdown_severity mismatch");
				}
				if (veh_old[length]->breakdown_type != u->breakdown_type) {
					CCLOGV("breakdown_type mismatch");
				}
				if (veh_old[length]->vehicle_flags != u->vehicle_flags) {
					CCLOGV("vehicle_flags mismatch");
				}
				auto print_gv_cache_diff = [&](const char *vtype, const GroundVehicleCache &a, const GroundVehicleCache &b) {
					CCLOGV("%s ground vehicle cache mismatch: %c%c%c%c%c%c%c%c%c%c",
							vtype,
							a.cached_weight != b.cached_weight ? 'w' : '-',
							a.cached_slope_resistance != b.cached_slope_resistance ? 'r' : '-',
							a.cached_max_te != b.cached_max_te ? 't' : '-',
							a.cached_axle_resistance != b.cached_axle_resistance ? 'a' : '-',
							a.cached_max_track_speed != b.cached_max_track_speed ? 's' : '-',
							a.cached_power != b.cached_power ? 'p' : '-',
							a.cached_air_drag != b.cached_air_drag ? 'd' : '-',
							a.cached_total_length != b.cached_total_length ? 'l' : '-',
							a.first_engine != b.first_engine ? 'e' : '-',
							a.cached_veh_length != b.cached_veh_length ? 'L' : '-');
				};
				switch (u->type) {
					case VEH_TRAIN:
						if (gro_cache[length] != Train::From(u)->gcache) {
							print_gv_cache_diff("train", gro_cache[length], Train::From(u)->gcache);
						}
						if (tra_cache[length] != Train::From(u)->tcache) {
							CCLOGV("train cache mismatch: %c%c%c%c%c%c%c%c%c%c%c",
									tra_cache[length].cached_override != Train::From(u)->tcache.cached_override ? 'o' : '-',
									tra_cache[length].cached_curve_speed_mod != Train::From(u)->tcache.cached_curve_speed_mod ? 'C' : '-',
									tra_cache[length].cached_tflags != Train::From(u)->tcache.cached_tflags ? 'f' : '-',
									tra_cache[length].cached_num_engines != Train::From(u)->tcache.cached_num_engines ? 'e' : '-',
									tra_cache[length].cached_centre_mass != Train::From(u)->tcache.cached_centre_mass ? 'm' : '-',
									tra_cache[length].cached_braking_length != Train::From(u)->tcache.cached_braking_length ? 'b' : '-',
									tra_cache[length].cached_veh_weight != Train::From(u)->tcache.cached_veh_weight ? 'w' : '-',
									tra_cache[length].cached_uncapped_decel != Train::From(u)->tcache.cached_uncapped_decel ? 'D' : '-',
									tra_cache[length].cached_deceleration != Train::From(u)->tcache.cached_deceleration ? 'd' : '-',
									tra_cache[length].user_def_data != Train::From(u)->tcache.user_def_data ? 'u' : '-',
									tra_cache[length].cached_max_curve_speed != Train::From(u)->tcache.cached_max_curve_speed ? 'c' : '-');
						}
						if (Train::From(veh_old[length].get())->railtype != Train::From(u)->railtype) {
							CCLOGV("railtype mismatch");
						}
						if (Train::From(veh_old[length].get())->compatible_railtypes != Train::From(u)->compatible_railtypes) {
							CCLOGV("compatible_railtypes mismatch");
						}
						if (Train::From(veh_old[length].get())->flags != Train::From(u)->flags) {
							CCLOGV("train flags mismatch");
						}
						break;
					case VEH_ROAD:
						if (gro_cache[length] != RoadVehicle::From(u)->gcache) {
							print_gv_cache_diff("road vehicle", gro_cache[length], Train::From(u)->gcache);
						}
						break;
					case VEH_AIRCRAFT:
						if (air_cache[length] != Aircraft::From(u)->acache) {
							CCLOGV("Aircraft vehicle cache mismatch: %c%c",
									air_cache[length].cached_max_range != Aircraft::From(u)->acache.cached_max_range ? 'r' : '-',
									air_cache[length].cached_max_range_sqr != Aircraft::From(u)->acache.cached_max_range_sqr ? 's' : '-');
						}
						break;
					default:
						break;
				}
			}

			grf_cache.clear();
			veh_cache.clear();
			gro_cache.clear();
			air_cache.clear();
			tra_cache.clear();
			veh_old.clear();
		}

		/* Check whether the caches are still valid */
		for (Vehicle *v : Vehicle::Iterate()) {
			Money old_feeder_share = v->cargo.GetFeederShare();
			uint old_count = v->cargo.TotalCount();
			uint64_t old_cargo_periods_in_transit = v->cargo.CargoPeriodsInTransit();

			v->cargo.InvalidateCache();

			uint changed = 0;
			if (v->cargo.GetFeederShare() != old_feeder_share) SetBit(changed, 0);
			if (v->cargo.TotalCount() != old_count) SetBit(changed, 1);
			if (v->cargo.CargoPeriodsInTransit() != old_cargo_periods_in_transit) SetBit(changed, 2);
			if (changed != 0) {
				CCLOGV1("vehicle cargo cache mismatch: %c%c%c",
						HasBit(changed, 0) ? 'f' : '-',
						HasBit(changed, 1) ? 't' : '-',
						HasBit(changed, 2) ? 'p' : '-');
			}
		}

		for (Station *st : Station::Iterate()) {
			for (CargoID c = 0; c < NUM_CARGO; c++) {
				if (st->goods[c].data == nullptr) continue;

				uint old_count = st->goods[c].data->cargo.TotalCount();
				uint64_t old_cargo_periods_in_transit = st->goods[c].data->cargo.CargoPeriodsInTransit();

				st->goods[c].data->cargo.InvalidateCache();

				uint changed = 0;
				if (st->goods[c].data->cargo.TotalCount() != old_count) SetBit(changed, 0);
				if (st->goods[c].data->cargo.CargoPeriodsInTransit() != old_cargo_periods_in_transit) SetBit(changed, 1);
				if (changed != 0) {
					CCLOG("station cargo cache mismatch: station %i, company %i, cargo %u: %c%c",
							st->index, (int)st->owner, c,
							HasBit(changed, 0) ? 't' : '-',
							HasBit(changed, 1) ? 'd' : '-');
				}
			}

			/* Check docking tiles */
			TileArea ta;
			btree::btree_set<TileIndex> docking_tiles;
			for (TileIndex tile : st->docking_station) {
				ta.Add(tile);
				if (IsDockingTile(tile)) docking_tiles.insert(tile);
			}
			UpdateStationDockingTiles(st);
			if (ta.tile != st->docking_station.tile || ta.w != st->docking_station.w || ta.h != st->docking_station.h) {
				CCLOG("station docking mismatch: station %i, company %i, prev: (%X, %u, %u), recalc: (%X, %u, %u)",
						st->index, (int)st->owner, ta.tile, ta.w, ta.h, st->docking_station.tile, st->docking_station.w, st->docking_station.h);
			}
			for (TileIndex tile : ta) {
				if ((docking_tiles.find(tile) != docking_tiles.end()) != IsDockingTile(tile)) {
					CCLOG("docking tile mismatch: tile %i", (int)tile);
				}
			}
		}

#ifdef WITH_ASSERT
		for (OrderList *order_list : OrderList::Iterate()) {
			order_list->DebugCheckSanity();
		}
#endif

		extern void ValidateVehicleTickCaches();
		ValidateVehicleTickCaches();

		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->Previous()) assert_msg(v->Previous()->Next() == v, "%u", v->index);
			if (v->Next()) assert_msg(v->Next()->Previous() == v, "%u", v->index);
		}
		for (const TemplateVehicle *tv : TemplateVehicle::Iterate()) {
			if (tv->Prev()) assert_msg(tv->Prev()->Next() == tv, "%u", tv->index);
			if (tv->Next()) assert_msg(tv->Next()->Prev() == tv, "%u", tv->index);
		}

		{
			extern std::string ValidateTemplateReplacementCaches();
			std::string template_validation_result = ValidateTemplateReplacementCaches();
			if (!template_validation_result.empty()) {
				CCLOG("Template replacement cache validation failed: %s", template_validation_result.c_str());
			}
		}

		if (!TraceRestrictSlot::ValidateVehicleIndex()) CCLOG("Trace restrict slot vehicle index validation failed");
		TraceRestrictSlot::ValidateSlotOccupants(log);

		if (!CargoPacket::ValidateDeferredCargoPayments()) CCLOG("Cargo packets deferred payments validation failed");

		if (_order_destination_refcount_map_valid) {
			btree::btree_map<uint32_t, uint32_t> saved_order_destination_refcount_map = std::move(_order_destination_refcount_map);
			for (auto iter = saved_order_destination_refcount_map.begin(); iter != saved_order_destination_refcount_map.end();) {
				if (iter->second == 0) {
					iter = saved_order_destination_refcount_map.erase(iter);
				} else {
					++iter;
				}
			}
			IntialiseOrderDestinationRefcountMap();
			if (saved_order_destination_refcount_map != _order_destination_refcount_map) CCLOG("Order destination refcount map mismatch");
		} else {
			CCLOG("Order destination refcount map not valid");
		}
	}

	if (flags & CHECK_CACHE_WATER_REGIONS) {
		extern void WaterRegionCheckCaches(std::function<void(const char *)> log);
		WaterRegionCheckCaches(log);
	}

	if ((flags & CHECK_CACHE_EMIT_LOG) && !saved_messages.empty()) {
		InconsistencyExtraInfo info;
		info.check_caches_result = std::move(saved_messages);
		CrashLog::InconsistencyLog(info);
		for (std::string &str : info.check_caches_result) {
			LogDesyncMsg(std::move(str));
		}
	}

#undef CCLOG
#undef CCLOGV
#undef CCLOGV1
}

/**
 * Network-safe forced desync check.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 unused
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDesyncCheck(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	if (flags & DC_EXEC) {
		CheckCaches(true, nullptr, CHECK_CACHE_ALL | CHECK_CACHE_EMIT_LOG);
	}

	return CommandCost();
}
