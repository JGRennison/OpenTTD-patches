/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_debug_data.h Data 'tables' for NewGRF debugging. */

#include "../newgrf_house.h"
#include "../newgrf_engine.h"
#include "../newgrf_roadtype.h"
#include "../newgrf_roadstop.h"
#include "../newgrf_cargo.h"
#include "../newgrf_newsignals.h"
#include "../newgrf_newlandscape.h"
#include "../date_func.h"
#include "../timetable.h"
#include "../ship.h"
#include "../aircraft.h"
#include "../object_map.h"
#include "../waypoint_base.h"
#include "../newgrf_extension.h"
#include "../animated_tile.h"
#include "../clear_map.h"
#include "../tunnelbridge.h"
#include "../train_speed_adaptation.h"
#include "../tracerestrict.h"
#include "../newgrf_dump.h"

/* Helper for filling property tables */
#define NIP(prop, base, variable, type, name) { name, { (ptrdiff_t)cpp_offsetof(base, variable), cpp_sizeof(base, variable) }, prop, type }

/* Helper for filling callback tables */
#define NIC(cb_id, base, variable, bit) { #cb_id, { (ptrdiff_t)cpp_offsetof(base, variable), cpp_sizeof(base, variable) }, bit, cb_id }

/* Helper for filling variable tables */
#define NIV(var, name) { name, var, NIVF_NONE }
#define NIVF(var, name, flags) { name, var, flags }


static InspectTargetId GetTownInspectTargetId(const Town *town)
{
	if (town == nullptr) return InspectTargetId::Invalid();
	return InspectTargetId(GSF_FAKE_TOWNS, town->index);
}

struct label_dumper : public NewGRFLabelDumper {
	inline const char *RailTypeLabel(RailType rt)
	{
		return this->Label(GetRailTypeInfo(rt)->label);
	}

	inline const char *RoadTypeLabel(RoadType rt)
	{
		return this->Label(GetRoadTypeInfo(rt)->label);
	}
};

static void DumpRailTypeList(NIExtraInfoOutput &output, const char *prefix, RailTypes rail_types, RailTypes mark = RAILTYPES_NONE)
{
	for (RailType rt = RAILTYPE_BEGIN; rt < RAILTYPE_END; rt++) {
		if (!HasBit(rail_types, rt)) continue;
		const RailTypeInfo *rti = GetRailTypeInfo(rt);
		if (rti->label == 0) continue;

		output.Print("{}{:02} {}{}",
				prefix,
				(uint)rt,
				label_dumper().Label(rti->label),
				HasBit(mark, rt) ? " !!!" : "");
	}
}

static void DumpRoadTypeList(NIExtraInfoOutput &output, const char *prefix, RoadTypes road_types)
{
	for (RoadType rt = ROADTYPE_BEGIN; rt < ROADTYPE_END; rt++) {
		if (!HasBit(road_types, rt)) continue;
		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
		if (rti->label == 0) continue;

		output.Print("{}{:02} {} {}",
				prefix,
				(uint)rt,
				RoadTypeIsTram(rt) ? "Tram" : "Road",
				label_dumper().Label(rti->label));
	}
}

/*** NewGRF Vehicles ***/

#define NICV(cb_id, bit) NIC(cb_id, Engine, info.callback_mask, bit)
static const NICallback _nic_vehicles[] = {
	NICV(CBID_VEHICLE_VISUAL_EFFECT,         VehicleCallbackMask::VisualEffect),
	NICV(CBID_VEHICLE_LENGTH,                VehicleCallbackMask::Length),
	NICV(CBID_VEHICLE_LOAD_AMOUNT,           VehicleCallbackMask::LoadAmount),
	NICV(CBID_VEHICLE_REFIT_CAPACITY,        VehicleCallbackMask::RefitCapacity),
	NICV(CBID_VEHICLE_ARTIC_ENGINE,          VehicleCallbackMask::ArticEngine),
	NICV(CBID_VEHICLE_CARGO_SUFFIX,          VehicleCallbackMask::CargoSuffix),
	NICV(CBID_TRAIN_ALLOW_WAGON_ATTACH,      std::monostate{}),
	NICV(CBID_VEHICLE_ADDITIONAL_TEXT,       std::monostate{}),
	NICV(CBID_VEHICLE_COLOUR_MAPPING,        VehicleCallbackMask::ColourRemap),
	NICV(CBID_VEHICLE_START_STOP_CHECK,      std::monostate{}),
	NICV(CBID_VEHICLE_32DAY_CALLBACK,        std::monostate{}),
	NICV(CBID_VEHICLE_SOUND_EFFECT,          VehicleCallbackMask::SoundEffect),
	NICV(CBID_VEHICLE_AUTOREPLACE_SELECTION, std::monostate{}),
	NICV(CBID_VEHICLE_MODIFY_PROPERTY,       std::monostate{}),
	NICV(CBID_VEHICLE_NAME,                  VehicleCallbackMask::Name),
};


static const NIVariable _niv_vehicles[] = {
	NIV(0x40, "position in consist and length"),
	NIV(0x41, "position and length of chain of same vehicles"),
	NIV(0x42, "transported cargo types"),
	NIV(0x43, "player info"),
	NIV(0x44, "aircraft info"),
	NIV(0x45, "curvature info"),
	NIV(0x46, "motion counter"),
	NIV(0x47, "vehicle cargo info"),
	NIV(0x48, "vehicle type info"),
	NIV(0x49, "year of construction"),
	NIV(0x4A, "current rail/road type info"),
	NIV(0x4B, "long date of last service"),
	NIV(0x4C, "current max speed"),
	NIV(0x4D, "position in articulated vehicle"),
	NIV(0x60, "count vehicle id occurrences"),
	// 0x61 not useful, since it requires register 0x10F
	NIV(0x62, "curvature/position difference to other vehicle"),
	NIV(0x63, "tile compatibility wrt. track-type"),
};

class NIHVehicle : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return Vehicle::Get(index)->GetGRF() == nullptr; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { const Vehicle *first = Vehicle::Get(index)->First(); return InspectTargetId(GetGrfSpecFeature(first->type), first->index); }
	const void *GetInstance(uint index) const override   { return Vehicle::Get(index); }
	const void *GetSpec(uint index) const override       { return Vehicle::Get(index)->GetEngine(); }
	void SetStringParameters(uint index) const override  { this->SetSimpleStringParameters(STR_VEHICLE_NAME, Vehicle::Get(index)->First()->index); }
	uint32_t GetGRFID(uint index) const override         { return Vehicle::Get(index)->GetGRFID(); }
	std::span<const BadgeID> GetBadges(uint index) const override { return Vehicle::Get(index)->GetEngine()->badges; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		Vehicle *v = Vehicle::Get(index);
		VehicleResolverObject ro(v->engine_type, v, VehicleResolverObject::WO_CACHED);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	/* virtual */ void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{

		Vehicle *v = Vehicle::Get(index);
		output.Print("Debug Info:");
		this->VehicleInfo(v, output, true, 0);
		if (v->type == VEH_AIRCRAFT) {
			output.Print("");
			output.Print("Shadow:");
			this->VehicleInfo(v->Next(), output, false, 8);
			if (v->Next()->Next() != nullptr) {
				output.Print("");
				output.Print("Rotor:");
				this->VehicleInfo(v->Next()->Next(), output, false, 16);
			}
		}
	}

	void VehicleInfo(Vehicle *v, NIExtraInfoOutput &output, bool show_engine, uint flag_shift) const
	{
		output.Print("  Index: {}", v->index);
		output.register_next_line_click_flag_toggle(1 << flag_shift);
		if (output.flags & (1 << flag_shift)) {
			output.Print("  [-] Flags:");

			v->DumpVehicleFlagsMultiline(output.buffer, "    ", "  ");
			output.FinishPrintMultiline();

			output.Print("    Tile hash: {}", (v->hash_tile_current != INVALID_TILE) ? "yes" : "no");
		} else {
			output.buffer.append("  [+] Flags: ");

			v->DumpVehicleFlags(output.buffer, false);
			output.FinishPrint();
		}

		output.buffer.append("  ");
		DumpTileInfo(output.buffer, v->tile);
		if (output.buffer.size() > 2 && output.buffer.data()[2] == 't') output.buffer.data()[2] = 'T';
		output.FinishPrint();

		TileIndex vtile = TileVirtXY(v->x_pos, v->y_pos);
		if (v->tile != vtile) {
			output.Print("  VirtXYTile: {}", vtile);
		}
		output.buffer.format("  Position: {:X}, {:X}, {:X}, Direction: {}", v->x_pos, v->y_pos, v->z_pos, v->direction);
		if (v->type == VEH_TRAIN) output.buffer.format(", tile margin: {}", GetTileMarginInFrontOfTrain(Train::From(v)));
		if (v->type == VEH_SHIP) output.buffer.format(", rotation: {}", Ship::From(v)->rotation);
		output.FinishPrint();

		if (v->IsPrimaryVehicle()) {
			output.Print("  Order indices: real: {}, implicit: {}, tt: {}, current type: {}",
					v->cur_real_order_index, v->cur_implicit_order_index, v->cur_timetable_order_index, GetOrderTypeName(v->current_order.GetType()));
			output.Print("  Current order time: ({}, {} mins), current loading time: ({}, {} mins)",
					v->current_order_time, v->current_order_time / _settings_time.ticks_per_minute,
					v->current_loading_time, v->current_loading_time / _settings_time.ticks_per_minute);
		}
		output.Print("  Speed: {}, sub-speed: {}, progress: {}, acceleration: {}",
				v->cur_speed, v->subspeed, v->progress, v->acceleration);
		output.Print("  Reliability: {}, spd_dec: {}, needs service: {}",
				v->reliability, v->reliability_spd_dec, v->NeedsServicing() ? "yes" : "no");
		output.Print("  Breakdown: ctr: {}, delay: {}, since svc: {}, chance: {}",
				v->breakdown_ctr, v->breakdown_delay, v->breakdowns_since_last_service, v->breakdown_chance);
		output.Print("  V Cache: max speed: {}, cargo age period: {}, vis effect: {}",
				v->vcache.cached_max_speed, v->vcache.cached_cargo_age_period, v->vcache.cached_vis_effect);
		if (v->cargo_type != INVALID_CARGO) {
			output.Print("  V Cargo: type: {}, sub type: {}, cap: {}, transfer: {}, deliver: {}, keep: {}, load: {}",
					v->cargo_type, v->cargo_subtype, v->cargo_cap,
					v->cargo.ActionCount(VehicleCargoList::MTA_TRANSFER), v->cargo.ActionCount(VehicleCargoList::MTA_DELIVER),
					v->cargo.ActionCount(VehicleCargoList::MTA_KEEP), v->cargo.ActionCount(VehicleCargoList::MTA_LOAD));
		}
		if (BaseStation::IsValidID(v->last_station_visited)) {
			output.Print("  V Last station visited: {}, {}", v->last_station_visited, BaseStation::Get(v->last_station_visited)->GetCachedName());
		}
		if (BaseStation::IsValidID(v->last_loading_station)) {
			output.Print("  V Last loading station: {}, {}", v->last_loading_station, BaseStation::Get(v->last_loading_station)->GetCachedName());
			output.Print("  V Last loading tick: {} ({}, {} mins ago)",
					v->last_loading_tick, _state_ticks - v->last_loading_tick, (_state_ticks - v->last_loading_tick).base() / _settings_time.ticks_per_minute);
		}
		if (v->IsGroundVehicle()) {
			const GroundVehicleCache &gvc = *(v->GetGroundVehicleCache());
			output.Print("  GV Cache: weight: {}, slope res: {}, max TE: {}, axle res: {}",
					gvc.cached_weight, gvc.cached_slope_resistance, gvc.cached_max_te, gvc.cached_axle_resistance);
			output.Print("  GV Cache: max track speed: {}, power: {}, air drag: {}",
					gvc.cached_max_track_speed, gvc.cached_power, gvc.cached_air_drag);
			output.Print("  GV Cache: total length: {}, veh length: {}",
					gvc.cached_total_length, gvc.cached_veh_length);
		}
		if (v->type == VEH_TRAIN) {
			const Train *t = Train::From(v);
			output.Print("  T cache: tilt: {}, speed varies by railtype: {}, curve speed mod: {}, engines: {}",
					(t->tcache.cached_tflags & TCF_TILT) ? 1 : 0, (t->tcache.cached_tflags & TCF_SPD_RAILTYPE) ? 1 : 0, t->tcache.cached_curve_speed_mod, t->tcache.cached_num_engines);
			output.Print("  T cache: RL braking: {}, decel: {}, uncapped decel: {}, centre mass: {}, braking length: {}",
					(t->UsingRealisticBraking()) ? 1 : 0, t->tcache.cached_deceleration, t->tcache.cached_uncapped_decel, t->tcache.cached_centre_mass, t->tcache.cached_braking_length);
			output.Print("  T cache: veh weight: {}, user data: {}, curve speed: {}",
					t->tcache.cached_veh_weight, t->tcache.user_def_data, t->tcache.cached_max_curve_speed);
			output.Print("  Wait counter: {}, rev distance: {}, TBSN: {}",
					t->wait_counter, t->reverse_distance, t->tunnel_bridge_signal_num);
			output.Print("  Speed restriction: {}, signal speed restriction (ATC): {}",
					t->speed_restriction, t->signal_speed_restriction);

			output.register_next_line_click_flag_toggle(8 << flag_shift);
			output.Print("  [{}] Railtype: {} ({}), compatible_railtypes: 0x{}, acceleration type: {}",
					(output.flags & (8 << flag_shift)) ? '-' : '+', t->railtype, label_dumper().RailTypeLabel(t->railtype), t->compatible_railtypes, t->GetAccelerationType());

			if (output.flags & (8 << flag_shift)) {
				DumpRailTypeList(output, "    ", t->compatible_railtypes);
			}

			if (t->vehstatus & VS_CRASHED) {
				output.Print("  CRASHED: anim pos: {}", t->crash_anim_pos);
			} else if (t->crash_anim_pos > 0) {
				output.Print("  Brake heating: {}", t->crash_anim_pos);
			}
			if (t->lookahead != nullptr) {
				output.Print("  Look ahead:");
				const TrainReservationLookAhead &l = *t->lookahead;
				TrainDecelerationStats stats(t, l.cached_zpos);

				auto print_braking_speed = [&](int position, int end_speed, int end_z) {
					if (!t->UsingRealisticBraking()) return;
					extern void LimitSpeedFromLookAhead(int &max_speed, const TrainDecelerationStats &stats, int current_position, int position, int end_speed, int z_delta);
					int speed = INT_MAX;
					LimitSpeedFromLookAhead(speed, stats, l.current_position, position, end_speed, end_z - stats.z_pos);
					if (speed != INT_MAX) {
						output.buffer.format(", appr speed: {}", speed);
					}
				};

				output.buffer.format("    Position: current: {}, z: {}, end: {}, remaining: {}", l.current_position, stats.z_pos, l.reservation_end_position, l.reservation_end_position - l.current_position);
				if (l.lookahead_end_position <= l.reservation_end_position) {
					output.buffer.format(", (lookahead: end: {}, remaining: {})", l.lookahead_end_position, l.lookahead_end_position - l.current_position);
				}
				if (l.next_extend_position > l.current_position) {
					output.buffer.format(", next extend position: {} (dist: {})", l.next_extend_position, l.next_extend_position - l.current_position);
				}
				output.FinishPrint();

				const int overall_zpos = t->CalculateOverallZPos();
				output.Print("    Cached zpos: {} (actual: {}, delta: {}), positions to refresh: {}",
						l.cached_zpos, overall_zpos, (l.cached_zpos - overall_zpos), l.zpos_refresh_remaining);

				output.buffer.format("    Reservation ends at {}, trackdir: {:02X}, z: {}",
						l.reservation_end_tile, l.reservation_end_trackdir, l.reservation_end_z);
				if (l.flags.Test(TrainReservationLookAheadFlag::DepotEnd)) {
					print_braking_speed(l.reservation_end_position - TILE_SIZE, _settings_game.vehicle.rail_depot_speed_limit, l.reservation_end_z);
				} else {
					print_braking_speed(l.reservation_end_position, 0, l.reservation_end_z);
				}
				output.FinishPrint();

				output.buffer.format("    TB reserved tiles: {}, flags: ", l.tunnel_bridge_reserved_tiles);
				if (l.flags.Test(TrainReservationLookAheadFlag::TunnelBridgeExitFree)) output.buffer.push_back('x');
				if (l.flags.Test(TrainReservationLookAheadFlag::DepotEnd)) output.buffer.push_back('d');
				if (l.flags.Test(TrainReservationLookAheadFlag::ApplyAdvisory)) output.buffer.push_back('a');
				if (l.flags.Test(TrainReservationLookAheadFlag::Chunnel)) output.buffer.push_back('c');
				if (l.flags.Test(TrainReservationLookAheadFlag::TunnelBridgeCombinedDefer)) output.buffer.push_back('d');
				output.FinishPrint();

				output.Print("    Items: {}", l.items.size());
				for (const TrainReservationLookAheadItem &item : l.items) {
					output.buffer.format("      Start: {} (dist: {}), end: {} (dist: {}), z: {}, ",
							item.start, item.start - l.current_position, item.end, item.end - l.current_position, item.z_pos);
					switch (item.type) {
						case TRLIT_STATION:
							output.buffer.format("station: {}, {}", item.data_id, BaseStation::IsValidID(item.data_id) ? BaseStation::Get(item.data_id)->GetCachedName() : "[invalid]");
							if (t->current_order.ShouldStopAtStation(t->last_station_visited, item.data_id, Waypoint::GetIfValid(item.data_id) != nullptr)) {
								extern int PredictStationStoppingLocation(const Train *v, const Order *order, int station_length, DestinationID dest);
								int stop_position = PredictStationStoppingLocation(t, &(t->current_order), item.end - item.start, item.data_id);
								output.buffer.format(", stop_position: {}", item.start + stop_position);
								print_braking_speed(item.start + stop_position, 0, item.z_pos);
							} else if (t->current_order.IsType(OT_GOTO_WAYPOINT) && t->current_order.GetDestination() == item.data_id && (t->current_order.GetWaypointFlags() & OWF_REVERSE)) {
								print_braking_speed(item.start + t->gcache.cached_total_length, 0, item.z_pos);
							}
							break;
						case TRLIT_REVERSE:
							output.buffer.format("reverse");
							print_braking_speed(item.start + t->gcache.cached_total_length, 0, item.z_pos);
							break;
						case TRLIT_TRACK_SPEED:
							output.buffer.format("track speed: {}", item.data_id);
							print_braking_speed(item.start, item.data_id, item.z_pos);
							break;
						case TRLIT_SPEED_RESTRICTION:
							output.buffer.format("speed restriction: {}", item.data_id);
							if (item.data_id > 0) print_braking_speed(item.start, item.data_id, item.z_pos);
							break;
						case TRLIT_SIGNAL:
							output.buffer.format("signal: target speed: {}, style: {}, flags:", item.data_id, item.data_aux >> 8);
							if (HasBit(item.data_aux, TRSLAI_NO_ASPECT_INC)) output.buffer.format("n");
							if (HasBit(item.data_aux, TRSLAI_NEXT_ONLY)) output.buffer.format("s");
							if (HasBit(item.data_aux, TRSLAI_COMBINED)) output.buffer.format("c");
							if (HasBit(item.data_aux, TRSLAI_COMBINED_SHUNT)) output.buffer.format("X");
							if (_settings_game.vehicle.realistic_braking_aspect_limited == TRBALM_ON &&
									(l.lookahead_end_position == item.start || l.lookahead_end_position == item.start + 1)) {
								output.buffer.append(", lookahead end");
								print_braking_speed(item.start, 0, item.z_pos);
							}
							break;
						case TRLIT_CURVE_SPEED:
							output.buffer.format("curve speed: {}", item.data_id);
							if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) print_braking_speed(item.start, item.data_id, item.z_pos);

							break;
						case TRLIT_SPEED_ADAPTATION: {
							TileIndex tile(item.data_id);
							uint16_t td = item.data_aux;
							output.buffer.format("speed adaptation: tile: {:X}, trackdir: {:X}", tile, td);
							if (item.end + 1 < l.reservation_end_position) {
								output.buffer.format(" --> {}", GetLowestSpeedTrainAdaptationSpeedAtSignal(tile, td));
							}
							break;
						}
					}
					output.FinishPrint();
				}

				output.Print("    Curves: {}", l.curves.size());
				for (const TrainReservationLookAheadCurve &curve : l.curves) {
					output.Print("      Pos: {} (dist: {}), dir diff: {}", curve.position, curve.position - l.current_position, curve.dir_diff);
				}
			}
		}
		if (v->type == VEH_ROAD) {
			const RoadVehicle *rv = RoadVehicle::From(v);
			output.Print("  Overtaking: {}, overtaking_ctr: {}, overtaking threshold: {}",
					rv->overtaking, rv->overtaking_ctr, rv->GetOvertakingCounterThreshold());
			output.Print("  Speed: {}", rv->cur_speed);

			output.buffer.append("  Path cache: ");
			if (rv->cached_path != nullptr) {
				output.buffer.format("length: {}, layout ctr: {:X} (current: {:X})", rv->cached_path->size(), rv->cached_path->layout_ctr, _road_layout_change_counter);
				output.FinishPrint();
				uint idx = rv->cached_path->start;
				for (uint i = 0; i < rv->cached_path->size(); i++) {
					if (output.buffer.size() == 0) {
						output.buffer.append("    ");
					} else {
						output.buffer.append(", ");
					}
					output.buffer.format("({}x{}, {:X})", TileX(rv->cached_path->tile[idx]), TileY(rv->cached_path->tile[idx]), rv->cached_path->td[idx]);
					if ((i & 3) == 3) output.FinishPrint();
					idx = (idx + 1) & RV_PATH_CACHE_SEGMENT_MASK;
				}
				if (output.buffer.size() > 0) output.FinishPrint();
				output.buffer.clear();
			} else {
				output.buffer.append("none");
				output.FinishPrint();
			}

			output.register_next_line_click_flag_toggle(8 << flag_shift);
			output.Print("  [{}] Roadtype: {} ({}), Compatible: 0x{:X}",
					(output.flags & (8 << flag_shift)) ? '-' : '+', rv->roadtype, label_dumper().RoadTypeLabel(rv->roadtype), rv->compatible_roadtypes);
			if (output.flags & (8 << flag_shift)) {
				DumpRoadTypeList(output, "    ", rv->compatible_roadtypes);
			}
		}
		if (v->type == VEH_SHIP) {
			const Ship *s = Ship::From(v);
			output.Print("  Lost counter: {}",
					s->lost_count);

			output.buffer.format("  Path cache: ");
			if (!s->cached_path.empty()) {
				output.buffer.format("length: {}", s->cached_path.size());
				output.FinishPrint();
				uint i = 0;
				for (Trackdir td : s->cached_path) {
					if ((i & 7) == 0) {
						if (output.buffer.size() > 0) output.FinishPrint();
						output.buffer.format("    {:X}", td);
					} else {
						output.buffer.format(", {:X}", td);
					}
					i++;
				}
				if (output.buffer.size() > 0) output.FinishPrint();
			} else {
				output.buffer.append("none");
				output.FinishPrint();
			}
		}
		if (v->type == VEH_AIRCRAFT) {
			const Aircraft *a = Aircraft::From(v);
			output.buffer.format("  Pos: {}, prev pos: {}, state: {}",
					a->pos, a->previous_pos, a->state);
			if (a->IsPrimaryVehicle()) output.buffer.format(" ({})", AirportMovementStateToString(a->state));
			output.buffer.format(", flags: 0x{:X}", a->flags);
			output.FinishPrint();
			if (BaseStation::IsValidID(a->targetairport)) {
				output.Print("  Target airport: {}, {}", a->targetairport, BaseStation::Get(a->targetairport)->GetCachedName());
			}
		}

		output.Print("  Cached sprite bounds: ({}, {}) to ({}, {}), offs: ({}, {})",
				v->sprite_seq_bounds.left, v->sprite_seq_bounds.top, v->sprite_seq_bounds.right, v->sprite_seq_bounds.bottom, v->x_offs, v->y_offs);

		output.Print("  Current image cacheable: {} ({:X}), spritenum: {:X}",
				v->cur_image_valid_dir != INVALID_DIR ? "yes" : "no", v->cur_image_valid_dir, v->spritenum);

		if (HasBit(v->vehicle_flags, VF_SEPARATION_ACTIVE)) {
			std::vector<TimetableProgress> progress_array = PopulateSeparationState(v);
			if (!progress_array.empty()) {
				output.Print("  Separation state:");
			}
			for (const auto &info : progress_array) {
				output.buffer.format("    {} [{}, {}, {}], {}, ",
						info.id == v->index ? '*' : ' ', info.order_count, info.order_ticks, info.cumulative_ticks, info.id);
				SetDParam(0, info.id);
				output.buffer.append(GetString(STR_VEHICLE_NAME));
				output.buffer.format(", lateness: {}", Vehicle::Get(info.id)->lateness_counter);
				output.FinishPrint();
			}
		}

		if (v->HasUnbunchingOrder()) {
			output.Print("  Unbunching state:");
			for (const Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
				output.buffer.format("  {} {} (unit {}):", u == v ? '*' : ' ', u->index, u->unitnumber);

				if (u->unbunch_state == nullptr) {
					output.buffer.append(" [NO DATA]");
				}
				if (u->vehstatus & (VS_STOPPED | VS_CRASHED)) {
					output.buffer.append(" [STOPPED]");
				}
				output.FinishPrint();

				if (u->unbunch_state != nullptr) {
					auto print_tick = [&](StateTicks tick, const char *label) {
						output.buffer.format("      {}: ", label);
						if (tick != INVALID_STATE_TICKS) {
							if (tick > _state_ticks) {
								output.buffer.format("{} (in {} mins)", tick, (tick - _state_ticks) / _settings_time.ticks_per_minute);
							} else {
								output.buffer.format("{} ({} mins ago)", tick, (_state_ticks - tick) / _settings_time.ticks_per_minute);
							}
						} else {
							output.buffer.append("invalid");
						}
						output.FinishPrint();
					};
					print_tick(u->unbunch_state->depot_unbunching_last_departure, "Last unbunch departure");
					print_tick(u->unbunch_state->depot_unbunching_next_departure, "Next unbunch departure");
					output.Print("      RTT: {} ({} mins)", u->unbunch_state->round_trip_time, u->unbunch_state->round_trip_time / _settings_time.ticks_per_minute);
				}
			}
		}

		if (show_engine) {
			const Engine *e = Engine::GetIfValid(v->engine_type);
			output.buffer.format("  Engine: {}", v->engine_type);
			if (e->grf_prop.grffile != nullptr) {
				output.buffer.format(" (local ID: {})", e->grf_prop.local_id);
			}
			if (e->info.variant_id != INVALID_ENGINE) {
				output.buffer.format(", variant of: {}", e->info.variant_id);
				const Engine *variant_e = Engine::GetIfValid(e->info.variant_id);
				if (variant_e->grf_prop.grffile != nullptr) {
					output.buffer.format(" (local ID: {})", variant_e->grf_prop.local_id);
				}
			}
			output.FinishPrint();

			if (e != nullptr) {
				{
					output.buffer.append("    Callback flags: ");
					bool first = true;
					auto add_cb_flag = [&](SpriteGroupCallbacksUsed flag, const char *name) {
						if ((e->callbacks_used & flag) == 0) return;
						if (!first) output.buffer.append(", ");
						output.buffer.append(name);
						first = false;
					};
					add_cb_flag(SGCU_VEHICLE_32DAY_CALLBACK, "VEHICLE_32DAY_CALLBACK");
					add_cb_flag(SGCU_VEHICLE_REFIT_COST, "VEHICLE_REFIT_COST");
					add_cb_flag(SGCU_RANDOM_TRIGGER, "RANDOM_TRIGGER");
					add_cb_flag(SGCU_CB36_SPEED_RAILTYPE, "CB36_SPEED_RAILTYPE");
					add_cb_flag(SGCU_REFIT_CB_ALL_CARGOES, "REFIT_CB_ALL_CARGOES");
					if (first) output.buffer.append("[NONE]");
					output.FinishPrint();
				}
				output.Print("    CB36 Properties: 0x{:X}", e->cb36_properties_used);
				uint64_t cb36_properties = e->cb36_properties_used;
				if (!e->sprite_group_cb36_properties_used.empty()) {
					const SpriteGroup *root_spritegroup = nullptr;
					if (v->IsGroundVehicle()) root_spritegroup = GetWagonOverrideSpriteSet(v->engine_type, v->cargo_type, v->GetGroundVehicleCache()->first_engine);
					if (root_spritegroup == nullptr) {
						CargoType cargo = v->cargo_type;
						const SpriteGroup *cargo_spritegroup = e->grf_prop.GetSpriteGroup(cargo);
						root_spritegroup = (cargo_spritegroup != nullptr) ? cargo_spritegroup : e->grf_prop.GetSpriteGroup(SpriteGroupCargo::SG_DEFAULT);
					}
					auto iter = e->sprite_group_cb36_properties_used.find(root_spritegroup);
					if (iter != e->sprite_group_cb36_properties_used.end()) {
						cb36_properties = iter->second;
						output.Print("    Current sprite group: CB36 Properties: 0x{:X}", iter->second);
					}
				}
				if (cb36_properties != UINT64_MAX) {
					uint64_t props = cb36_properties;
					while (props) {
						PropertyID prop = (PropertyID)FindFirstBit(props);
						props = KillFirstBit(props);
						uint16_t res = GetVehicleProperty(v, prop, CALLBACK_FAILED);
						if (res == CALLBACK_FAILED) {
							output.Print("      CB36: 0x{:X} --> FAILED", prop);
						} else {
							output.Print("      CB36: 0x{:X} --> 0x{:X}", prop, res);
						}
					}
				}
				if (e->refit_capacity_values != nullptr) {
					const EngineRefitCapacityValue *caps = e->refit_capacity_values.get();
					CargoTypes seen = 0;
					while (seen != ALL_CARGOTYPES) {
						output.Print("    Refit capacity cache: cargoes: 0x{:X} --> 0x{:X}", caps->cargoes, caps->capacity);
						seen |= caps->cargoes;
						caps++;
					}
				}
				CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(e->intro_date);
				CalTime::YearMonthDay base_ymd = CalTime::ConvertDateToYMD(e->info.base_intro);
				output.Print("    Intro: {:4}-{:02}-{:02} (base: {:4}-{:02}-{:02}), Age: {}, Base life: {}, Durations: {} {} {} (sum: {})",
						ymd.year, ymd.month + 1, ymd.day, base_ymd.year, base_ymd.month + 1, base_ymd.day,
						e->age, e->info.base_life, e->duration_phase_1, e->duration_phase_2, e->duration_phase_3,
						e->duration_phase_1 + e->duration_phase_2 + e->duration_phase_3);
				output.Print("    Reliability: {}, spd_dec: {} ({}), start: {}, max: {}, final: {}",
						e->reliability, e->reliability_spd_dec, e->info.decay_speed, e->reliability_start, e->reliability_max, e->reliability_final);
				output.Print("    Cargo type: {}, refit mask: 0x{:X}, refit cost: {}",
						e->info.cargo_type, e->info.refit_mask, e->info.refit_cost);
				output.Print("    Cargo age period: {}, cargo load speed: {}",
						e->info.cargo_age_period, e->info.load_amount);
				output.Print("    Company availability: {:X}, climates: {:X}",
						e->company_avail, e->info.climates);

				output.register_next_line_click_flag_toggle(2 << flag_shift);
				if (output.flags & (2 << flag_shift)) {
					output.Print("    [-] Engine Misc Flags:\n");
					auto print_bit = [&](EngineMiscFlag bit, const char *name) {
						if (e->info.misc_flags.Test(bit)) {
							output.Print("      {}\n", name);
						}
					};
					print_bit(EngineMiscFlag::RailTilts,                "RailTilts");
					print_bit(EngineMiscFlag::Uses2CC,                  "Uses2CC");
					print_bit(EngineMiscFlag::RailIsMU,                 "RailIsMU");
					print_bit(EngineMiscFlag::RailFlips,                "RailFlips");
					print_bit(EngineMiscFlag::AutoRefit,                "AutoRefit");
					print_bit(EngineMiscFlag::NoDefaultCargoMultiplier, "NoDefaultCargoMultiplier");
					print_bit(EngineMiscFlag::NoBreakdownSmoke,         "NoBreakdownSmoke");
					print_bit(EngineMiscFlag::SpriteStack,              "SpriteStack");
				} else {
					output.Print("    [+] Engine Misc Flags: {}{}{}{}{}{}{}{}",
							e->info.misc_flags.Test(EngineMiscFlag::RailTilts)                ? 't' : '-',
							e->info.misc_flags.Test(EngineMiscFlag::Uses2CC)                  ? '2' : '-',
							e->info.misc_flags.Test(EngineMiscFlag::RailIsMU)                 ? 'm' : '-',
							e->info.misc_flags.Test(EngineMiscFlag::RailFlips)                ? 'f' : '-',
							e->info.misc_flags.Test(EngineMiscFlag::AutoRefit)                ? 'r' : '-',
							e->info.misc_flags.Test(EngineMiscFlag::NoDefaultCargoMultiplier) ? 'c' : '-',
							e->info.misc_flags.Test(EngineMiscFlag::NoBreakdownSmoke)         ? 'b' : '-',
							e->info.misc_flags.Test(EngineMiscFlag::SpriteStack)              ? 's' : '-');
				}

				if (e->type == VEH_TRAIN) {
					const RailTypeInfo *rti = GetRailTypeInfo(e->u.rail.railtype);
					output.Print("    Railtype: {} ({}), Compatible: 0x{:X}, Powered: 0x{:X}, All compatible: 0x{:X}",
							e->u.rail.railtype, label_dumper().RailTypeLabel(e->u.rail.railtype), rti->compatible_railtypes, rti->powered_railtypes, rti->all_compatible_railtypes);
					static const char *engine_types[] = {
						"SINGLEHEAD",
						"MULTIHEAD",
						"WAGON",
					};
					output.Print("    Rail veh type: {}, power: {}", engine_types[e->u.rail.railveh_type], e->u.rail.power);
				}
				if (e->type == VEH_ROAD) {
					output.register_next_line_click_flag_toggle(16 << flag_shift);
					const RoadTypeInfo* rti = GetRoadTypeInfo(e->u.road.roadtype);
					output.Print("    [{}] Roadtype: {} ({}), Powered: 0x{:X}",
							(output.flags & (16 << flag_shift)) ? '-' : '+', e->u.road.roadtype, label_dumper().RoadTypeLabel(e->u.road.roadtype), rti->powered_roadtypes);
					if (output.flags & (16 << flag_shift)) {
						DumpRoadTypeList(output, "      ", rti->powered_roadtypes);
					}
					output.Print("    Capacity: {}, Weight: {}, Power: {}, TE: {}, Air drag: {}, Shorten: {}",
							e->u.road.capacity, e->u.road.weight, e->u.road.power, e->u.road.tractive_effort, e->u.road.air_drag, e->u.road.shorten_factor);
				}
				if (e->type == VEH_SHIP) {
					output.Print("    Capacity: {}, Max speed: {}, Accel: {}, Ocean speed: {}, Canal speed: {}",
							e->u.ship.capacity, e->u.ship.max_speed, e->u.ship.acceleration, e->u.ship.ocean_speed_frac, e->u.ship.canal_speed_frac);
				}

				output.register_next_line_click_flag_toggle(4 << flag_shift);
				if (output.flags & (4 << flag_shift)) {
					output.Print("    [-] Extra Engine Flags:\n");
					auto print_bit = [&](ExtraEngineFlag flag, const char *name) {
						if (e->info.extra_flags.Test(flag)) {
							output.Print("      {}\n", name);
						}
					};
					print_bit(ExtraEngineFlag::NoNews,          "NoNews");
					print_bit(ExtraEngineFlag::NoPreview,       "NoPreview");
					print_bit(ExtraEngineFlag::JoinPreview,     "JoinPreview");
					print_bit(ExtraEngineFlag::SyncReliability, "SyncReliability");
				} else {
					output.Print("    [+] Extra Engine Flags: {}{}{}{}",
							e->info.extra_flags.Test(ExtraEngineFlag::NoNews)          ? 'n' : '-',
							e->info.extra_flags.Test(ExtraEngineFlag::NoPreview)       ? 'p' : '-',
							e->info.extra_flags.Test(ExtraEngineFlag::JoinPreview)     ? 'j' : '-',
							e->info.extra_flags.Test(ExtraEngineFlag::SyncReliability) ? 's' : '-');
				}
			}
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		extern void DumpVehicleSpriteGroup(const Vehicle *v, SpriteGroupDumper &dumper);
		DumpVehicleSpriteGroup(Vehicle::Get(index), dumper);
	}
};

static const NIFeature _nif_vehicle = {
	{},
	_nic_vehicles,
	_niv_vehicles,
	std::make_unique<NIHVehicle>(),
};


/*** NewGRF station (tiles) ***/

#define NICS(cb_id, bit) NIC(cb_id, StationSpec, callback_mask, bit)
static const NICallback _nic_stations[] = {
	NICS(CBID_STATION_AVAILABILITY,     StationCallbackMask::Avail),
	NICS(CBID_STATION_DRAW_TILE_LAYOUT, StationCallbackMask::DrawTileLayout),
	NICS(CBID_STATION_BUILD_TILE_LAYOUT,std::monostate{}),
	NICS(CBID_STATION_ANIM_START_STOP,  std::monostate{}),
	NICS(CBID_STATION_ANIM_NEXT_FRAME,  StationCallbackMask::AnimationNextFrame),
	NICS(CBID_STATION_ANIMATION_SPEED,  StationCallbackMask::AnimationSpeed),
	NICS(CBID_STATION_LAND_SLOPE_CHECK, StationCallbackMask::SlopeCheck),
};

static const NIVariable _niv_stations[] = {
	NIV(0x40, "platform info and relative position"),
	NIV(0x41, "platform info and relative position for individually built sections"),
	NIV(0x42, "terrain and track type"),
	NIV(0x43, "player info"),
	NIV(0x44, "path signalling info"),
	NIV(0x45, "rail continuation info"),
	NIV(0x46, "platform info and relative position from middle"),
	NIV(0x47, "platform info and relative position from middle for individually built sections"),
	NIV(0x48, "bitmask of accepted cargoes"),
	NIV(0x49, "platform info and relative position of same-direction section"),
	NIV(0x4A, "current animation frame"),
	NIV(0x60, "amount of cargo waiting"),
	NIV(0x61, "time since last cargo pickup"),
	NIV(0x62, "rating of cargo"),
	NIV(0x63, "time spent on route"),
	NIV(0x64, "information about last vehicle picking cargo up"),
	NIV(0x65, "amount of cargo acceptance"),
	NIV(0x66, "animation frame of nearby tile"),
	NIV(0x67, "land info of nearby tiles"),
	NIV(0x68, "station info of nearby tiles"),
	NIV(0x69, "information about cargo accepted in the past"),
	NIV(0x6A, "GRFID of nearby station tiles"),
	NIV(0x6B, "station ID of nearby tiles"),
	NIVF(A2VRI_STATION_INFO_NEARBY_TILES_V2, "station info of nearby tiles v2", NIVF_SHOW_PARAMS),
};

class NIHStation : public NIHelper {
	bool IsInspectable(uint index) const override        { return GetStationSpec(TileIndex{index}) != nullptr; }
	InspectTargetId GetParent(uint index) const override { return GetTownInspectTargetId(BaseStation::GetByTile(TileIndex{index})->town); }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return GetStationSpec(TileIndex{index}); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, GetStationIndex(TileIndex{index}), TileIndex{index}); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? GetStationSpec(TileIndex{index})->grf_prop.grfid : 0; }
	std::span<const BadgeID> GetBadges(uint index) const override { return this->IsInspectable(index) ? GetStationSpec(TileIndex{index})->badges : std::span<const BadgeID>{}; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		TileIndex tile{index};
		StationResolverObject ro(GetStationSpec(tile), BaseStation::GetByTile(tile), tile, INVALID_RAILTYPE);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	/* virtual */ void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		TileIndex tile{index};
		const StationSpec *statspec = GetStationSpec(tile);
		if (statspec == nullptr) return;

		if (statspec->grf_prop.grffile != nullptr) {
			output.Print("GRF local ID: {}", statspec->grf_prop.local_id);
		}

		const StationClass *cls = StationClass::Get(statspec->class_index);
		output.Print("Class ID: {}", label_dumper().Label(cls->global_id));

		for (size_t i = 0; i < statspec->renderdata.size(); i++) {
			output.Print("Tile Layout {}:", i);
			const NewGRFSpriteLayout &dts = statspec->renderdata[i];

			const TileLayoutRegisters *registers = dts.registers;
			auto print_reg_info = [&](uint i, bool is_parent) {
				if (registers == nullptr) {
					output.FinishPrint();
					return;
				}
				const TileLayoutRegisters *reg = registers + i;
				if (reg->flags == 0) {
					output.FinishPrint();
					return;
				}
				output.buffer.format(", register flags: {:X}", reg->flags);
				output.FinishPrint();
				auto log_reg = [&](TileLayoutFlags flag, const char *name, uint8_t flag_reg) {
					if (reg->flags & flag) {
						output.Print("  {} reg: {:X}", name, flag_reg);
					}
				};
				log_reg(TLF_DODRAW, "TLF_DODRAW", reg->dodraw);
				log_reg(TLF_SPRITE, "TLF_SPRITE", reg->sprite);
				log_reg(TLF_PALETTE, "TLF_PALETTE", reg->palette);
				if (is_parent) {
					log_reg(TLF_BB_XY_OFFSET, "TLF_BB_XY_OFFSET x", reg->delta.parent[0]);
					log_reg(TLF_BB_XY_OFFSET, "TLF_BB_XY_OFFSET y", reg->delta.parent[1]);
					log_reg(TLF_BB_Z_OFFSET, "TLF_BB_Z_OFFSET", reg->delta.parent[2]);
				} else {
					log_reg(TLF_CHILD_X_OFFSET, "TLF_CHILD_X_OFFSET", reg->delta.child[0]);
					log_reg(TLF_CHILD_Y_OFFSET, "TLF_CHILD_Y_OFFSET", reg->delta.child[1]);
				}
				if (reg->flags & TLF_SPRITE_VAR10) {
					output.Print("  TLF_SPRITE_VAR10 value: {:X}", reg->sprite_var10);
				}
				if (reg->flags & TLF_PALETTE_VAR10) {
					output.Print("  TLF_PALETTE_VAR10 value: {:X}", reg->palette_var10);
				}
			};

			output.buffer.format("  ground: ({:X}, {:X})",
					dts.ground.sprite, dts.ground.pal);
			print_reg_info(0, false); // this calls output.FinishPrint() as needed

			uint offset = 0; // offset 0 is the ground sprite
			const DrawTileSeqStruct *element;
			foreach_draw_tile_seq(element, dts.seq) {
				offset++;
				if (element->IsParentSprite()) {
					output.buffer.format("  section: {:X}, image: ({:X}, {:X}), d: ({}, {}, {}), s: ({}, {}, {})",
							offset, element->image.sprite, element->image.pal,
							element->delta_x, element->delta_y, element->delta_z,
							element->size_x, element->size_y, element->size_z);
				} else {
					output.buffer.format("  section: {:X}, image: ({:X}, {:X}), d: ({}, {})",
							offset, element->image.sprite, element->image.pal,
							element->delta_x, element->delta_y);
				}
				print_reg_info(offset, element->IsParentSprite()); // this calls output.FinishPrint() as needed
			}
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		TileIndex tile{index};
		extern void DumpStationSpriteGroup(const StationSpec *statspec, BaseStation *st, SpriteGroupDumper &dumper);
		DumpStationSpriteGroup(GetStationSpec(tile), BaseStation::GetByTile(tile), dumper);
	}
};

static const NIFeature _nif_station = {
	{},
	_nic_stations,
	_niv_stations,
	std::make_unique<NIHStation>(),
};


/*** NewGRF house tiles ***/

#define NICH(cb_id, bit) NIC(cb_id, HouseSpec, callback_mask, bit)
static const NICallback _nic_house[] = {
	NICH(CBID_HOUSE_ALLOW_CONSTRUCTION,        HouseCallbackMask::AllowConstruction),
	NICH(CBID_HOUSE_ANIMATION_NEXT_FRAME,      HouseCallbackMask::AnimationNextFrame),
	NICH(CBID_HOUSE_ANIMATION_START_STOP,      HouseCallbackMask::AnimationStartStop),
	NICH(CBID_HOUSE_CONSTRUCTION_STATE_CHANGE, HouseCallbackMask::ConstructionStateChange),
	NICH(CBID_HOUSE_COLOUR,                    HouseCallbackMask::Colour),
	NICH(CBID_HOUSE_CARGO_ACCEPTANCE,          HouseCallbackMask::CargoAcceptance),
	NICH(CBID_HOUSE_ANIMATION_SPEED,           HouseCallbackMask::AnimationSpeed),
	NICH(CBID_HOUSE_DESTRUCTION,               HouseCallbackMask::Destruction),
	NICH(CBID_HOUSE_ACCEPT_CARGO,              HouseCallbackMask::AcceptCargo),
	NICH(CBID_HOUSE_PRODUCE_CARGO,             HouseCallbackMask::ProduceCargo),
	NICH(CBID_HOUSE_DENY_DESTRUCTION,          HouseCallbackMask::DenyDestruction),
	NICH(CBID_HOUSE_WATCHED_CARGO_ACCEPTED,    std::monostate{}),
	NICH(CBID_HOUSE_CUSTOM_NAME,               std::monostate{}),
	NICH(CBID_HOUSE_DRAW_FOUNDATIONS,          HouseCallbackMask::DrawFoundations),
	NICH(CBID_HOUSE_AUTOSLOPE,                 HouseCallbackMask::Autoslope),
};

static const NIVariable _niv_house[] = {
	NIV(0x40, "construction state of tile and pseudo-random value"),
	NIV(0x41, "age of building in years"),
	NIV(0x42, "town zone"),
	NIV(0x43, "terrain type"),
	NIV(0x44, "building counts"),
	NIV(0x45, "town expansion bits"),
	NIV(0x46, "current animation frame"),
	NIV(0x47, "xy coordinate of the building"),
	NIV(0x60, "other building counts (old house type)"),
	NIV(0x61, "other building counts (new house type)"),
	NIV(0x62, "land info of nearby tiles"),
	NIV(0x63, "current animation frame of nearby house tile"),
	NIV(0x64, "cargo acceptance history of nearby stations"),
	NIV(0x65, "distance of nearest house matching a given criterion"),
	NIV(0x66, "class and ID of nearby house tile"),
	NIV(0x67, "GRFID of nearby house tile"),
	NIV(A2VRI_HOUSE_SAME_ID_MAP_COUNT, "building count: same ID, map"),
	NIV(A2VRI_HOUSE_SAME_CLASS_MAP_COUNT, "building count: same class, map"),
	NIV(A2VRI_HOUSE_SAME_ID_TOWN_COUNT, "building count: same ID, town"),
	NIV(A2VRI_HOUSE_SAME_CLASS_TOWN_COUNT, "building count: same class, town"),
	NIVF(A2VRI_HOUSE_OTHER_OLD_ID_MAP_COUNT, "building count: other old ID, map", NIVF_SHOW_PARAMS),
	NIVF(A2VRI_HOUSE_OTHER_OLD_ID_TOWN_COUNT, "building count: other old ID, town", NIVF_SHOW_PARAMS),
	NIVF(A2VRI_HOUSE_OTHER_ID_MAP_COUNT, "building count: other ID, map", NIVF_SHOW_PARAMS),
	NIVF(A2VRI_HOUSE_OTHER_CLASS_MAP_COUNT, "building count: other class, map", NIVF_SHOW_PARAMS),
	NIVF(A2VRI_HOUSE_OTHER_ID_TOWN_COUNT, "building count: other ID, town", NIVF_SHOW_PARAMS),
	NIVF(A2VRI_HOUSE_OTHER_CLASS_TOWN_COUNT, "building count: other class, town", NIVF_SHOW_PARAMS),
};

class NIHHouse : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return !HouseSpec::Get(GetHouseType(TileIndex{index}))->grf_prop.HasGrfFile(); }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId(GSF_FAKE_TOWNS, GetTownIndex(TileIndex{index})); }
	const void *GetInstance(uint)const override          { return nullptr; }
	const void *GetSpec(uint index) const override       { return HouseSpec::Get(GetHouseType(TileIndex{index})); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_TOWN_NAME, GetTownIndex(TileIndex{index}), TileIndex{index}); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? HouseSpec::Get(GetHouseType(TileIndex{index}))->grf_prop.grfid : 0; }
	std::span<const BadgeID> GetBadges(uint index) const override { return HouseSpec::Get(GetHouseType(TileIndex{index}))->badges; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		TileIndex tile{index};
		HouseResolverObject ro(GetHouseType(tile), tile, Town::GetByTile(tile));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		TileIndex tile{index};
		const HouseSpec *hs = HouseSpec::Get(GetHouseType(tile));
		output.Print("Debug Info:");
		output.buffer.format("  House Type: {}", GetHouseType(tile));
		if (hs->grf_prop.grffile != nullptr) {
			output.buffer.format("  (local ID: {})", hs->grf_prop.local_id);
		}
		output.FinishPrint();

		auto zone_flag = [&](HouseZonesBits zone) -> char {
			if (HasBit(hs->building_availability, zone)) return '0' + zone;
			return '-';
		};
		output.Print("  building_flags: 0x{:X}, zones: {}{}{}{}{}", hs->building_flags,
				zone_flag(HZB_TOWN_EDGE), zone_flag(HZB_TOWN_OUTSKIRT), zone_flag(HZB_TOWN_OUTER_SUBURB), zone_flag(HZB_TOWN_INNER_SUBURB), zone_flag(HZB_TOWN_CENTRE));

		output.Print("  extra_flags: 0x{:X}, ctrl_flags: 0x{:X}", hs->extra_flags, hs->ctrl_flags);
		output.Print("  remove_rating_decrease: {}, minimum_life: {}", hs->remove_rating_decrease, hs->minimum_life);
		output.Print("  population: {}, mail_generation: {}", hs->population, hs->mail_generation);
		output.Print("  animation: frames: {}, status: {}, speed: {}, triggers: 0x{:X}", hs->animation.frames, hs->animation.status, hs->animation.speed, hs->animation.triggers);

		{
			output.buffer.format("  min year: {}", hs->min_year);
			if (hs->max_year < CalTime::MAX_YEAR) {
				output.buffer.format(", max year {}", hs->max_year);
			}
			output.FinishPrint();
		}

		if (GetCleanHouseType(tile) != GetHouseType(tile)) {
			hs = HouseSpec::Get(GetCleanHouseType(tile));
			output.buffer.format("  Untranslated House Type: {}", GetCleanHouseType(tile));
			if (hs->grf_prop.grffile != nullptr) {
				output.buffer.format("  (local ID: {})", hs->grf_prop.local_id);
			}
			output.FinishPrint();
			output.Print("    building_flags: 0x{:X}", hs->building_flags);
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		dumper.DumpSpriteGroup(HouseSpec::Get(GetHouseType(TileIndex{index}))->grf_prop.GetSpriteGroup(), 0);
	}
};

static const NIFeature _nif_house = {
	{},
	_nic_house,
	_niv_house,
	std::make_unique<NIHHouse>(),
};


/*** NewGRF industry tiles ***/

#define NICIT(cb_id, bit) NIC(cb_id, IndustryTileSpec, callback_mask, bit)
static const NICallback _nic_industrytiles[] = {
	NICIT(CBID_INDTILE_ANIM_START_STOP,  std::monostate{}),
	NICIT(CBID_INDTILE_ANIM_NEXT_FRAME,  IndustryTileCallbackMask::AnimationNextFrame),
	NICIT(CBID_INDTILE_ANIMATION_SPEED,  IndustryTileCallbackMask::AnimationSpeed),
	NICIT(CBID_INDTILE_CARGO_ACCEPTANCE, IndustryTileCallbackMask::CargoAcceptance),
	NICIT(CBID_INDTILE_ACCEPT_CARGO,     IndustryTileCallbackMask::AcceptCargo),
	NICIT(CBID_INDTILE_SHAPE_CHECK,      IndustryTileCallbackMask::ShapeCheck),
	NICIT(CBID_INDTILE_DRAW_FOUNDATIONS, IndustryTileCallbackMask::DrawFoundations),
	NICIT(CBID_INDTILE_AUTOSLOPE,        IndustryTileCallbackMask::Autoslope),
};

static const NIVariable _niv_industrytiles[] = {
	NIV(0x40, "construction state of tile"),
	NIV(0x41, "ground type"),
	NIV(0x42, "current town zone in nearest town"),
	NIV(0x43, "relative position"),
	NIV(0x44, "animation frame"),
	NIV(0x60, "land info of nearby tiles"),
	NIV(0x61, "animation stage of nearby tiles"),
	NIV(0x62, "get industry or airport tile ID at offset"),
};

class NIHIndustryTile : public NIHelper {
	bool IsInspectable(uint index) const override        { return GetIndustryTileSpec(GetIndustryGfx(TileIndex{index}))->grf_prop.HasGrfFile(); }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId(GSF_INDUSTRIES, GetIndustryIndex(TileIndex{index})); }
	const void *GetInstance(uint)const override          { return nullptr; }
	const void *GetSpec(uint index) const override       { return GetIndustryTileSpec(GetIndustryGfx(TileIndex{index})); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_INDUSTRY_NAME, GetIndustryIndex(TileIndex{index}), TileIndex{index}); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? GetIndustryTileSpec(GetIndustryGfx(TileIndex{index}))->grf_prop.grfid : 0; }
	std::span<const BadgeID> GetBadges(uint index) const override { return GetIndustryTileSpec(GetIndustryGfx(TileIndex{index}))->badges; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		TileIndex tile{index};
		IndustryTileResolverObject ro(GetIndustryGfx(tile), tile, Industry::GetByTile(tile));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		TileIndex tile{index};
		output.Print("Debug Info:");
		output.Print("  Gfx Index: {}, animated tile: {}", GetIndustryGfx(tile), _animated_tiles.find(tile) != _animated_tiles.end());
		const IndustryTileSpec *indts = GetIndustryTileSpec(GetIndustryGfx(tile));
		if (indts != nullptr) {
			output.Print("  anim_production: {}, anim_next: {}, anim_state: {}, ", indts->anim_production, indts->anim_next, indts->anim_state);
			output.Print("  animation: frames: {}, status: {}, speed: {}, triggers: 0x{:X}", indts->animation.frames, indts->animation.status, indts->animation.speed, indts->animation.triggers);
			output.Print("  special_flags: 0x{:X}, enabled: {}", indts->special_flags, indts->enabled);
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		const IndustryTileSpec *indts = GetIndustryTileSpec(GetIndustryGfx(TileIndex{index}));
		if (indts != nullptr) {
			extern void DumpIndustryTileSpriteGroup(const IndustryTileSpec *spec, SpriteGroupDumper &dumper);
			DumpIndustryTileSpriteGroup(indts, dumper);
		}
	}
};

static const NIFeature _nif_industrytile = {
	{},
	_nic_industrytiles,
	_niv_industrytiles,
	std::make_unique<NIHIndustryTile>(),
};


/*** NewGRF industries ***/
template <typename BASE> uint NipProducedCargoReader(const void *b, uint8_t slot) { return static_cast<const BASE *>(b)->GetProduced(slot).cargo; }
template <typename BASE> uint NipAcceptedCargoReader(const void *b, uint8_t slot) { return static_cast<const BASE *>(b)->GetAccepted(slot).cargo; }
#define NIP_PRODUCED_CARGO(prop, base, slot, type, name) { name, NIValueReader(NipProducedCargoReader<base>, slot), prop, type }
#define NIP_ACCEPTED_CARGO(prop, base, slot, type, name) { name, NIValueReader(NipAcceptedCargoReader<base>, slot), prop, type }

static const NIProperty _nip_industries[] = {
	NIP_PRODUCED_CARGO(0x25, Industry,  0, NIT_CARGO, "produced cargo 0"),
	NIP_PRODUCED_CARGO(0x25, Industry,  1, NIT_CARGO, "produced cargo 1"),
	NIP_PRODUCED_CARGO(0x25, Industry,  2, NIT_CARGO, "produced cargo 2"),
	NIP_PRODUCED_CARGO(0x25, Industry,  3, NIT_CARGO, "produced cargo 3"),
	NIP_PRODUCED_CARGO(0x25, Industry,  4, NIT_CARGO, "produced cargo 4"),
	NIP_PRODUCED_CARGO(0x25, Industry,  5, NIT_CARGO, "produced cargo 5"),
	NIP_PRODUCED_CARGO(0x25, Industry,  6, NIT_CARGO, "produced cargo 6"),
	NIP_PRODUCED_CARGO(0x25, Industry,  7, NIT_CARGO, "produced cargo 7"),
	NIP_PRODUCED_CARGO(0x25, Industry,  8, NIT_CARGO, "produced cargo 8"),
	NIP_PRODUCED_CARGO(0x25, Industry,  9, NIT_CARGO, "produced cargo 9"),
	NIP_PRODUCED_CARGO(0x25, Industry, 10, NIT_CARGO, "produced cargo 10"),
	NIP_PRODUCED_CARGO(0x25, Industry, 11, NIT_CARGO, "produced cargo 11"),
	NIP_PRODUCED_CARGO(0x25, Industry, 12, NIT_CARGO, "produced cargo 12"),
	NIP_PRODUCED_CARGO(0x25, Industry, 13, NIT_CARGO, "produced cargo 13"),
	NIP_PRODUCED_CARGO(0x25, Industry, 14, NIT_CARGO, "produced cargo 14"),
	NIP_PRODUCED_CARGO(0x25, Industry, 15, NIT_CARGO, "produced cargo 15"),
	NIP_ACCEPTED_CARGO(0x26, Industry,  0, NIT_CARGO, "accepted cargo 0"),
	NIP_ACCEPTED_CARGO(0x26, Industry,  1, NIT_CARGO, "accepted cargo 1"),
	NIP_ACCEPTED_CARGO(0x26, Industry,  2, NIT_CARGO, "accepted cargo 2"),
	NIP_ACCEPTED_CARGO(0x26, Industry,  3, NIT_CARGO, "accepted cargo 3"),
	NIP_ACCEPTED_CARGO(0x26, Industry,  4, NIT_CARGO, "accepted cargo 4"),
	NIP_ACCEPTED_CARGO(0x26, Industry,  5, NIT_CARGO, "accepted cargo 5"),
	NIP_ACCEPTED_CARGO(0x26, Industry,  6, NIT_CARGO, "accepted cargo 6"),
	NIP_ACCEPTED_CARGO(0x26, Industry,  7, NIT_CARGO, "accepted cargo 7"),
	NIP_ACCEPTED_CARGO(0x26, Industry,  8, NIT_CARGO, "accepted cargo 8"),
	NIP_ACCEPTED_CARGO(0x26, Industry,  9, NIT_CARGO, "accepted cargo 9"),
	NIP_ACCEPTED_CARGO(0x26, Industry, 10, NIT_CARGO, "accepted cargo 10"),
	NIP_ACCEPTED_CARGO(0x26, Industry, 11, NIT_CARGO, "accepted cargo 11"),
	NIP_ACCEPTED_CARGO(0x26, Industry, 12, NIT_CARGO, "accepted cargo 12"),
	NIP_ACCEPTED_CARGO(0x26, Industry, 13, NIT_CARGO, "accepted cargo 13"),
	NIP_ACCEPTED_CARGO(0x26, Industry, 14, NIT_CARGO, "accepted cargo 14"),
	NIP_ACCEPTED_CARGO(0x26, Industry, 15, NIT_CARGO, "accepted cargo 15"),
};

#define NICI(cb_id, bit) NIC(cb_id, IndustrySpec, callback_mask, bit)
static const NICallback _nic_industries[] = {
	NICI(CBID_INDUSTRY_PROBABILITY,          IndustryCallbackMask::Probability),
	NICI(CBID_INDUSTRY_LOCATION,             IndustryCallbackMask::Location),
	NICI(CBID_INDUSTRY_PRODUCTION_CHANGE,    IndustryCallbackMask::ProductionChange),
	NICI(CBID_INDUSTRY_MONTHLYPROD_CHANGE,   IndustryCallbackMask::MonthlyProdChange),
	NICI(CBID_INDUSTRY_CARGO_SUFFIX,         IndustryCallbackMask::CargoSuffix),
	NICI(CBID_INDUSTRY_FUND_MORE_TEXT,       IndustryCallbackMask::FundMoreText),
	NICI(CBID_INDUSTRY_WINDOW_MORE_TEXT,     IndustryCallbackMask::WindowMoreText),
	NICI(CBID_INDUSTRY_SPECIAL_EFFECT,       IndustryCallbackMask::SpecialEffect),
	NICI(CBID_INDUSTRY_REFUSE_CARGO,         IndustryCallbackMask::RefuseCargo),
	NICI(CBID_INDUSTRY_DECIDE_COLOUR,        IndustryCallbackMask::DecideColour),
	NICI(CBID_INDUSTRY_INPUT_CARGO_TYPES,    IndustryCallbackMask::InputCargoTypes),
	NICI(CBID_INDUSTRY_OUTPUT_CARGO_TYPES,   IndustryCallbackMask::OutputCargoTypes),
	NICI(CBID_INDUSTRY_PROD_CHANGE_BUILD,    IndustryCallbackMask::ProdChangeBuild),
};

static const NIVariable _niv_industries[] = {
	NIV(0x40, "waiting cargo 0"),
	NIV(0x41, "waiting cargo 1"),
	NIV(0x42, "waiting cargo 2"),
	NIV(0x43, "distance to closest dry/land tile"),
	NIV(0x44, "layout number"),
	NIV(0x45, "player info"),
	NIV(0x46, "industry construction date"),
	NIV(0x60, "get industry tile ID at offset"),
	NIV(0x61, "get random tile bits at offset"),
	NIV(0x62, "land info of nearby tiles"),
	NIV(0x63, "animation stage of nearby tiles"),
	NIV(0x64, "distance on nearest industry with given type"),
	NIV(0x65, "get town zone and Manhattan distance of closest town"),
	NIV(0x66, "get square of Euclidean distance of closes town"),
	NIV(0x67, "count of industry and distance of closest instance"),
	NIV(0x68, "count of industry and distance of closest instance with layout filter"),
	NIV(0x69, "produced cargo waiting"),
	NIV(0x6A, "cargo produced this month"),
	NIV(0x6B, "cargo transported this month"),
	NIV(0x6C, "cargo produced last month"),
	NIV(0x6D, "cargo transported last month"),
	NIV(0x6E, "date since cargo was delivered"),
	NIV(0x6F, "waiting input cargo"),
	NIV(0x70, "production rate"),
	NIV(0x71, "percentage of cargo transported last month"),
};

class NIHIndustry : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { return HasBit(index, 26) ? InspectTargetId::Invalid() : GetTownInspectTargetId(Industry::Get(index)->town); }
	const void *GetInstance(uint index)const override    { return HasBit(index, 26) ? nullptr : Industry::Get(index); }
	uint32_t GetGRFID(uint index) const override         { return (!this->ShowExtraInfoOnly(index)) ? ((const IndustrySpec *)this->GetSpec(index))->grf_prop.grfid : 0; }
	std::span<const BadgeID> GetBadges(uint index) const override { return GetIndustrySpec(Industry::Get(index)->type)->badges; }

	bool ShowExtraInfoOnly(uint index) const override
	{
		const IndustrySpec *spec = (const IndustrySpec *)this->GetSpec(index);
		return spec == nullptr || spec->grf_prop.grffile == nullptr;
	}

	bool ShowExtraInfoIncludingGRFIDOnly(uint index) const override
	{
		return HasBit(index, 26);
	}

	const void *GetSpec(uint index) const override
	{
		if (HasBit(index, 26)) {
			return GetIndustrySpec(GB(index, 0, 16));
		} else {
			Industry *i = Industry::Get(index);
			return i != nullptr ? GetIndustrySpec(i->type) : nullptr;
		}
	}

	void SetStringParameters(uint index) const override
	{
		if (HasBit(index, 26)) {
			SetDParam(0, GetIndustrySpec(GB(index, 0, 16))->name);
		} else {
			this->SetSimpleStringParameters(STR_INDUSTRY_NAME, index);
		}
	}

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		Industry *i = Industry::Get(index);
		IndustriesResolverObject ro(i->location.tile, i, i->type);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	const std::span<int32_t> GetPSA(uint index, uint32_t) const override
	{
		const Industry *i = (const Industry *)this->GetInstance(index);
		if (i->psa == nullptr) return {};
		return i->psa->storage;
	}

	std::vector<uint32_t> GetPSAGRFIDs(uint index) const override
	{
		return { 0 };
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		output.Print("Debug Info:");

		if (!HasBit(index, 26)) {
			output.Print("  Index: {}", index);
			const Industry *ind = Industry::GetIfValid(index);
			if (ind) {
				output.Print("  Location: {}x{} ({:X}), w: {}, h: {}", TileX(ind->location.tile), TileY(ind->location.tile), ind->location.tile, ind->location.w, ind->location.h);
				if (ind->neutral_station) {
					output.Print("  Neutral station: {}: {}", ind->neutral_station->index, ind->neutral_station->GetCachedName());
				}
				output.Print("  Nearby stations: {}", ind->stations_near.size());
				for (const Station *st : ind->stations_near) {
					output.Print("    {}: {}", st->index, st->GetCachedName());
				}
				output.Print("  Produces:");
				for (const auto &p : ind->Produced()) {
					if (p.cargo != INVALID_CARGO) {
						output.Print("    {}:", GetStringPtr(CargoSpec::Get(p.cargo)->name));
						output.Print("      Waiting: {}, rate: {}",
								p.waiting, p.rate);
						output.Print("      This month: production: {}, transported: {}",
								p.history[THIS_MONTH].production, p.history[THIS_MONTH].transported);
						output.Print("      Last month: production: {}, transported: {}, ({}/255)",
								p.history[LAST_MONTH].production, p.history[LAST_MONTH].transported, p.history[LAST_MONTH].PctTransported());
					}
				}
				output.Print("  Accepts:");
				for (const auto &a : ind->Accepted()) {
					if (a.cargo != INVALID_CARGO) {
						output.Print("    {}: waiting: {}",
								GetStringPtr(CargoSpec::Get(a.cargo)->name), a.waiting);
					}
				}
				output.Print("  Counter: {}", ind->counter);
			}
		}

		const IndustrySpec *indsp = (const IndustrySpec *)this->GetSpec(index);
		if (indsp) {
			output.Print("  CBM_IND_PRODUCTION_CARGO_ARRIVAL: {}", indsp->callback_mask.Test(IndustryCallbackMask::ProductionCargoArrival) ? "yes" : "no");
			output.Print("  CBM_IND_PRODUCTION_256_TICKS: {}", indsp->callback_mask.Test(IndustryCallbackMask::Production256Ticks) ? "yes" : "no");
			if (_industry_cargo_scaler.HasScaling() && indsp->callback_mask.Test(IndustryCallbackMask::Production256Ticks)) {
				output.Print("  Counter production interval: {}", _industry_inverse_cargo_scaler.Scale(INDUSTRY_PRODUCE_TICKS));
			}
			output.Print("  Number of layouts: {}", indsp->layouts.size());
			for (size_t i = 0; i < indsp->layout_anim_masks.size(); i++) {
				output.Print("  Layout anim inhibit mask {}: {:X}", i, indsp->layout_anim_masks[i]);
			}
			if (indsp->grf_prop.grffile != nullptr) {
				output.Print("  GRF local ID: {}", indsp->grf_prop.local_id);
			}
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		const IndustrySpec *spec = (const IndustrySpec *)this->GetSpec(index);
		if (spec) {
			extern void DumpIndustrySpriteGroup(const IndustrySpec *spec, SpriteGroupDumper &dumper);
			DumpIndustrySpriteGroup(spec, dumper);
		}
	}
};

static const NIFeature _nif_industry = {
	_nip_industries,
	_nic_industries,
	_niv_industries,
	std::make_unique<NIHIndustry>(),
};


/*** NewGRF cargos ***/

#define NICC(cb_id, bit) NIC(cb_id, CargoSpec, callback_mask, bit)
static const NICallback _nic_cargo[] = {
	NICC(CBID_CARGO_PROFIT_CALC,               CargoCallbackMask::ProfitCalc),
	NICC(CBID_CARGO_STATION_RATING_CALC,       CargoCallbackMask::StationRatingCalc),
};

class NIHCargo : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return CargoSpec::Get(index)->grffile == nullptr; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId::Invalid(); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return CargoSpec::Get(index); }
	void SetStringParameters(uint index) const override  { SetDParam(0, CargoSpec::Get(index)->name); }
	uint32_t GetGRFID(uint index) const override         { return (!this->ShowExtraInfoOnly(index)) ? CargoSpec::Get(index)->grffile->grfid : 0; }
	std::span<const BadgeID> GetBadges(uint) const override { return {}; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		return 0;
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		output.Print("Debug Info:");
		output.Print("  Index: {}", index);

		const CargoSpec *spec = CargoSpec::Get(index);
		output.Print("  Bit: {:2}, Label: {}, Callback mask: 0x{:02X}",
				spec->bitnum,
				label_dumper().Label(spec->label.base()),
				spec->callback_mask);
		output.buffer.format("  Cargo class: {}{}{}{}{}{}{}{}{}{}{}",
				(spec->classes & CC_PASSENGERS)   != 0 ? "passenger, " : "",
				(spec->classes & CC_MAIL)         != 0 ? "mail, " : "",
				(spec->classes & CC_EXPRESS)      != 0 ? "express, " : "",
				(spec->classes & CC_ARMOURED)     != 0 ? "armoured, " : "",
				(spec->classes & CC_BULK)         != 0 ? "bulk, " : "",
				(spec->classes & CC_PIECE_GOODS)  != 0 ? "piece goods, " : "",
				(spec->classes & CC_LIQUID)       != 0 ? "liquid, " : "",
				(spec->classes & CC_REFRIGERATED) != 0 ? "refrigerated, " : "",
				(spec->classes & CC_HAZARDOUS)    != 0 ? "hazardous, " : "",
				(spec->classes & CC_COVERED)      != 0 ? "covered/sheltered, " : "",
				(spec->classes & CC_SPECIAL)      != 0 ? "special, " : "");
		std::string_view view = output.buffer;
		if (view.ends_with(", ")) output.buffer.restore_size(output.buffer.size() - 2);
		output.FinishPrint();

		output.Print("  Weight: {}, Capacity multiplier: {}", spec->weight, spec->multiplier);
		output.Print("  Initial payment: {}, Current payment: {}, Transit periods: ({}, {})",
				spec->initial_payment, (int64_t)spec->current_payment, spec->transit_periods[0], spec->transit_periods[1]);
		output.Print("  Freight: {}, Town acceptance effect: {}, Town production effect: {}",
				spec->is_freight ? "yes" : "no", spec->town_acceptance_effect, spec->town_production_effect);
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		dumper.DumpSpriteGroup(CargoSpec::Get(index)->group, 0);
	}
};

static const NIFeature _nif_cargo = {
	{},
	_nic_cargo,
	{},
	std::make_unique<NIHCargo>(),
};


/*** NewGRF signals ***/
void DumpTileSignalsInfo(TileIndex tile, NIExtraInfoOutput &output)
{
	for (Trackdir td = TRACKDIR_BEGIN; td < TRACKDIR_END; td = (Trackdir)(td + 1)) {
		if (!IsValidTrackdir(td)) continue;
		if (HasTrack(tile, TrackdirToTrack(td)) && HasSignalOnTrackdir(tile, td)) {
			const SignalState state = GetSignalStateByTrackdir(tile, td);
			output.buffer.format("  trackdir: {}, state: {}", td, state);
			if (_extra_aspects > 0 && state == SIGNAL_STATE_GREEN) output.buffer.format(", aspect: {}", GetSignalAspect(tile, TrackdirToTrack(td)));
			if (GetSignalAlwaysReserveThrough(tile, TrackdirToTrack(td))) output.buffer.append(", always reserve through");
			if (GetSignalSpecialPropagationFlag(tile, TrackdirToTrack(td))) output.buffer.append(", special propagation flag");
			output.FinishPrint();
		}
	}
}

void DumpTunnelBridgeSignalsInfo(TileIndex tile, NIExtraInfoOutput &output)
{
	if (IsTunnelBridgeSignalSimulationEntrance(tile)) {
		const SignalState state = GetTunnelBridgeEntranceSignalState(tile);
		output.buffer.format("  Entrance: state: {}", state);
		if (_extra_aspects > 0 && state == SIGNAL_STATE_GREEN) output.buffer.format(", aspect: {}", GetTunnelBridgeEntranceSignalAspect(tile));
		output.FinishPrint();
	}
	if (IsTunnelBridgeSignalSimulationExit(tile)) {
		const SignalState state = GetTunnelBridgeExitSignalState(tile);
		output.buffer.format("  Exit: state: {}", state);
		if (_extra_aspects > 0 && state == SIGNAL_STATE_GREEN) output.buffer.format(", aspect: {}", GetTunnelBridgeExitSignalAspect(tile));
		output.FinishPrint();
	}
	if (GetTunnelBridgeSignalSpecialPropagationFlag(tile)) {
		output.Print("  Special propagation flag");
	}
	if (IsTunnelBridgeCombinedNormalShuntSignalStyle(tile)) {
		output.Print("  Combined normal/shunt signal style flag");
	}
	TileIndex end = GetOtherTunnelBridgeEnd(tile);
	extern uint GetTunnelBridgeSignalSimulationSignalCount(TileIndex begin, TileIndex end);
	output.Print("  Spacing: {}, total signals: {}", GetTunnelBridgeSignalSimulationSpacing(tile), GetTunnelBridgeSignalSimulationSignalCount(tile, end));
}

static const NIVariable _niv_signals[] = {
	NIV(0x40, "terrain type"),
	NIV(A2VRI_SIGNALS_SIGNAL_RESTRICTION_INFO, "restriction info"),
	NIV(A2VRI_SIGNALS_SIGNAL_CONTEXT, "context"),
	NIV(A2VRI_SIGNALS_SIGNAL_STYLE, "style"),
	NIV(A2VRI_SIGNALS_SIGNAL_SIDE, "side"),
	NIV(A2VRI_SIGNALS_SIGNAL_VERTICAL_CLEARANCE, "vertical_clearance"),
};

class NIHSignals : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return _new_signals_grfs.empty(); }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId::Invalid(); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_SIGNALS, INVALID_STRING_ID, TileIndex{index}); }
	uint32_t GetGRFID(uint index) const override         { return 0; }
	std::span<const BadgeID> GetBadges(uint) const override { return {}; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		TileIndex tile{index};
		extern TraceRestrictProgram *GetFirstTraceRestrictProgramOnTile(TileIndex t);
		CustomSignalSpriteContext ctx = { CSSC_TRACK };
		uint8_t style = 0;
		uint z = 0;
		if (IsTunnelBridgeWithSignalSimulation(tile)) {
			ctx = { IsTunnelBridgeSignalSimulationEntrance(tile) ? CSSC_TUNNEL_BRIDGE_ENTRANCE : CSSC_TUNNEL_BRIDGE_EXIT };
			if (IsTunnel(tile)) ctx.ctx_flags |= CSSCF_TUNNEL;
			style = GetTunnelBridgeSignalStyle(tile);
			z = GetTunnelBridgeSignalZ(tile, !IsTunnelBridgeSignalSimulationEntrance(tile));
		} else if (IsTileType(tile, MP_RAILWAY) && HasSignals(tile)) {
			TrackBits bits = GetTrackBits(tile);
			do {
				Track track = RemoveFirstTrack(&bits);
				if (HasSignalOnTrack(tile, track)) {
					style = GetSignalStyle(tile, track);
					Trackdir td = TrackToTrackdir(track);
					if (!HasSignalOnTrackdir(tile, td)) td = ReverseTrackdir(td);

					uint x, y;
					GetSignalXYZByTrackdir(tile, td, HasBit(_signal_style_masks.signal_opposite_side, style), x, y, z);
					break;
				}
			} while (bits != TRACK_BIT_NONE);
		}
		NewSignalsResolverObject ro(nullptr, tile, TCX_NORMAL, 0, 0, ctx, style, GetFirstTraceRestrictProgramOnTile(tile), z);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		TileIndex tile{index};
		output.Print("Debug Info:");
		if (IsTileType(tile, MP_RAILWAY) && HasSignals(tile)) {
			output.Print("Signals:");
			DumpTileSignalsInfo(TileIndex{tile}, output);
		}
		if (IsTunnelBridgeWithSignalSimulation(tile)) {
			output.Print("Signals:");
			DumpTunnelBridgeSignalsInfo(TileIndex{tile}, output);
		}
		if (_settings_game.vehicle.train_speed_adaptation) {
			SignalSpeedKey speed_key = { tile, 0, (Trackdir)0 };
			for (auto iter = _signal_speeds.lower_bound(speed_key); iter != _signal_speeds.end() && iter->first.signal_tile == tile; ++iter) {
				const auto &it = *iter;
				output.buffer.format("Speed adaptation: Track: {:X}, last dir: {:X} --> speed: {}",
						it.first.signal_track, it.first.last_passing_train_dir, it.second.train_speed);
				if (it.second.IsOutOfDate()) {
					output.buffer.format(", expired");
				} else {
					output.buffer.format(", expires in {} ticks", (it.second.time_stamp - _state_ticks));
				}
				output.FinishPrint();
			}
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		extern void DumpNewSignalsSpriteGroups(SpriteGroupDumper &dumper);
		DumpNewSignalsSpriteGroups(dumper);
	}

	/* virtual */ bool ShowOptionsDropDown(uint index) const override
	{
		return true;
	}

	/* virtual */ void FillOptionsDropDown(uint index, DropDownList &list) const override
	{
		list.push_back(MakeDropDownListStringItem(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_RAIL_TYPE, 0, !IsTileType(TileIndex{index}, MP_RAILWAY)));
	}

	/* virtual */ void OnOptionsDropdownSelect(uint index, int selected) const override
	{
		switch (selected) {
			case 0:
				ShowNewGRFInspectWindow(GSF_RAILTYPES, index);
				break;
		}
	}
};

static const NIFeature _nif_signals = {
	{},
	{},
	_niv_signals,
	std::make_unique<NIHSignals>(),
};

/*** NewGRF objects ***/

#define NICO(cb_id, bit) NIC(cb_id, ObjectSpec, callback_mask, bit)
static const NICallback _nic_objects[] = {
	NICO(CBID_OBJECT_LAND_SLOPE_CHECK,     ObjectCallbackMask::SlopeCheck),
	NICO(CBID_OBJECT_ANIMATION_NEXT_FRAME, ObjectCallbackMask::AnimationNextFrame),
	NICO(CBID_OBJECT_ANIMATION_START_STOP, std::monostate{}),
	NICO(CBID_OBJECT_ANIMATION_SPEED,      ObjectCallbackMask::AnimationSpeed),
	NICO(CBID_OBJECT_COLOUR,               ObjectCallbackMask::Colour),
	NICO(CBID_OBJECT_FUND_MORE_TEXT,       ObjectCallbackMask::FundMoreText),
	NICO(CBID_OBJECT_AUTOSLOPE,            ObjectCallbackMask::Autoslope),
};

static const NIVariable _niv_objects[] = {
	NIV(0x40, "relative position"),
	NIV(0x41, "tile information"),
	NIV(0x42, "construction date"),
	NIV(0x43, "animation counter"),
	NIV(0x44, "object founder"),
	NIV(0x45, "get town zone and Manhattan distance of closest town"),
	NIV(0x46, "get square of Euclidean distance of closes town"),
	NIV(0x47, "colour"),
	NIV(0x48, "view"),
	NIV(0x60, "get object ID at offset"),
	NIV(0x61, "get random tile bits at offset"),
	NIV(0x62, "land info of nearby tiles"),
	NIV(0x63, "animation stage of nearby tiles"),
	NIV(0x64, "distance on nearest object with given type"),
	NIV(A2VRI_OBJECT_FOUNDATION_SLOPE,        "slope after foundation applied"),
	NIV(A2VRI_OBJECT_FOUNDATION_SLOPE_CHANGE, "slope after foundation applied xor non-foundation slope"),
};

class NIHObject : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return !ObjectSpec::GetByTile(TileIndex{index})->grf_prop.HasGrfFile(); }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { return GetTownInspectTargetId(Object::GetByTile(TileIndex{index})->town); }
	const void *GetInstance(uint index)const override    { return Object::GetByTile(TileIndex{index}); }
	const void *GetSpec(uint index) const override       { return ObjectSpec::GetByTile(TileIndex{index}); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_OBJECT, INVALID_STRING_ID, TileIndex{index}); }
	uint32_t GetGRFID(uint index) const override         { return (!this->ShowExtraInfoOnly(index)) ? ObjectSpec::GetByTile(TileIndex{index})->grf_prop.grfid : 0; }
	std::span<const BadgeID> GetBadges(uint index) const override { return ObjectSpec::GetByTile(TileIndex{index})->badges; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		TileIndex tile{index};
		ObjectResolverObject ro(ObjectSpec::GetByTile(tile), Object::GetByTile(tile), tile);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		TileIndex tile{index};
		output.Print("Debug Info:");
		const ObjectSpec *spec = ObjectSpec::GetByTile(tile);
		if (spec != nullptr) {
			ObjectID id = GetObjectIndex(tile);
			const Object *obj = Object::Get(id);
			output.buffer.format("  tile: {}, type ID: {}", id, GetObjectType(tile));
			if (spec->grf_prop.grffile != nullptr) {
				output.buffer.format("  (local ID: {})", spec->grf_prop.local_id);
			}
			if (spec->class_index != INVALID_OBJECT_CLASS) {
				uint class_id = ObjectClass::Get(spec->class_index)->global_id;
				output.buffer.format(", class ID: {}", label_dumper().Label(class_id));
			}
			output.FinishPrint();

			output.Print("  view: {}, colour: {}, effective foundation: {}", obj->view, obj->colour, GetObjectEffectiveFoundationType(tile));
			if (spec->ctrl_flags.Test(ObjectCtrlFlag::UseLandGround)) {
				output.Print("  ground type: {}, density: {}, counter: {}, water class: {}", GetObjectGroundType(tile), GetObjectGroundDensity(tile), GetObjectGroundCounter(tile), GetWaterClass(tile));
			}
			output.Print("  animation: frames: {}, status: {}, speed: {}, triggers: 0x{:X}", spec->animation.frames, spec->animation.status, spec->animation.speed, spec->animation.triggers);
			output.Print("  size: {}x{}, height: {}, views: {}", GB(spec->size, 4, 4), GB(spec->size, 0, 4), spec->height, spec->views);

			{
				CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(spec->introduction_date);
				output.buffer.format("  intro: {}-{:02}-{:02}",
						ymd.year, ymd.month + 1, ymd.day);
				if (spec->end_of_life_date < CalTime::MAX_DATE) {
					ymd = CalTime::ConvertDateToYMD(spec->end_of_life_date);
					output.buffer.format(", end of life: {}-{:02}-{:02}",
							ymd.year, ymd.month + 1, ymd.day);
				}
				output.FinishPrint();
			}

			output.register_next_line_click_flag_toggle(1);
			output.Print("  [{}] flags: 0x{:X}", output.flags & 1 ? '-' : '+', spec->flags);
			if (output.flags & 1) {
				auto print = [&](const char *name) {
					output.Print("    {}", name);
				};
				auto check_flag = [&](ObjectFlag flag, const char *name) {
					if (spec->flags.Test(flag)) print(name);
				};
				auto check_ctrl_flag = [&](ObjectCtrlFlag flag, const char *name) {
					if (spec->ctrl_flags.Test(flag)) print(name);
				};
				check_flag(ObjectFlag::OnlyInScenedit,   "OnlyInScenedit");
				check_flag(ObjectFlag::CannotRemove,     "CannotRemove");
				check_flag(ObjectFlag::Autoremove,       "Autoremove");
				check_flag(ObjectFlag::BuiltOnWater,     "BuiltOnWater");
				check_flag(ObjectFlag::ClearIncome,      "ClearIncome");
				check_flag(ObjectFlag::HasNoFoundation,  "HasNoFoundation");
				check_flag(ObjectFlag::Animation,        "Animation");
				check_flag(ObjectFlag::OnlyInGame,       "OnlyInGame");
				check_flag(ObjectFlag::Uses2CC,          "Uses2CC");
				check_flag(ObjectFlag::NotOnLand,        "NotOnLand");
				check_flag(ObjectFlag::DrawWater,        "DrawWater");
				check_flag(ObjectFlag::AllowUnderBridge, "AllowUnderBridge");
				check_flag(ObjectFlag::AnimRandomBits,   "AnimRandomBits");
				check_flag(ObjectFlag::ScaleByWater,     "ScaleByWater");
				check_ctrl_flag(ObjectCtrlFlag::UseLandGround,      "UseLandGround");
				check_ctrl_flag(ObjectCtrlFlag::EdgeFoundation,     "EdgeFoundation");
				check_ctrl_flag(ObjectCtrlFlag::FloodResistant,     "FloodResistant");
				check_ctrl_flag(ObjectCtrlFlag::ViewportMapTypeSet, "ViewportMapTypeSet");
			}
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		extern void DumpObjectSpriteGroup(const ObjectSpec *spec, SpriteGroupDumper &dumper);
		DumpObjectSpriteGroup(ObjectSpec::GetByTile(TileIndex{index}), dumper);
	}
};

static const NIFeature _nif_object = {
	{},
	_nic_objects,
	_niv_objects,
	std::make_unique<NIHObject>(),
};


/*** NewGRF rail types ***/

static const NIVariable _niv_railtypes[] = {
	NIV(0x40, "terrain type"),
	NIV(0x41, "enhanced tunnels"),
	NIV(0x42, "level crossing status"),
	NIV(0x43, "construction date"),
	NIV(0x44, "town zone"),
	NIV(0x45, "track types"),
	NIV(A2VRI_RAILTYPE_ADJACENT_CROSSING, "adjacent crossing"),
};

static void PrintTypeLabels(NIExtraInfoOutput &output, const char *prefix, uint32_t label, const uint32_t *alternate_labels, size_t alternate_labels_count)
{
	if (alternate_labels_count > 0) {
		output.buffer.format("{}Alternate labels: ", prefix);
		for (size_t i = 0; i < alternate_labels_count; i++) {
			if (i != 0) output.buffer.append(", ");
			uint32_t l = alternate_labels[i];
			output.buffer.append(label_dumper().Label(l));
		}
		output.FinishPrint();
	}
}

class NIHRailType : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId::Invalid(); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_RAIL_TYPE, INVALID_STRING_ID, TileIndex{index}); }
	uint32_t GetGRFID(uint index) const override         { return 0; }
	std::span<const BadgeID> GetBadges(uint index) const override { return GetRailTypeInfo(GetRailType(TileIndex{index}))->badges; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		/* There is no unique GRFFile for the tile. Multiple GRFs can define different parts of the railtype.
		 * However, currently the NewGRF Debug GUI does not display variables depending on the GRF (like 0x7F) anyway. */
		RailTypeResolverObject ro(nullptr, TileIndex{index}, TCX_NORMAL, RTSG_END);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		TileIndex tile{index};
		RailType primary = GetTileRailType(tile);
		RailType secondary = GetTileSecondaryRailTypeIfValid(tile);

		auto writeRailType = [&](RailType type) {
			const RailTypeInfo *info = GetRailTypeInfo(type);
			output.Print("  Type: {} ({})", type, label_dumper().RailTypeLabel(type));
			output.Print("  Flags: {}{}{}{}{}{}",
					info->flags.Test(RailTypeFlag::Catenary)        ? 'c' : '-',
					info->flags.Test(RailTypeFlag::NoLevelCrossing) ? 'l' : '-',
					info->flags.Test(RailTypeFlag::Hidden)          ? 'h' : '-',
					info->flags.Test(RailTypeFlag::NoSpriteCombine) ? 's' : '-',
					info->flags.Test(RailTypeFlag::Allow90Deg)      ? 'a' : '-',
					info->flags.Test(RailTypeFlag::Disallow90Deg)   ? 'd' : '-');
			output.Print("  Ctrl flags: {}{}{}{}{}",
					info->ctrl_flags.Test(RailTypeCtrlFlag::SigSpriteProgSig)         ? 'p' : '-',
					info->ctrl_flags.Test(RailTypeCtrlFlag::SigSpriteRestrictedSig)   ? 'r' : '-',
					info->ctrl_flags.Test(RailTypeCtrlFlag::NoRealisticBraking)       ? 'b' : '-',
					info->ctrl_flags.Test(RailTypeCtrlFlag::SigSpriteRecolourEnabled) ? 'c' : '-',
					info->ctrl_flags.Test(RailTypeCtrlFlag::SigSpriteNoEntry)         ? 'n' : '-');

			uint bit = 1;
			auto dump_railtypes = [&](const char *name, RailTypes types, RailTypes mark) {
				output.register_next_line_click_flag_toggle(bit);
				output.Print("  [{}] {}: 0x{:X}", (output.flags & bit) ? '-' : '+', name, types);
				if (output.flags & bit) {
					DumpRailTypeList(output, "    ", types, mark);
				}

				bit <<= 1;
			};
			dump_railtypes("Powered", info->powered_railtypes, RAILTYPES_NONE);
			dump_railtypes("Compatible", info->compatible_railtypes, RAILTYPES_NONE);
			dump_railtypes("All compatible", info->all_compatible_railtypes, ~info->compatible_railtypes);

			PrintTypeLabels(output, "  ", info->label, (const uint32_t*) info->alternate_labels.data(), info->alternate_labels.size());
			output.Print("  Cost multiplier: {}/8, Maintenance multiplier: {}/8", info->cost_multiplier, info->maintenance_multiplier);

			CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(info->introduction_date);
			output.Print("  Introduction date: {:4}-{:02}-{:02}", ymd.year, ymd.month + 1, ymd.day);
			output.Print("  Intro required railtypes: 0x{:X}", info->introduction_required_railtypes);
			output.Print("  Intro railtypes: 0x{:X}", info->introduces_railtypes);
		};

		output.Print("Debug Info:");
		writeRailType(primary);
		if (secondary != INVALID_RAILTYPE) {
			writeRailType(secondary);
		}

		if (IsTileType(tile, MP_RAILWAY) && HasSignals(tile)) {
			output.Print("Signals:");
			DumpTileSignalsInfo(tile, output);
		}
		if (IsTileType(tile, MP_RAILWAY) && IsRailDepot(tile)) {
			output.Print("Depot: reserved: {}", HasDepotReservation(tile));
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		extern void DumpRailTypeSpriteGroup(RailType rt, SpriteGroupDumper &dumper);
		DumpRailTypeSpriteGroup(GetTileRailType(TileIndex{index}), dumper);
	}

	/* virtual */ bool ShowOptionsDropDown(uint index) const override
	{
		return true;
	}

	/* virtual */ void FillOptionsDropDown(uint index, DropDownList &list) const override
	{
		TileIndex tile{index};
		list.push_back(MakeDropDownListStringItem(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_ROAD_TYPE, 0, !IsLevelCrossingTile(tile)));
		list.push_back(MakeDropDownListStringItem(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_SIGNALS, 1, !(IsTileType(tile, MP_RAILWAY) && HasSignals(tile))));
	}

	/* virtual */ void OnOptionsDropdownSelect(uint index, int selected) const override
	{
		switch (selected) {
			case 0:
				ShowNewGRFInspectWindow(GSF_ROADTYPES, index);
				break;

			case 1:
				ShowNewGRFInspectWindow(GSF_SIGNALS, index);
				break;
		}
	}
};

static const NIFeature _nif_railtype = {
	{},
	{},
	_niv_railtypes,
	std::make_unique<NIHRailType>(),
};


/*** NewGRF airport tiles ***/

#define NICAT(cb_id, bit) NIC(cb_id, AirportTileSpec, callback_mask, bit)
static const NICallback _nic_airporttiles[] = {
	NICAT(CBID_AIRPTILE_DRAW_FOUNDATIONS, AirportTileCallbackMask::DrawFoundations),
	NICAT(CBID_AIRPTILE_ANIM_START_STOP,  std::monostate{}),
	NICAT(CBID_AIRPTILE_ANIM_NEXT_FRAME,  AirportTileCallbackMask::AnimationNextFrame),
	NICAT(CBID_AIRPTILE_ANIMATION_SPEED,  AirportTileCallbackMask::AnimationSpeed),
};

static const NIVariable _niv_airporttiles[] = {
	NIV(0x41, "ground type"),
	NIV(0x42, "current town zone in nearest town"),
	NIV(0x43, "relative position"),
	NIV(0x44, "animation frame"),
	NIV(0x60, "land info of nearby tiles"),
	NIV(0x61, "animation stage of nearby tiles"),
	NIV(0x62, "get industry or airport tile ID at offset"),
	NIV(A2VRI_AIRPORTTILES_AIRPORT_LAYOUT, "airport layout"),
	NIV(A2VRI_AIRPORTTILES_AIRPORT_ID, "airport local ID"),
};

class NIHAirportTile : public NIHelper {
	bool IsInspectable(uint index) const override        { return AirportTileSpec::Get(GetAirportGfx(TileIndex{index}))->grf_prop.HasGrfFile(); }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId(GSF_AIRPORTS, GetStationIndex(TileIndex{index})); }
	const void *GetInstance(uint)const override          { return nullptr; }
	const void *GetSpec(uint index) const override       { return AirportTileSpec::Get(GetAirportGfx(TileIndex{index})); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, GetStationIndex(TileIndex{index}), TileIndex{index}); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? AirportTileSpec::Get(GetAirportGfx(TileIndex{index}))->grf_prop.grfid : 0; }
	std::span<const BadgeID> GetBadges(uint index) const override { return AirportTileSpec::Get(GetAirportGfx(TileIndex{index}))->badges; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		TileIndex tile{index};
		AirportTileResolverObject ro(AirportTileSpec::GetByTile(tile), tile, Station::GetByTile(tile));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		TileIndex tile{index};
		output.Print("Debug Info:");
		output.Print("  Gfx Index: {}", GetAirportGfx(tile));
		const AirportTileSpec *spec = AirportTileSpec::Get(GetAirportGfx(tile));
		if (spec != nullptr) {
			output.Print("  animation: frames: {}, status: {}, speed: {}, triggers: 0x{:X}", spec->animation.frames, spec->animation.status, spec->animation.speed, spec->animation.triggers);
		}
	}
};

static const NIFeature _nif_airporttile = {
	{},
	_nic_airporttiles,
	_niv_airporttiles,
	std::make_unique<NIHAirportTile>(),
};


/*** NewGRF airports ***/

static const NIVariable _niv_airports[] = {
	NIV(0x40, "Layout number"),
	NIV(0x48, "bitmask of accepted cargoes"),
	NIV(0x60, "amount of cargo waiting"),
	NIV(0x61, "time since last cargo pickup"),
	NIV(0x62, "rating of cargo"),
	NIV(0x63, "time spent on route"),
	NIV(0x64, "information about last vehicle picking cargo up"),
	NIV(0x65, "amount of cargo acceptance"),
	NIV(0x69, "information about cargo accepted in the past"),
	NIV(0xF1, "type of the airport"),
	NIV(0xF6, "airport block status"),
	NIV(0xFA, "built date"),
};

class NIHAirport : public NIHelper {
	bool IsInspectable(uint index) const override        { return AirportSpec::Get(Station::Get(index)->airport.type)->grf_prop.HasGrfFile(); }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId(GSF_FAKE_TOWNS, Station::Get(index)->town->index); }
	const void *GetInstance(uint index)const override    { return Station::Get(index); }
	const void *GetSpec(uint index) const override       { return AirportSpec::Get(Station::Get(index)->airport.type); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, index, Station::Get(index)->airport.tile); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? AirportSpec::Get(Station::Get(index)->airport.type)->grf_prop.grfid : 0; }
	std::span<const BadgeID> GetBadges(uint index) const override { return AirportSpec::Get(Station::Get(index)->airport.type)->badges; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		Station *st = Station::Get(index);
		AirportResolverObject ro(st->airport.tile, st, AirportSpec::Get(st->airport.type), st->airport.layout);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	const std::span<int32_t> GetPSA(uint index, uint32_t) const override
	{
		const Station *st = (const Station *)this->GetInstance(index);
		if (st->airport.psa == nullptr) return {};
		return st->airport.psa->storage;
	}
};

static const NIFeature _nif_airport = {
	{},
	{},
	_niv_airports,
	std::make_unique<NIHAirport>(),
};


/*** NewGRF towns ***/

static const NIVariable _niv_towns[] = {
	NIV(0x40, "larger town effect on this town"),
	NIV(0x41, "town index"),
	NIV(0x82, "population"),
	NIV(0x94, "zone radius 0"),
	NIV(0x96, "zone radius 1"),
	NIV(0x98, "zone radius 2"),
	NIV(0x9A, "zone radius 3"),
	NIV(0x9C, "zone radius 4"),
	NIV(0xB6, "number of buildings"),
	NIV(A2VRI_TOWNS_HOUSE_COUNT, "number of buildings (uncapped)"),
	NIV(A2VRI_TOWNS_POPULATION, "population (uncapped)"),
	NIV(A2VRI_TOWNS_ZONE_0, "zone radius 0 (uncapped)"),
	NIV(A2VRI_TOWNS_ZONE_1, "zone radius 1 (uncapped)"),
	NIV(A2VRI_TOWNS_ZONE_2, "zone radius 2 (uncapped)"),
	NIV(A2VRI_TOWNS_ZONE_3, "zone radius 3 (uncapped)"),
	NIV(A2VRI_TOWNS_ZONE_4, "zone radius 4 (uncapped)"),
	NIV(A2VRI_TOWNS_XY, "town tile xy"),
};

class NIHTown : public NIHelper {
	bool IsInspectable(uint index) const override        { return Town::IsValidID(index); }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId::Invalid(); }
	const void *GetInstance(uint index)const override    { return Town::Get(index); }
	const void *GetSpec(uint) const override             { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetSimpleStringParameters(STR_TOWN_NAME, index); }
	uint32_t GetGRFID(uint index) const override         { return 0; }
	bool PSAWithParameter() const override               { return true; }
	std::span<const BadgeID> GetBadges(uint) const override { return {}; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		TownResolverObject ro(nullptr, Town::Get(index), true);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	const std::span<int32_t> GetPSA(uint index, uint32_t grfid) const override
	{
		Town *t = Town::Get(index);

		for (const auto &it : t->psa_list) {
			if (it->grfid == grfid) return it->storage;
		}

		return {};
	}

	virtual std::vector<uint32_t> GetPSAGRFIDs(uint index) const override
	{
		Town *t = Town::Get(index);

		std::vector<uint32_t> output;
		for (const auto &iter : t->psa_list) {
			output.push_back(iter->grfid);
		}
		return output;
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		const Town *t = Town::Get(index);

		output.Print("Debug Info:");
		output.Print("  Index: {}", index);
		output.Print("  Churches: {}, Stadiums: {}", t->church_count, t->stadium_count);

		output.Print("  Nearby stations: {}", t->stations_near.size());
		for (const Station *st : t->stations_near) {
			output.Print("    {}: {}", st->index, st->GetCachedName());
		}

		output.Print("  Growth rate: {}, Growth Counter: {}, T to Rebuild: {}, Growing: {}, Custom growth: {}",
				t->growth_rate, t->grow_counter, t->time_until_rebuild, HasBit(t->flags, TOWN_IS_GROWING) ? 1 : 0,HasBit(t->flags, TOWN_CUSTOM_GROWTH) ? 1 : 0);

		output.Print("  Road layout: {}", GetStringPtr(STR_CONFIG_SETTING_TOWN_LAYOUT_DEFAULT + t->layout));

		if (t->have_ratings.Any()) {
			output.Print("  Company ratings:");
			for (CompanyID bit : t->have_ratings.IterateSetBits()) {
				output.Print("    {}: {}", bit, t->ratings[bit]);
			}
		}

		for (int tae = TAE_BEGIN; tae != TAE_END; tae++) {
			static constexpr const char *names[] = {
				"NONE",
				"PASSENGERS",
				"MAIL",
				"GOODS",
				"WATER",
				"FOOD",
			};
			static_assert(lengthof(names) == NUM_TAE);

			if (t->goal[tae] == TOWN_GROWTH_WINTER) {
				output.Print("  TAE_{}: TOWN_GROWTH_WINTER", names[tae - TAE_BEGIN]);
			} else if (t->goal[tae] == TOWN_GROWTH_DESERT) {
				output.Print("  TAE_{}: TOWN_GROWTH_DESERT", names[tae - TAE_BEGIN]);
			} else if (t->goal[tae] != 0) {
				output.Print("  TAE_{}: {}", names[tae - TAE_BEGIN], t->goal[tae]);
			}
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		extern void DumpGenericCallbackSpriteGroups(GrfSpecFeature feature, SpriteGroupDumper &dumper);
		DumpGenericCallbackSpriteGroups(GSF_FAKE_TOWNS, dumper);
	}
};

static const NIFeature _nif_town = {
	{},
	{},
	_niv_towns,
	std::make_unique<NIHTown>(),
};

class NIHStationStruct : public NIHelper {
	bool IsInspectable(uint index) const override        { return BaseStation::IsValidID(index); }
	bool ShowExtraInfoOnly(uint index) const override    { return true; }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId::Invalid(); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	std::span<const BadgeID> GetBadges(uint) const override { return {}; }

	void SetStringParameters(uint index) const override
	{
		const BaseStation *bst = BaseStation::GetIfValid(index);
		if (bst != nullptr && !Station::IsExpected(bst)) {
			this->SetSimpleStringParameters(STR_WAYPOINT_NAME, index);
		} else {
			this->SetSimpleStringParameters(STR_STATION_NAME, index);
		}
	}

	uint32_t GetGRFID(uint index) const override         { return 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		return 0;
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		output.Print("Debug Info:");
		output.Print("  Index: {}", index);
		const BaseStation *bst = BaseStation::GetIfValid(index);
		if (!bst) return;
		output.Print("  Tile: {}", bst->xy);
		if (bst->rect.IsEmpty()) {
			output.Print("  rect: empty");
		} else {
			output.Print("  rect: left: {}, right: {}, top: {}, bottom: {}", bst->rect.left, bst->rect.right, bst->rect.top, bst->rect.bottom);
		}
		const Station *st = Station::GetIfValid(index);
		if (st != nullptr) {
			if (st->industry) {
				output.Print("  Neutral industry: {}: {}", st->industry->index, st->industry->GetCachedName().c_str());
			}
			output.Print("  Nearby industries: {}", st->industries_near.size());
			for (const auto &i : st->industries_near) {
				output.Print("    {}: {}, distance: {}", i.industry->index, i.industry->GetCachedName().c_str(), i.distance);
			}
			output.Print("  Station tiles: {}", st->station_tiles);
			output.Print("  Delete counter: {}", st->delete_ctr);
			output.Print("  Docking tiles: {}", st->docking_station.tile);
			output.Print("  Time since: load: {}, unload: {}", st->time_since_load, st->time_since_unload);

			if (st->airport.tile != INVALID_TILE) {
				output.Print("  Airport: type: {} (local: {}), layout: {}, rotation: {}",
						st->airport.type, st->airport.GetSpec()->grf_prop.local_id, st->airport.layout, st->airport.rotation);
			}

			for (const CargoSpec *cs : CargoSpec::Iterate()) {
				const GoodsEntry *ge = &st->goods[cs->Index()];

				if (ge->data == nullptr && ge->status == 0) {
					/* Nothing of note to show */
					continue;
				}

				const StationCargoPacketMap *pkts = ge->data != nullptr ? ge->data->cargo.Packets() : nullptr;

				output.Print("  Goods entry: {}: {}", cs->Index(), GetStringPtr(cs->name));
				output.buffer.format("    Status: {}{}{}{}{}{}{}",
						HasBit(ge->status, GoodsEntry::GES_ACCEPTANCE)       ? 'a' : '-',
						HasBit(ge->status, GoodsEntry::GES_RATING)           ? 'r' : '-',
						HasBit(ge->status, GoodsEntry::GES_EVER_ACCEPTED)    ? 'e' : '-',
						HasBit(ge->status, GoodsEntry::GES_LAST_MONTH)       ? 'l' : '-',
						HasBit(ge->status, GoodsEntry::GES_CURRENT_MONTH)    ? 'c' : '-',
						HasBit(ge->status, GoodsEntry::GES_ACCEPTED_BIGTICK) ? 'b' : '-',
						HasBit(ge->status, GoodsEntry::GES_NO_CARGO_SUPPLY)  ? 'n' : '-');
				if (ge->data != nullptr && ge->data->MayBeRemoved()) output.buffer.append(", (removable)");
				if (ge->data == nullptr) output.buffer.append(", (no data)");
				output.FinishPrint();

				if (ge->HasRating()) {
					output.Print("    Time since pickup: {}", ge->time_since_pickup);
				}
				if (ge->amount_fract > 0) {
					output.Print("    Amount fract: {}", ge->amount_fract);
				}
				if (pkts != nullptr && (pkts->MapSize() > 0 || ge->CargoTotalCount() > 0)) {
					output.Print("    Cargo packets: {}, cargo packet keys: {}, available: {}, reserved: {}",
							pkts->size(), pkts->MapSize(), ge->CargoAvailableCount(), ge->CargoReservedCount());
				}
				if (ge->link_graph != INVALID_LINK_GRAPH) {
					output.Print("    Link graph: {}, node: {}", ge->link_graph, ge->node);
				}
				if (ge->max_waiting_cargo > 0) {
					output.Print("    Max waiting cargo: {}", ge->max_waiting_cargo);
				}
				if (ge->data != nullptr && ge->data->flows.size() > 0) {
					size_t total_shares = 0;
					for (const FlowStat &fs : ge->data->flows.IterateUnordered()) {
						total_shares += fs.size();
					}
					output.Print("    Flows: {}, total shares: {}", ge->data->flows.size(), total_shares);
				}
			}
		}
		const Waypoint *wp = Waypoint::GetIfValid(index);
		if (wp != nullptr) {
			output.register_next_line_click_flag_toggle(1);
			output.Print("  [{}] flags: 0x{:X}", output.flags & 1 ? '-' : '+', wp->waypoint_flags);
			if (output.flags & 1) {
				auto check_flag = [&](WaypointFlags flag, const char *name) {
					if (HasBit(wp->waypoint_flags, flag)) output.Print("    {}", name);
				};
				check_flag(WPF_HIDE_LABEL,   "WPF_HIDE_LABEL");
				check_flag(WPF_ROAD,         "WPF_ROAD");
			}

			output.Print("  road_waypoint_area: tile: {}, width: {}, height: {}",
					wp->road_waypoint_area.tile, wp->road_waypoint_area.w, wp->road_waypoint_area.h);
		}
	}
};

static const NIFeature _nif_station_struct = {
	{},
	{},
	{},
	std::make_unique<NIHStationStruct>(),
};

class NIHTraceRestrict : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return true; }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId::Invalid(); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	std::span<const BadgeID> GetBadges(uint) const override { return {}; }

	void SetStringParameters(uint index) const override
	{
		SetDParam(0, STR_NEWGRF_INSPECT_CAPTION_TRACERESTRICT);
		SetDParam(1, GetTraceRestrictRefIdTileIndex(static_cast<TraceRestrictRefId>(index)));
		SetDParam(2, GetTraceRestrictRefIdTrack(static_cast<TraceRestrictRefId>(index)));
	}

	uint32_t GetGRFID(uint index) const override         { return 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		return 0;
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		TraceRestrictRefId ref = static_cast<TraceRestrictRefId>(index);
		const TraceRestrictProgram *prog = GetTraceRestrictProgram(ref, false);

		if (prog == nullptr) {
			output.Print("No program");
			return;
		}

		output.Print("Index: {}", prog->index);
		output.Print("");

		output.Print("Actions used: 0x{:X}", prog->actions_used_flags);
		auto check_action = [&](TraceRestrictProgramActionsUsedFlags flag, const char *label) {
			if (prog->actions_used_flags & flag) {
				output.Print("  {}", label);
			}
		};
#define CA(f) check_action(TRPAUF_##f, #f);
		CA(PF)
		CA(RESERVE_THROUGH)
		CA(LONG_RESERVE)
		CA(WAIT_AT_PBS)
		CA(SLOT_ACQUIRE)
		CA(SLOT_RELEASE_BACK)
		CA(SLOT_RELEASE_FRONT)
		CA(PBS_RES_END_WAIT)
		CA(PBS_RES_END_SLOT)
		CA(REVERSE_BEHIND)
		CA(SPEED_RESTRICTION)
		CA(TRAIN_NOT_STUCK)
		CA(CHANGE_COUNTER)
		CA(NO_PBS_BACK_PENALTY)
		CA(SLOT_CONDITIONALS)
		CA(SPEED_ADAPTATION)
		CA(PBS_RES_END_SIMULATE)
		CA(RESERVE_THROUGH_ALWAYS)
		CA(CMB_SIGNAL_MODE_CTRL)
		CA(ORDER_CONDITIONALS)
		CA(REVERSE_AT)
#undef CA
		output.Print("");

		output.Print("Ref count: {}", prog->GetReferenceCount());
		for (TraceRestrictRefId ref : prog->GetReferences()) {
			TileIndex tile = GetTraceRestrictRefIdTileIndex(ref);
			output.Print("  {:X} x {:X}, track: {:X}", TileX(tile), TileY(tile), GetTraceRestrictRefIdTrack(ref));
		}
		output.Print("");

		output.Print("Program: items: {}, instructions: {}", prog->items.size(), prog->GetInstructionCount());
		for (auto iter : prog->IterateInstructions()) {
			if (iter.Instruction().IsDoubleItem()) {
				output.Print("  {:08X} {:08X}", iter.Instruction(), iter.Secondary());
			} else {
				output.Print("  {:08X}", iter.Instruction());
			}
		}

		if (prog->texts != nullptr && !prog->texts->labels.empty()) {
			output.Print("");
			output.Print("Labels:");
			size_t i = 0;
			for (const auto &str : prog->texts->labels) {
				if (!str.empty()) output.Print("  {:X}: {}", i, str);
				i++;
			}
		}
	}
};

static const NIFeature _nif_tracerestrict = {
	{},
	{},
	{},
	std::make_unique<NIHTraceRestrict>(),
};

/*** NewGRF road types ***/

static const NIVariable _niv_roadtypes[] = {
	NIV(0x40, "terrain type"),
	NIV(0x41, "enhanced tunnels"),
	NIV(0x42, "level crossing status"),
	NIV(0x43, "construction date"),
	NIV(0x44, "town zone"),
	NIV(0x45, "track types"),
};

class NIHRoadType : public NIHelper {
	const RoadTramType rtt;

public:
	NIHRoadType(RoadTramType rtt) : rtt(rtt) {}

private:
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId::Invalid(); }
	const void *GetInstance(uint index) const override   { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_ROAD_TYPE, INVALID_STRING_ID, TileIndex{index}); }
	uint32_t GetGRFID(uint index) const override         { return 0; }

	std::span<const BadgeID> GetBadges(uint index) const override
	{
		RoadType rt = GetRoadType(TileIndex{index}, this->rtt);
		if (rt == INVALID_ROADTYPE) return {};
		return GetRoadTypeInfo(rt)->badges;
	}

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		/* There is no unique GRFFile for the tile. Multiple GRFs can define different parts of the railtype.
		 * However, currently the NewGRF Debug GUI does not display variables depending on the GRF (like 0x7F) anyway. */
		RoadTypeResolverObject ro(nullptr, TileIndex{index}, TCX_NORMAL, ROTSG_END);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		TileIndex tile{index};
		output.Print("Debug Info:");
		auto writeInfo = [&](RoadTramType rtt) {
			RoadType type = GetRoadType(tile, rtt);
			if (type == INVALID_ROADTYPE) return;

			const RoadTypeInfo* rti = GetRoadTypeInfo(type);
			output.Print("  {} Type: {} ({})", rtt == RTT_TRAM ? "Tram" : "Road", type, label_dumper().RoadTypeLabel(type));
			output.Print("    Flags: {}{}{}{}{}",
					rti->flags.Test(RoadTypeFlag::Catenary)        ? 'c' : '-',
					rti->flags.Test(RoadTypeFlag::NoLevelCrossing) ? 'l' : '-',
					rti->flags.Test(RoadTypeFlag::NoHouses)        ? 'X' : '-',
					rti->flags.Test(RoadTypeFlag::Hidden)          ? 'h' : '-',
					rti->flags.Test(RoadTypeFlag::TownBuild)       ? 'T' : '-');
			output.Print("    Extra Flags:{}{}{}{}",
					rti->extra_flags.Test(RoadTypeExtraFlag::NotAvailableAiGs)   ? 's' : '-',
					rti->extra_flags.Test(RoadTypeExtraFlag::NoTownModification) ? 't' : '-',
					rti->extra_flags.Test(RoadTypeExtraFlag::NoTunnels)          ? 'T' : '-',
					rti->extra_flags.Test(RoadTypeExtraFlag::NoTrainCollision)   ? 'c' : '-');
			output.Print("    Collision mode: {}", rti->collision_mode);

			output.register_next_line_click_flag_toggle((1 << rtt));
			output.Print("    [{}] Powered: 0x{:X}", (output.flags & (1 << rtt)) ? '-' : '+', rti->powered_roadtypes);
			if (output.flags & (1 << rtt)) {
				DumpRoadTypeList(output, "      ", rti->powered_roadtypes);
			}
			PrintTypeLabels(output, "    ", rti->label, (const uint32_t*) rti->alternate_labels.data(), rti->alternate_labels.size());
			output.Print("    Cost multiplier: {}/8, Maintenance multiplier: {}/8", rti->cost_multiplier, rti->maintenance_multiplier);
		};
		writeInfo(RTT_ROAD);
		writeInfo(RTT_TRAM);
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		TileIndex tile{index};
		for (RoadTramType rtt : { RTT_ROAD, RTT_TRAM }) {
			RoadType rt = GetRoadType(tile, rtt);
			if (rt == INVALID_ROADTYPE) continue;

			extern void DumpRoadTypeSpriteGroup(RoadType rt, SpriteGroupDumper &dumper);
			DumpRoadTypeSpriteGroup(rt, dumper);
		}
	}

	/* virtual */ bool ShowOptionsDropDown(uint index) const override
	{
		return true;
	}

	/* virtual */ void FillOptionsDropDown(uint index, DropDownList &list) const override
	{
		list.push_back(MakeDropDownListStringItem(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_RAIL_TYPE, 0, !IsLevelCrossingTile(TileIndex{index})));
	}

	/* virtual */ void OnOptionsDropdownSelect(uint index, int selected) const override
	{
		switch (selected) {
			case 0:
				ShowNewGRFInspectWindow(GSF_RAILTYPES, index);
				break;
		}
	}
};

static const NIFeature _nif_roadtype = {
	{},
	{},
	_niv_roadtypes,
	std::make_unique<NIHRoadType>(RTT_ROAD),
};

static const NIFeature _nif_tramtype = {
	{},
	{},
	_niv_roadtypes,
	std::make_unique<NIHRoadType>(RTT_TRAM),
};

#define NICRS(cb_id, bit) NIC(cb_id, RoadStopSpec, callback_mask, bit)
static const NICallback _nic_roadstops[] = {
	NICRS(CBID_STATION_AVAILABILITY,     RoadStopCallbackMask::Avail),
	NICRS(CBID_STATION_ANIM_START_STOP,  std::monostate{}),
	NICRS(CBID_STATION_ANIM_NEXT_FRAME,  RoadStopCallbackMask::AnimationNextFrame),
	NICRS(CBID_STATION_ANIMATION_SPEED,  RoadStopCallbackMask::AnimationSpeed),
};

static const NIVariable _nif_roadstops[] = {
	NIV(0x40, "view/rotation"),
	NIV(0x41, "stop type"),
	NIV(0x42, "terrain type"),
	NIV(0x43, "road type"),
	NIV(0x44, "tram type"),
	NIV(0x45, "town zone and Manhattan distance of town"),
	NIV(0x46, "square of Euclidean distance of town"),
	NIV(0x47, "player info"),
	NIV(0x48, "bitmask of accepted cargoes"),
	NIV(0x49, "current animation frame"),
	NIV(0x50, "miscellaneous info"),
	NIV(0x60, "amount of cargo waiting"),
	NIV(0x61, "time since last cargo pickup"),
	NIV(0x62, "rating of cargo"),
	NIV(0x63, "time spent on route"),
	NIV(0x64, "information about last vehicle picking cargo up"),
	NIV(0x65, "amount of cargo acceptance"),
	NIV(0x66, "animation frame of nearby tile"),
	NIV(0x67, "land info of nearby tiles"),
	NIV(0x68, "road stop info of nearby tiles"),
	NIV(0x69, "information about cargo accepted in the past"),
	NIV(0x6A, "GRFID of nearby road stop tiles"),
	NIV(0x6B, "road stop ID of nearby tiles"),
	NIVF(A2VRI_ROADSTOP_INFO_NEARBY_TILES_EXT, "road stop info of nearby tiles ext", NIVF_SHOW_PARAMS),
	NIVF(A2VRI_ROADSTOP_INFO_NEARBY_TILES_V2, "road stop info of nearby tiles v2", NIVF_SHOW_PARAMS),
	NIVF(A2VRI_ROADSTOP_ROAD_INFO_NEARBY_TILES, "Road info of nearby plain road tiles", NIVF_SHOW_PARAMS),
};

class NIHRoadStop : public NIHelper {
	bool IsInspectable(uint index) const override        { return GetRoadStopSpec(TileIndex{index}) != nullptr; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { return GetTownInspectTargetId(BaseStation::GetByTile(TileIndex{index})->town); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return GetRoadStopSpec(TileIndex{index}); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, GetStationIndex(TileIndex{index}), TileIndex{index}); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? GetRoadStopSpec(TileIndex{index})->grf_prop.grfid : 0; }
	std::span<const BadgeID> GetBadges(uint index) const override { return this->IsInspectable(index) ? GetRoadStopSpec(TileIndex{index})->badges : std::span<const BadgeID>{}; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		TileIndex tile{index};
		StationGfx view = GetStationGfx(tile);
		RoadStopResolverObject ro(GetRoadStopSpec(tile), BaseStation::GetByTile(tile), tile, INVALID_ROADTYPE, GetStationType(tile), view);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		TileIndex tile{index};
		output.Print("Debug Info:");
		const RoadStopSpec *spec = GetRoadStopSpec(tile);
		if (spec != nullptr) {
			uint class_id = RoadStopClass::Get(spec->class_index)->global_id;
			output.buffer.format("  class ID: {}", label_dumper().Label(class_id));
			if (spec->grf_prop.grffile != nullptr) {
				output.buffer.format("  (local ID: {})", spec->grf_prop.local_id);
			}
			output.FinishPrint();

			output.Print("  spec: stop type: {:X}, draw mode: {:X}, cargo triggers: {:X}", spec->stop_type, spec->draw_mode, spec->cargo_triggers);
			output.Print("  spec: callback mask: {:X}, flags: {:X}, intl flags: {:X}", spec->callback_mask, spec->flags, spec->internal_flags);
			output.Print("  spec: build: {}, clear: {}, height: {}", spec->build_cost_multiplier, spec->clear_cost_multiplier, spec->height);
			output.Print("  animation: frames: {}, status: {}, speed: {}, triggers: 0x{:X}", spec->animation.frames, spec->animation.status, spec->animation.speed, spec->animation.triggers);

			const BaseStation *st = BaseStation::GetByTile(tile);
			output.Print("  road stop: random bits: {:02X}, animation frame: {:02X}", st->GetRoadStopRandomBits(tile), st->GetRoadStopAnimationFrame(tile));
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		TileIndex tile{index};
		extern void DumpRoadStopSpriteGroup(const BaseStation *st, const RoadStopSpec *spec, SpriteGroupDumper &dumper);
		DumpRoadStopSpriteGroup(BaseStation::GetByTile(tile), GetRoadStopSpec(tile), dumper);
	}
};

static const NIFeature _nif_roadstop = {
	{},
	_nic_roadstops,
	_nif_roadstops,
	std::make_unique<NIHRoadStop>(),
};

static const NIVariable _niv_newlandscape[] = {
	NIV(0x40, "terrain type"),
	NIV(0x41, "tile slope"),
	NIV(0x42, "tile height"),
	NIV(0x43, "tile hash"),
	NIV(0x44, "landscape type"),
	NIV(0x45, "ground info"),
	NIV(0x60, "land info of nearby tiles"),
};

class NIHNewLandscape : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return _new_landscape_rocks_grfs.empty(); }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	InspectTargetId GetParent(uint index) const override { return InspectTargetId::Invalid(); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_LAI_CLEAR_DESCRIPTION_ROCKS, INVALID_STRING_ID, TileIndex{index}); }
	uint32_t GetGRFID(uint index) const override         { return 0; }
	std::span<const BadgeID> GetBadges(uint) const override { return {}; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra &extra) const override
	{
		TileIndex tile{index};
		if (!IsTileType(tile, MP_CLEAR)) return 0;

		TileInfo ti;
		ti.x = TileX(tile);
		ti.y = TileY(tile);
		std::tie(ti.tileh, ti.z) = GetTilePixelSlope(tile);
		ti.tile = tile;

		NewLandscapeResolverObject ro(nullptr, &ti, NEW_LANDSCAPE_ROCKS);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		output.Print("New Landscape GRFs:");
		for (const GRFFile *grf : _new_landscape_rocks_grfs) {
			output.Print("  GRF: {:08X}", std::byteswap(grf->grfid));
			output.Print("    Enable rocks recolour: {}, Enable drawing snowy rocks: {}",
					HasBit(grf->new_landscape_ctrl_flags, NLCF_ROCKS_RECOLOUR_ENABLED), HasBit(grf->new_landscape_ctrl_flags, NLCF_ROCKS_DRAW_SNOWY_ENABLED));
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		extern void DumpNewLandscapeRocksSpriteGroups(SpriteGroupDumper &dumper);
		DumpNewLandscapeRocksSpriteGroups(dumper);
	}
};

static const NIFeature _nif_newlandscape = {
	{},
	{},
	_niv_newlandscape,
	std::make_unique<NIHNewLandscape>(),
};

/** Table with all NIFeatures. */
static const NIFeature * const _nifeatures[] = {
	&_nif_vehicle,      // GSF_TRAINS
	&_nif_vehicle,      // GSF_ROADVEHICLES
	&_nif_vehicle,      // GSF_SHIPS
	&_nif_vehicle,      // GSF_AIRCRAFT
	&_nif_station,      // GSF_STATIONS
	nullptr,            // GSF_CANALS (no callbacks/action2 implemented)
	nullptr,            // GSF_BRIDGES (no callbacks/action2)
	&_nif_house,        // GSF_HOUSES
	nullptr,            // GSF_GLOBALVAR (has no "physical" objects)
	&_nif_industrytile, // GSF_INDUSTRYTILES
	&_nif_industry,     // GSF_INDUSTRIES
	&_nif_cargo,        // GSF_CARGOES (has no "physical" objects)
	nullptr,            // GSF_SOUNDFX (has no "physical" objects)
	&_nif_airport,      // GSF_AIRPORTS
	&_nif_signals,      // GSF_SIGNALS
	&_nif_object,       // GSF_OBJECTS
	&_nif_railtype,     // GSF_RAILTYPES
	&_nif_airporttile,  // GSF_AIRPORTTILES
	&_nif_roadtype,     // GSF_ROADTYPES
	&_nif_tramtype,     // GSF_TRAMTYPES
	&_nif_roadstop,     // GSF_ROADSTOPS
	nullptr,            // GSF_BADGES
	&_nif_newlandscape, // GSF_NEWLANDSCAPE
	&_nif_town,         // GSF_FAKE_TOWNS
	&_nif_station_struct,  // GSF_FAKE_STATION_STRUCT
	&_nif_tracerestrict,   // GSF_FAKE_TRACERESTRICT
};
static_assert(lengthof(_nifeatures) == GSF_FAKE_END);
