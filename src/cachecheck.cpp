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
#include "date_func.h"
#include "debug.h"
#include "debug_desync.h"
#include "debug_settings.h"
#include "industry.h"
#include "roadstop_base.h"
#include "roadveh.h"
#include "scope_info.h"
#include "settings_cmd.h"
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
extern void RebuildTownCaches(bool cargo_update_required);
extern void WriteVehicleInfo(format_target &buffer, const Vehicle *u, const Vehicle *v, uint length);

static bool SignalInfraTotalMatches()
{
	std::array<int, MAX_COMPANIES> old_signal_totals = {};
	for (const Company *c : Company::Iterate()) {
		old_signal_totals[c->index] = c->infrastructure.signal;
	}

	std::array<int, MAX_COMPANIES> new_signal_totals = {};
	for (TileIndex tile(0); tile < Map::Size(); ++tile) {
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
void CheckCaches(bool force_check, std::function<void(std::string_view)> log, CheckCachesFlags flags)
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

	SCOPE_INFO_FMT([flags], "CheckCaches: {:X}", flags);

	std::vector<std::string> saved_messages;
	std::function<void(std::string_view)> log_orig;
	if (flags & CHECK_CACHE_EMIT_LOG) {
		log_orig = std::move(log);
		log = [&saved_messages, &log_orig](std::string_view str) {
			if (log_orig) log_orig(str);
			saved_messages.emplace_back(str);
		};
	}

	format_buffer cc_buffer;

	auto cclog_output = [&](std::string_view str) {
		debug_print(DebugLevelID::desync, 0, str);
		if (log) {
			log(str);
		} else {
			LogDesyncMsg(std::string{str});
		}
	};

	auto cclog_start = [&]<typename... T>(fmt::format_string<T...> fmtstr, T&&... args) {
		cc_buffer.clear();
		cc_buffer.format(fmtstr, std::forward<T>(args)...);
	};

	auto cclog = [&]<typename... T>(fmt::format_string<T...> fmtstr, T&&... args) {
		cclog_start(fmtstr, std::forward<T>(args)...);
		cclog_output(cc_buffer);
	};

	auto output_veh_info = [&](const Vehicle *u, const Vehicle *v, uint length) {
		WriteVehicleInfo(cc_buffer, u, v, length);
	};
	auto output_veh_info_single = [&](const Vehicle *v) {
		uint length = 0;
		for (const Vehicle *u = v->First(); u != v; u = u->Next()) {
			length++;
		}
		WriteVehicleInfo(cc_buffer, v, v->First(), length);
	};

#define CCLOGV(...) { \
	cclog_start(__VA_ARGS__); \
	output_veh_info(u, v, length); \
	cclog_output(cc_buffer); \
}

#define CCLOGV1(...) { \
	cclog_start(__VA_ARGS__); \
	output_veh_info_single(v); \
	cclog_output(cc_buffer); \
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

		RebuildTownCaches(false);
		RebuildSubsidisedSourceAndDestinationCache();

		Station::RecomputeCatchmentForAll();

		uint i = 0;
		for (Town *t : Town::Iterate()) {
			if (old_town_caches[i].num_houses != t->cache.num_houses) {
				cclog("town cache num_houses mismatch: town {}, (old size: {}, new size: {})", t->index, old_town_caches[i].num_houses, t->cache.num_houses);
			}
			if (old_town_caches[i].population != t->cache.population) {
				cclog("town cache population mismatch: town {}i, (old size: {}, new size: {})", t->index, old_town_caches[i].population, t->cache.population);
			}
			if (old_town_caches[i].part_of_subsidy != t->cache.part_of_subsidy) {
				cclog("town cache population mismatch: town {}, (old size: {}, new size: {})", t->index, old_town_caches[i].part_of_subsidy, t->cache.part_of_subsidy);
			}
			if (old_town_caches[i].squared_town_zone_radius != t->cache.squared_town_zone_radius) {
				cclog("town cache squared_town_zone_radius mismatch: town {}", t->index);
			}
			if (old_town_caches[i].building_counts != t->cache.building_counts) {
				cclog("town cache building_counts mismatch: town {}", t->index);
			}
			if (old_town_stations_nears[i] != t->stations_near) {
				cclog("town stations_near mismatch: town {}, (old size: {}, new size: {})", t->index, old_town_stations_nears[i].size(), t->stations_near.size());
			}
			i++;
		}
		i = 0;
		for (Station *st : Station::Iterate()) {
			if (old_station_industries_nears[i] != st->industries_near) {
				cclog("station industries_near mismatch: st {}, (old size: {}, new size: {})", (int)st->index, (uint)old_station_industries_nears[i].size(), (uint)st->industries_near.size());
			}
			if (!(old_station_catchment_tiles[i] == st->catchment_tiles)) {
				cclog("station catchment_tiles mismatch: st {}", (int)st->index);
			}
			if (!(old_station_tiles[i] == st->station_tiles)) {
				cclog("station station_tiles mismatch: st {}, (old: {}, new: {})", (int)st->index, old_station_tiles[i], st->station_tiles);
			}
			i++;
		}
		i = 0;
		for (Industry *ind : Industry::Iterate()) {
			if (old_industry_stations_nears[i] != ind->stations_near) {
				cclog("industry stations_near mismatch: ind {}, (old size: {}, new size: {})", (int)ind->index, (uint)old_industry_stations_nears[i].size(), (uint)ind->stations_near.size());
			}
			StationList stlist;
			if (ind->neutral_station != nullptr && !_settings_game.station.serve_neutral_industries) {
				stlist.insert(ind->neutral_station);
				if (ind->stations_near != stlist) {
					cclog("industry neutral station stations_near mismatch: ind {}, (recalc size: {}, neutral size: {})", (int)ind->index, (uint)ind->stations_near.size(), (uint)stlist.size());
				}
			} else {
				ForAllStationsAroundTiles(ind->location, [ind, &stlist](Station *st, TileIndex tile) {
					if (!IsTileType(tile, MP_INDUSTRY) || GetIndustryIndex(tile) != ind->index) return false;
					stlist.insert(st);
					return true;
				});
				if (ind->stations_near != stlist) {
					cclog("industry FindStationsAroundTiles mismatch: ind {}, (recalc size: {}, find size: {})", (int)ind->index, (uint)ind->stations_near.size(), (uint)stlist.size());
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
				cclog("infrastructure cache mismatch: company {}", (int)c->index);
				format_buffer infra_buffer;
				old_infrastructure[i].Dump(infra_buffer);
				cclog("Previous:");
				ProcessLineByLine(infra_buffer, [&](std::string_view line) {
					cclog("  {}", line);
				});
				infra_buffer.clear();
				c->infrastructure.Dump(infra_buffer);
				cclog("Recalculated:");
				ProcessLineByLine(infra_buffer, [&](std::string_view line) {
					cclog("  {}", line);
				});
				if (old_infrastructure[i].signal != c->infrastructure.signal && _network_server && !HasChickenBit(DCBF_DESYNC_CHECK_PERIODIC_SIGNALS)) {
					Command<CMD_CHANGE_SETTING>::Post("debug.chicken_bits", _settings_game.debug.chicken_bits | (1 << DCBF_DESYNC_CHECK_PERIODIC_SIGNALS));
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

		struct SavedVehicleInfo {
			NewGRFCache grf_cache;
			VehicleCache vcache;
			uint8_t acceleration;
			uint8_t breakdown_ctr;
			uint8_t breakdown_delay;
			uint8_t breakdowns_since_last_service;
			uint8_t breakdown_chance;
			uint8_t breakdown_severity;
			uint8_t breakdown_type;
			uint32_t vehicle_flags;

			SavedVehicleInfo(const Vehicle *v) :
					grf_cache(v->grf_cache), vcache(v->vcache), acceleration(v->acceleration), breakdown_ctr(v->breakdown_ctr),
					breakdown_delay(v->breakdown_delay), breakdowns_since_last_service(v->breakdowns_since_last_service),
					breakdown_chance(v->breakdown_chance), breakdown_severity(v->breakdown_severity), breakdown_type(v->breakdown_type),
					vehicle_flags(v->vehicle_flags) {}
		};
		std::vector<SavedVehicleInfo> veh_old;

		struct SavedTrainInfo {
			TrainCache tcache;
			RailType railtype;
			RailTypes compatible_railtypes;
			uint32_t flags;
			SavedTrainInfo(const Train *t) : tcache(t->tcache), railtype(t->railtype), compatible_railtypes(t->compatible_railtypes), flags(t->flags) {}
		};
		std::vector<SavedTrainInfo> train_old;

		std::vector<GroundVehicleCache> gro_cache;
		std::vector<AircraftCache> air_cache;

		for (Vehicle *v : Vehicle::Iterate()) {
			extern bool ValidateVehicleTileHash(const Vehicle *v);
			if (!ValidateVehicleTileHash(v)) {
				cclog("vehicle tile hash mismatch: type {}, vehicle {}, company {}, unit number {}", (int)v->type, v->index, (int)v->owner, v->unitnumber);
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
				veh_old.emplace_back(u);
				switch (u->type) {
					case VEH_TRAIN:
						gro_cache.push_back(Train::From(u)->gcache);
						train_old.emplace_back(Train::From(u));
						break;
					case VEH_ROAD:
						gro_cache.push_back(RoadVehicle::From(u)->gcache);
						break;
					case VEH_AIRCRAFT:
						air_cache.push_back(Aircraft::From(u)->acache);
						break;
					default:
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
				const SavedVehicleInfo &oldv = veh_old[length];
				if (oldv.grf_cache != u->grf_cache) {
					CCLOGV("newgrf cache mismatch");
				}
				if (oldv.vcache.cached_max_speed != u->vcache.cached_max_speed || oldv.vcache.cached_cargo_age_period != u->vcache.cached_cargo_age_period ||
						oldv.vcache.cached_vis_effect != u->vcache.cached_vis_effect || HasBit(oldv.vcache.cached_veh_flags ^ u->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT)) {
					CCLOGV("vehicle cache mismatch: {}{}{}{}",
							oldv.vcache.cached_max_speed != u->vcache.cached_max_speed ? 'm' : '-',
							oldv.vcache.cached_cargo_age_period != u->vcache.cached_cargo_age_period ? 'c' : '-',
							oldv.vcache.cached_vis_effect != u->vcache.cached_vis_effect ? 'v' : '-',
							HasBit(oldv.vcache.cached_veh_flags ^ u->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT) ? 'l' : '-');
				}
				if (u->IsGroundVehicle() && (HasBit(u->GetGroundVehicleFlags(), GVF_GOINGUP_BIT) || HasBit(u->GetGroundVehicleFlags(), GVF_GOINGDOWN_BIT)) && u->GetGroundVehicleCache()->cached_slope_resistance && HasBit(v->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST)) {
					CCLOGV("VCF_GV_ZERO_SLOPE_RESIST set incorrectly (2)");
				}
				if (oldv.acceleration != u->acceleration) {
					CCLOGV("acceleration mismatch");
				}
				if (oldv.breakdown_chance != u->breakdown_chance) {
					CCLOGV("breakdown_chance mismatch");
				}
				if (oldv.breakdown_ctr != u->breakdown_ctr) {
					CCLOGV("breakdown_ctr mismatch");
				}
				if (oldv.breakdown_delay != u->breakdown_delay) {
					CCLOGV("breakdown_delay mismatch");
				}
				if (oldv.breakdowns_since_last_service != u->breakdowns_since_last_service) {
					CCLOGV("breakdowns_since_last_service mismatch");
				}
				if (oldv.breakdown_severity != u->breakdown_severity) {
					CCLOGV("breakdown_severity mismatch");
				}
				if (oldv.breakdown_type != u->breakdown_type) {
					CCLOGV("breakdown_type mismatch");
				}
				if (oldv.vehicle_flags != u->vehicle_flags) {
					CCLOGV("vehicle_flags mismatch");
				}
				auto print_gv_cache_diff = [&](const char *vtype, const GroundVehicleCache &a, const GroundVehicleCache &b) {
					CCLOGV("{} ground vehicle cache mismatch: {}{}{}{}{}{}{}{}{}{}",
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
					case VEH_TRAIN: {
						if (gro_cache[length] != Train::From(u)->gcache) {
							print_gv_cache_diff("train", gro_cache[length], Train::From(u)->gcache);
						}
						const SavedTrainInfo &oldt = train_old[length];
						if (oldt.tcache != Train::From(u)->tcache) {
							CCLOGV("train cache mismatch: {}{}{}{}{}{}{}{}{}{}{}",
									oldt.tcache.cached_override != Train::From(u)->tcache.cached_override ? 'o' : '-',
									oldt.tcache.cached_curve_speed_mod != Train::From(u)->tcache.cached_curve_speed_mod ? 'C' : '-',
									oldt.tcache.cached_tflags != Train::From(u)->tcache.cached_tflags ? 'f' : '-',
									oldt.tcache.cached_num_engines != Train::From(u)->tcache.cached_num_engines ? 'e' : '-',
									oldt.tcache.cached_centre_mass != Train::From(u)->tcache.cached_centre_mass ? 'm' : '-',
									oldt.tcache.cached_braking_length != Train::From(u)->tcache.cached_braking_length ? 'b' : '-',
									oldt.tcache.cached_veh_weight != Train::From(u)->tcache.cached_veh_weight ? 'w' : '-',
									oldt.tcache.cached_uncapped_decel != Train::From(u)->tcache.cached_uncapped_decel ? 'D' : '-',
									oldt.tcache.cached_deceleration != Train::From(u)->tcache.cached_deceleration ? 'd' : '-',
									oldt.tcache.user_def_data != Train::From(u)->tcache.user_def_data ? 'u' : '-',
									oldt.tcache.cached_max_curve_speed != Train::From(u)->tcache.cached_max_curve_speed ? 'c' : '-');
						}
						if (oldt.railtype != Train::From(u)->railtype) {
							CCLOGV("railtype mismatch");
						}
						if (oldt.compatible_railtypes != Train::From(u)->compatible_railtypes) {
							CCLOGV("compatible_railtypes mismatch");
						}
						if (oldt.flags != Train::From(u)->flags) {
							CCLOGV("train flags mismatch");
						}
						break;
					}

					case VEH_ROAD: {
						if (gro_cache[length] != RoadVehicle::From(u)->gcache) {
							print_gv_cache_diff("road vehicle", gro_cache[length], Train::From(u)->gcache);
						}
						break;
					}

					case VEH_AIRCRAFT: {
						if (air_cache[length] != Aircraft::From(u)->acache) {
							CCLOGV("Aircraft vehicle cache mismatch: {}{}",
									air_cache[length].cached_max_range != Aircraft::From(u)->acache.cached_max_range ? 'r' : '-',
									air_cache[length].cached_max_range_sqr != Aircraft::From(u)->acache.cached_max_range_sqr ? 's' : '-');
						}
						break;
					}

					default:
						break;
				}
			}

			veh_old.clear();
			train_old.clear();
			gro_cache.clear();
			air_cache.clear();
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
				CCLOGV1("vehicle cargo cache mismatch: {}{}{}",
						HasBit(changed, 0) ? 'f' : '-',
						HasBit(changed, 1) ? 't' : '-',
						HasBit(changed, 2) ? 'p' : '-');
			}
		}

		for (Station *st : Station::Iterate()) {
			for (CargoType c = 0; c < NUM_CARGO; c++) {
				if (st->goods[c].data == nullptr) continue;

				uint old_count = st->goods[c].data->cargo.TotalCount();
				uint64_t old_cargo_periods_in_transit = st->goods[c].data->cargo.CargoPeriodsInTransit();

				st->goods[c].data->cargo.InvalidateCache();

				uint changed = 0;
				if (st->goods[c].data->cargo.TotalCount() != old_count) SetBit(changed, 0);
				if (st->goods[c].data->cargo.CargoPeriodsInTransit() != old_cargo_periods_in_transit) SetBit(changed, 1);
				if (changed != 0) {
					cclog("station cargo cache mismatch: station {}, company {}, cargo {}: {}{}",
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
				cclog("station docking mismatch: station {}, company {}, prev: ({:X}, {}, {}), recalc: ({:X}, {}, {})",
						st->index, (int)st->owner, ta.tile, ta.w, ta.h, st->docking_station.tile, st->docking_station.w, st->docking_station.h);
			}
			for (TileIndex tile : ta) {
				if ((docking_tiles.find(tile) != docking_tiles.end()) != IsDockingTile(tile)) {
					cclog("docking tile mismatch: tile {}", tile);
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
			if (v->Previous()) assert_msg(v->Previous()->Next() == v, "{}", v->index);
			if (v->Next()) assert_msg(v->Next()->Previous() == v, "{}", v->index);
		}
		for (const TemplateVehicle *tv : TemplateVehicle::Iterate()) {
			if (tv->Prev()) assert_msg(tv->Prev()->Next() == tv, "{}", tv->index);
			if (tv->Next()) assert_msg(tv->Next()->Prev() == tv, "{}", tv->index);
		}

		{
			extern std::string ValidateTemplateReplacementCaches();
			std::string template_validation_result = ValidateTemplateReplacementCaches();
			if (!template_validation_result.empty()) {
				cclog("Template replacement cache validation failed: {}", template_validation_result);
			}
		}

		if (!TraceRestrictSlot::ValidateVehicleIndex()) cclog("Trace restrict slot vehicle index validation failed");
		TraceRestrictSlot::ValidateSlotOccupants(log);
		TraceRestrictSlot::ValidateSlotGroupDescendants(log);

		if (!CargoPacket::ValidateDeferredCargoPayments()) cclog("Cargo packets deferred payments validation failed");

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
			if (saved_order_destination_refcount_map != _order_destination_refcount_map) cclog("Order destination refcount map mismatch");
		} else {
			cclog("Order destination refcount map not valid");
		}
	}

	if (flags & CHECK_CACHE_WATER_REGIONS) {
		extern void WaterRegionCheckCaches(std::function<void(std::string_view)> log);
		WaterRegionCheckCaches(cclog_output);
	}

	if ((flags & CHECK_CACHE_EMIT_LOG) && !saved_messages.empty()) {
		InconsistencyExtraInfo info;
		info.check_caches_result = std::move(saved_messages);
		CrashLog::InconsistencyLog(info);
		for (std::string &str : info.check_caches_result) {
			LogDesyncMsg(std::move(str));
		}
	}

#undef CCLOGV
#undef CCLOGV1
}

/**
 * Network-safe forced desync check.
 * @param tile unused
 * @param flags operation to perform
 * @return the cost of this operation or an error
 */
CommandCost CmdDesyncCheck(DoCommandFlag flags)
{
	if (flags & DC_EXEC) {
		CheckCaches(true, nullptr, CHECK_CACHE_ALL | CHECK_CACHE_EMIT_LOG);
	}

	return CommandCost();
}
