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
#include "../string_func_extra.h"
#include "../newgrf_extension.h"
#include "../animated_tile.h"
#include "../clear_map.h"
#include "../tunnelbridge.h"
#include "../train_speed_adaptation.h"
#include "../tracerestrict.h"
#include "../newgrf_dump.h"

/* Helper for filling property tables */
#define NIP(prop, base, variable, type, name) { name, (ptrdiff_t)cpp_offsetof(base, variable), cpp_sizeof(base, variable), prop, type }
#define NIP_END() { nullptr, 0, 0, 0, 0 }

/* Helper for filling callback tables */
#define NIC(cb_id, base, variable, bit) { #cb_id, (ptrdiff_t)cpp_offsetof(base, variable), cpp_sizeof(base, variable), bit, cb_id }
#define NIC_END() { nullptr, 0, 0, 0, 0 }

/* Helper for filling variable tables */
#define NIV(var, name) { name, var, NIVF_NONE }
#define NIVF(var, name, flags) { name, var, flags }
#define NIV_END() { nullptr, 0, NIVF_NONE }


static uint GetTownInspectWindowNumber(const Town *town)
{
	if (town == nullptr) return UINT32_MAX;
	return GetInspectWindowNumber(GSF_FAKE_TOWNS, town->index);
}

static bool IsLabelPrintable(uint32_t l)
{
	for (uint i = 0; i < 4; i++) {
		if ((l & 0xFF) < 0x20 || (l & 0xFF) > 0x7F) return false;
		l >>= 8;
	}
	return true;
}

struct label_dumper {
	inline const char *Label(uint32_t label)
	{
		if (IsLabelPrintable(label)) {
			seprintf(this->buffer, lastof(this->buffer), "%c%c%c%c", label >> 24, label >> 16, label >> 8, label);
		} else {
			seprintf(this->buffer, lastof(this->buffer), "0x%08X", BSWAP32(label));
		}
		return this->buffer;
	}

	inline const char *RailTypeLabel(RailType rt)
	{
		return this->Label(GetRailTypeInfo(rt)->label);
	}

	inline const char *RoadTypeLabel(RoadType rt)
	{
		return this->Label(GetRoadTypeInfo(rt)->label);
	}

private:
	char buffer[64];
};

static void DumpRailTypeList(NIExtraInfoOutput &output, const char *prefix, RailTypes rail_types, RailTypes mark = RAILTYPES_NONE)
{
	char buffer[256];
	for (RailType rt = RAILTYPE_BEGIN; rt < RAILTYPE_END; rt++) {
		if (!HasBit(rail_types, rt)) continue;
		const RailTypeInfo *rti = GetRailTypeInfo(rt);
		if (rti->label == 0) continue;

		seprintf(buffer, lastof(buffer), "%s%02u %s%s",
				prefix,
				(uint) rt,
				label_dumper().Label(rti->label),
				HasBit(mark, rt) ? " !!!" : "");
		output.print(buffer);
	}
}

static void DumpRoadTypeList(NIExtraInfoOutput &output, const char *prefix, RoadTypes road_types)
{
	char buffer[256];
	for (RoadType rt = ROADTYPE_BEGIN; rt < ROADTYPE_END; rt++) {
		if (!HasBit(road_types, rt)) continue;
		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
		if (rti->label == 0) continue;

		seprintf(buffer, lastof(buffer), "%s%02u %s %s",
				prefix,
				(uint) rt,
				RoadTypeIsTram(rt) ? "Tram" : "Road",
				label_dumper().Label(rti->label));
		output.print(buffer);
	}
}

/*** NewGRF Vehicles ***/

#define NICV(cb_id, bit) NIC(cb_id, Engine, info.callback_mask, bit)
static const NICallback _nic_vehicles[] = {
	NICV(CBID_VEHICLE_VISUAL_EFFECT,         CBM_VEHICLE_VISUAL_EFFECT),
	NICV(CBID_VEHICLE_LENGTH,                CBM_VEHICLE_LENGTH),
	NICV(CBID_VEHICLE_LOAD_AMOUNT,           CBM_VEHICLE_LOAD_AMOUNT),
	NICV(CBID_VEHICLE_REFIT_CAPACITY,        CBM_VEHICLE_REFIT_CAPACITY),
	NICV(CBID_VEHICLE_ARTIC_ENGINE,          CBM_VEHICLE_ARTIC_ENGINE),
	NICV(CBID_VEHICLE_CARGO_SUFFIX,          CBM_VEHICLE_CARGO_SUFFIX),
	NICV(CBID_TRAIN_ALLOW_WAGON_ATTACH,      CBM_NO_BIT),
	NICV(CBID_VEHICLE_ADDITIONAL_TEXT,       CBM_NO_BIT),
	NICV(CBID_VEHICLE_COLOUR_MAPPING,        CBM_VEHICLE_COLOUR_REMAP),
	NICV(CBID_VEHICLE_START_STOP_CHECK,      CBM_NO_BIT),
	NICV(CBID_VEHICLE_32DAY_CALLBACK,        CBM_NO_BIT),
	NICV(CBID_VEHICLE_SOUND_EFFECT,          CBM_VEHICLE_SOUND_EFFECT),
	NICV(CBID_VEHICLE_AUTOREPLACE_SELECTION, CBM_NO_BIT),
	NICV(CBID_VEHICLE_MODIFY_PROPERTY,       CBM_NO_BIT),
	NICV(CBID_VEHICLE_NAME,                  CBM_VEHICLE_NAME),
	NIC_END()
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
	NIV_END()
};

class NIHVehicle : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return Vehicle::Get(index)->GetGRF() == nullptr; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { const Vehicle *first = Vehicle::Get(index)->First(); return GetInspectWindowNumber(GetGrfSpecFeature(first->type), first->index); }
	const void *GetInstance(uint index)const override    { return Vehicle::Get(index); }
	const void *GetSpec(uint index) const override       { return Vehicle::Get(index)->GetEngine(); }
	void SetStringParameters(uint index) const override  { this->SetSimpleStringParameters(STR_VEHICLE_NAME, Vehicle::Get(index)->First()->index); }
	uint32_t GetGRFID(uint index) const override         { return Vehicle::Get(index)->GetGRFID(); }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		Vehicle *v = Vehicle::Get(index);
		VehicleResolverObject ro(v->engine_type, v, VehicleResolverObject::WO_CACHED);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	/* virtual */ void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{

		Vehicle *v = Vehicle::Get(index);
		output.print("Debug Info:");
		this->VehicleInfo(v, output, true, 0);
		if (v->type == VEH_AIRCRAFT) {
			output.print("");
			output.print("Shadow:");
			this->VehicleInfo(v->Next(), output, false, 8);
			if (v->Next()->Next() != nullptr) {
				output.print("");
				output.print("Rotor:");
				this->VehicleInfo(v->Next()->Next(), output, false, 16);
			}
		}
	}

	void VehicleInfo(Vehicle *v, NIExtraInfoOutput &output, bool show_engine, uint flag_shift) const
	{
		char buffer[1024];
		seprintf(buffer, lastof(buffer), "  Index: %u", v->index);
		output.print(buffer);
		output.register_next_line_click_flag_toggle(1 << flag_shift);
		char *b = buffer;
		if (output.flags & (1 << flag_shift)) {
			b += seprintf(b, lastof(buffer), "  [-] Flags:\n");
			b = v->DumpVehicleFlagsMultiline(b, lastof(buffer), "    ", "  ");
			ProcessLineByLine(buffer, output.print);
			seprintf(buffer, lastof(buffer), "    Tile hash: %s", (v->hash_tile_current != INVALID_TILE) ? "yes" : "no");
			output.print(buffer);
		} else {
			b += seprintf(b, lastof(buffer), "  [+] Flags: ");
			b = v->DumpVehicleFlags(b, lastof(buffer), false);
			output.print(buffer);
		}

		b = buffer + seprintf(buffer, lastof(buffer), "  ");
		b = DumpTileInfo(b, lastof(buffer), v->tile);
		if (buffer[2] == 't') buffer[2] = 'T';
		output.print(buffer);

		TileIndex vtile = TileVirtXY(v->x_pos, v->y_pos);
		if (v->tile != vtile) {
			seprintf(buffer, lastof(buffer), "  VirtXYTile: %X (%u x %u)", vtile, TileX(vtile), TileY(vtile));
			output.print(buffer);
		}
		b = buffer + seprintf(buffer, lastof(buffer), "  Position: %X, %X, %X, Direction: %d", v->x_pos, v->y_pos, v->z_pos, v->direction);
		if (v->type == VEH_TRAIN) seprintf(b, lastof(buffer), ", tile margin: %d", GetTileMarginInFrontOfTrain(Train::From(v)));
		if (v->type == VEH_SHIP) seprintf(b, lastof(buffer), ", rotation: %d", Ship::From(v)->rotation);
		output.print(buffer);

		if (v->IsPrimaryVehicle()) {
			seprintf(buffer, lastof(buffer), "  Order indices: real: %u, implicit: %u, tt: %u, current type: %s",
					v->cur_real_order_index, v->cur_implicit_order_index, v->cur_timetable_order_index, GetOrderTypeName(v->current_order.GetType()));
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  Current order time: (%u, %u mins), current loading time: (%u, %u mins)",
					v->current_order_time, v->current_order_time / _settings_time.ticks_per_minute,
					v->current_loading_time, v->current_loading_time / _settings_time.ticks_per_minute);
			output.print(buffer);
		}
		seprintf(buffer, lastof(buffer), "  Speed: %u, sub-speed: %u, progress: %u, acceleration: %u",
				v->cur_speed, v->subspeed, v->progress, v->acceleration);
		output.print(buffer);
		seprintf(buffer, lastof(buffer), "  Reliability: %u, spd_dec: %u, needs service: %s",
				v->reliability, v->reliability_spd_dec, v->NeedsServicing() ? "yes" : "no");
		output.print(buffer);
		seprintf(buffer, lastof(buffer), "  Breakdown: ctr: %u, delay: %u, since svc: %u, chance: %u",
				v->breakdown_ctr, v->breakdown_delay, v->breakdowns_since_last_service, v->breakdown_chance);
		output.print(buffer);
		seprintf(buffer, lastof(buffer), "  V Cache: max speed: %u, cargo age period: %u, vis effect: %u",
				v->vcache.cached_max_speed, v->vcache.cached_cargo_age_period, v->vcache.cached_vis_effect);
		output.print(buffer);
		if (v->cargo_type != INVALID_CARGO) {
			seprintf(buffer, lastof(buffer), "  V Cargo: type: %u, sub type: %u, cap: %u, transfer: %u, deliver: %u, keep: %u, load: %u",
					v->cargo_type, v->cargo_subtype, v->cargo_cap,
					v->cargo.ActionCount(VehicleCargoList::MTA_TRANSFER), v->cargo.ActionCount(VehicleCargoList::MTA_DELIVER),
					v->cargo.ActionCount(VehicleCargoList::MTA_KEEP), v->cargo.ActionCount(VehicleCargoList::MTA_LOAD));
			output.print(buffer);
		}
		if (BaseStation::IsValidID(v->last_station_visited)) {
			seprintf(buffer, lastof(buffer), "  V Last station visited: %u, %s", v->last_station_visited, BaseStation::Get(v->last_station_visited)->GetCachedName());
			output.print(buffer);
		}
		if (BaseStation::IsValidID(v->last_loading_station)) {
			seprintf(buffer, lastof(buffer), "  V Last loading station: %u, %s", v->last_loading_station, BaseStation::Get(v->last_loading_station)->GetCachedName());
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  V Last loading tick: " OTTD_PRINTF64 " (" OTTD_PRINTF64 ", " OTTD_PRINTF64 " mins ago)",
					v->last_loading_tick.base(), (_state_ticks - v->last_loading_tick).base(), (_state_ticks - v->last_loading_tick).base() / _settings_time.ticks_per_minute);
			output.print(buffer);
		}
		if (v->IsGroundVehicle()) {
			const GroundVehicleCache &gvc = *(v->GetGroundVehicleCache());
			seprintf(buffer, lastof(buffer), "  GV Cache: weight: %u, slope res: %u, max TE: %u, axle res: %u",
					gvc.cached_weight, gvc.cached_slope_resistance, gvc.cached_max_te, gvc.cached_axle_resistance);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  GV Cache: max track speed: %u, power: %u, air drag: %u",
					gvc.cached_max_track_speed, gvc.cached_power, gvc.cached_air_drag);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  GV Cache: total length: %u, veh length: %u",
					gvc.cached_total_length, gvc.cached_veh_length);
			output.print(buffer);
		}
		if (v->type == VEH_TRAIN) {
			const Train *t = Train::From(v);
			seprintf(buffer, lastof(buffer), "  T cache: tilt: %d, speed varies by railtype: %d, curve speed mod: %d, engines: %u",
					(t->tcache.cached_tflags & TCF_TILT) ? 1 : 0, (t->tcache.cached_tflags & TCF_SPD_RAILTYPE) ? 1 : 0, t->tcache.cached_curve_speed_mod, t->tcache.cached_num_engines);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  T cache: RL braking: %d, decel: %u, uncapped decel: %u, centre mass: %u, braking length: %u",
					(t->UsingRealisticBraking()) ? 1 : 0, t->tcache.cached_deceleration, t->tcache.cached_uncapped_decel, t->tcache.cached_centre_mass, t->tcache.cached_braking_length);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  T cache: veh weight: %u, user data: %u, curve speed: %u",
					t->tcache.cached_veh_weight, t->tcache.user_def_data, t->tcache.cached_max_curve_speed);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  Wait counter: %u, rev distance: %u, TBSN: %u",
					t->wait_counter, t->reverse_distance, t->tunnel_bridge_signal_num);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  Speed restriction: %u, signal speed restriction (ATC): %u",
					t->speed_restriction, t->signal_speed_restriction);
			output.print(buffer);

			output.register_next_line_click_flag_toggle(8 << flag_shift);
			seprintf(buffer, lastof(buffer), "  [%c] Railtype: %u (%s), compatible_railtypes: 0x" OTTD_PRINTFHEX64 ", acceleration type: %u",
					(output.flags & (8 << flag_shift)) ? '-' : '+', t->railtype, label_dumper().RailTypeLabel(t->railtype), t->compatible_railtypes, t->GetAccelerationType());
			output.print(buffer);
			if (output.flags & (8 << flag_shift)) {
				DumpRailTypeList(output, "    ", t->compatible_railtypes);
			}

			if (t->vehstatus & VS_CRASHED) {
				seprintf(buffer, lastof(buffer), "  CRASHED: anim pos: %u", t->crash_anim_pos);
				output.print(buffer);
			} else if (t->crash_anim_pos > 0) {
				seprintf(buffer, lastof(buffer), "  Brake heating: %u", t->crash_anim_pos);
				output.print(buffer);
			}
			if (t->lookahead != nullptr) {
				output.print("  Look ahead:");
				const TrainReservationLookAhead &l = *t->lookahead;
				TrainDecelerationStats stats(t, l.cached_zpos);

				auto print_braking_speed = [&](int position, int end_speed, int end_z) {
					if (!t->UsingRealisticBraking()) return;
					extern void LimitSpeedFromLookAhead(int &max_speed, const TrainDecelerationStats &stats, int current_position, int position, int end_speed, int z_delta);
					int speed = INT_MAX;
					LimitSpeedFromLookAhead(speed, stats, l.current_position, position, end_speed, end_z - stats.z_pos);
					if (speed != INT_MAX) {
						b += seprintf(b, lastof(buffer), ", appr speed: %d", speed);
					}
				};

				b = buffer + seprintf(buffer, lastof(buffer), "    Position: current: %d, z: %d, end: %d, remaining: %d", l.current_position, stats.z_pos, l.reservation_end_position, l.reservation_end_position - l.current_position);
				if (l.lookahead_end_position <= l.reservation_end_position) {
					b += seprintf(b, lastof(buffer), ", (lookahead: end: %d, remaining: %d)", l.lookahead_end_position, l.lookahead_end_position - l.current_position);
				}
				if (l.next_extend_position > l.current_position) {
					b += seprintf(b, lastof(buffer), ", next extend position: %d (dist: %d)", l.next_extend_position, l.next_extend_position - l.current_position);
				}
				output.print(buffer);

				const int overall_zpos = t->CalculateOverallZPos();
				seprintf(buffer, lastof(buffer), "    Cached zpos: %u (actual: %u, delta: %d), positions to refresh: %u",
						l.cached_zpos, overall_zpos, (l.cached_zpos - overall_zpos), l.zpos_refresh_remaining);
				output.print(buffer);

				b = buffer + seprintf(buffer, lastof(buffer), "    Reservation ends at %X (%u x %u), trackdir: %02X, z: %d",
						l.reservation_end_tile, TileX(l.reservation_end_tile), TileY(l.reservation_end_tile), l.reservation_end_trackdir, l.reservation_end_z);
				if (HasBit(l.flags, TRLF_DEPOT_END)) {
					print_braking_speed(l.reservation_end_position - TILE_SIZE, _settings_game.vehicle.rail_depot_speed_limit, l.reservation_end_z);
				} else {
					print_braking_speed(l.reservation_end_position, 0, l.reservation_end_z);
				}
				output.print(buffer);

				b = buffer + seprintf(buffer, lastof(buffer), "    TB reserved tiles: %d, flags:", l.tunnel_bridge_reserved_tiles);
				if (HasBit(l.flags, TRLF_TB_EXIT_FREE)) b += seprintf(b, lastof(buffer), "x");
				if (HasBit(l.flags, TRLF_DEPOT_END)) b += seprintf(b, lastof(buffer), "d");
				if (HasBit(l.flags, TRLF_APPLY_ADVISORY)) b += seprintf(b, lastof(buffer), "a");
				if (HasBit(l.flags, TRLF_CHUNNEL)) b += seprintf(b, lastof(buffer), "c");
				output.print(buffer);

				seprintf(buffer, lastof(buffer), "    Items: %u", (uint)l.items.size());
				output.print(buffer);
				for (const TrainReservationLookAheadItem &item : l.items) {
					b = buffer + seprintf(buffer, lastof(buffer), "      Start: %d (dist: %d), end: %d (dist: %d), z: %d, ",
							item.start, item.start - l.current_position, item.end, item.end - l.current_position, item.z_pos);
					switch (item.type) {
						case TRLIT_STATION:
							b += seprintf(b, lastof(buffer), "station: %u, %s", item.data_id, BaseStation::IsValidID(item.data_id) ? BaseStation::Get(item.data_id)->GetCachedName() : "[invalid]");
							if (t->current_order.ShouldStopAtStation(t->last_station_visited, item.data_id, Waypoint::GetIfValid(item.data_id) != nullptr)) {
								extern int PredictStationStoppingLocation(const Train *v, const Order *order, int station_length, DestinationID dest);
								int stop_position = PredictStationStoppingLocation(t, &(t->current_order), item.end - item.start, item.data_id);
								b += seprintf(b, lastof(buffer), ", stop_position: %d", item.start + stop_position);
								print_braking_speed(item.start + stop_position, 0, item.z_pos);
							} else if (t->current_order.IsType(OT_GOTO_WAYPOINT) && t->current_order.GetDestination() == item.data_id && (t->current_order.GetWaypointFlags() & OWF_REVERSE)) {
								print_braking_speed(item.start + t->gcache.cached_total_length, 0, item.z_pos);
							}
							break;
						case TRLIT_REVERSE:
							b += seprintf(b, lastof(buffer), "reverse");
							print_braking_speed(item.start + t->gcache.cached_total_length, 0, item.z_pos);
							break;
						case TRLIT_TRACK_SPEED:
							b += seprintf(b, lastof(buffer), "track speed: %u", item.data_id);
							print_braking_speed(item.start, item.data_id, item.z_pos);
							break;
						case TRLIT_SPEED_RESTRICTION:
							b += seprintf(b, lastof(buffer), "speed restriction: %u", item.data_id);
							if (item.data_id > 0) print_braking_speed(item.start, item.data_id, item.z_pos);
							break;
						case TRLIT_SIGNAL:
							b += seprintf(b, lastof(buffer), "signal: target speed: %u, style: %u, flags:", item.data_id, item.data_aux >> 8);
							if (HasBit(item.data_aux, TRSLAI_NO_ASPECT_INC)) b += seprintf(b, lastof(buffer), "n");
							if (HasBit(item.data_aux, TRSLAI_NEXT_ONLY)) b += seprintf(b, lastof(buffer), "s");
							if (HasBit(item.data_aux, TRSLAI_COMBINED)) b += seprintf(b, lastof(buffer), "c");
							if (HasBit(item.data_aux, TRSLAI_COMBINED_SHUNT)) b += seprintf(b, lastof(buffer), "X");
							if (_settings_game.vehicle.realistic_braking_aspect_limited == TRBALM_ON &&
									(l.lookahead_end_position == item.start || l.lookahead_end_position == item.start + 1)) {
								b += seprintf(b, lastof(buffer), ", lookahead end");
								print_braking_speed(item.start, 0, item.z_pos);
							}
							break;
						case TRLIT_CURVE_SPEED:
							b += seprintf(b, lastof(buffer), "curve speed: %u", item.data_id);
							if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) print_braking_speed(item.start, item.data_id, item.z_pos);

							break;
						case TRLIT_SPEED_ADAPTATION: {
							TileIndex tile = item.data_id;
							uint16_t td = item.data_aux;
							b += seprintf(b, lastof(buffer), "speed adaptation: tile: %X, trackdir: %X", tile, td);
							if (item.end + 1 < l.reservation_end_position) {
								b += seprintf(b, lastof(buffer), " --> %u", GetLowestSpeedTrainAdaptationSpeedAtSignal(tile, td));
							}
							break;
						}
					}
					output.print(buffer);
				}

				seprintf(buffer, lastof(buffer), "    Curves: %u", (uint)l.curves.size());
				output.print(buffer);
				for (const TrainReservationLookAheadCurve &curve : l.curves) {
					seprintf(buffer, lastof(buffer), "      Pos: %d (dist: %d), dir diff: %d", curve.position, curve.position - l.current_position, curve.dir_diff);
					output.print(buffer);
				}
			}
		}
		if (v->type == VEH_ROAD) {
			const RoadVehicle *rv = RoadVehicle::From(v);
			seprintf(buffer, lastof(buffer), "  Overtaking: %u, overtaking_ctr: %u, overtaking threshold: %u",
					rv->overtaking, rv->overtaking_ctr, rv->GetOvertakingCounterThreshold());
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  Speed: %u", rv->cur_speed);
			output.print(buffer);

			b = buffer + seprintf(buffer, lastof(buffer), "  Path cache: ");
			if (rv->cached_path != nullptr) {
				b += seprintf(b, lastof(buffer), "length: %u, layout ctr: %X (current: %X)", (uint)rv->cached_path->size(), rv->cached_path->layout_ctr, _road_layout_change_counter);
				output.print(buffer);
				b = buffer;
				uint idx = rv->cached_path->start;
				for (uint i = 0; i < rv->cached_path->size(); i++) {
					if ((i & 3) == 0) {
						if (b > buffer + 4) output.print(buffer);
						b = buffer + seprintf(buffer, lastof(buffer), "    ");
					} else {
						b += seprintf(b, lastof(buffer), ", ");
					}
					b += seprintf(b, lastof(buffer), "(%ux%u, %X)", TileX(rv->cached_path->tile[idx]), TileY(rv->cached_path->tile[idx]), rv->cached_path->td[idx]);
					idx = (idx + 1) & RV_PATH_CACHE_SEGMENT_MASK;
				}
				if (b > buffer + 4) output.print(buffer);
			} else {
				b += seprintf(b, lastof(buffer), "none");
				output.print(buffer);
			}

			output.register_next_line_click_flag_toggle(8 << flag_shift);
			seprintf(buffer, lastof(buffer), "  [%c] Roadtype: %u (%s), Compatible: 0x" OTTD_PRINTFHEX64,
					(output.flags & (8 << flag_shift)) ? '-' : '+', rv->roadtype, label_dumper().RoadTypeLabel(rv->roadtype), rv->compatible_roadtypes);
			output.print(buffer);
			if (output.flags & (8 << flag_shift)) {
				DumpRoadTypeList(output, "    ", rv->compatible_roadtypes);
			}
		}
		if (v->type == VEH_SHIP) {
			const Ship *s = Ship::From(v);
			seprintf(buffer, lastof(buffer), "  Lost counter: %u",
					s->lost_count);
			output.print(buffer);
			b = buffer + seprintf(buffer, lastof(buffer), "  Path cache: ");
			if (!s->cached_path.empty()) {
				b += seprintf(b, lastof(buffer), "length: %u", (uint)s->cached_path.size());
				output.print(buffer);
				b = buffer;
				uint i = 0;
				for (Trackdir td : s->cached_path) {
					if ((i & 7) == 0) {
						if (b > buffer) output.print(buffer);
						b = buffer + seprintf(buffer, lastof(buffer), "    %X", td);
					} else {
						b += seprintf(b, lastof(buffer), ", %X", td);
					}
					i++;
				}
				if (b > buffer) output.print(buffer);
			} else {
				b += seprintf(b, lastof(buffer), "none");
				output.print(buffer);
			}
		}
		if (v->type == VEH_AIRCRAFT) {
			const Aircraft *a = Aircraft::From(v);
			b = buffer + seprintf(buffer, lastof(buffer), "  Pos: %u, prev pos: %u, state: %u",
					a->pos, a->previous_pos, a->state);
			if (a->IsPrimaryVehicle()) b += seprintf(b, lastof(buffer), " (%s)", AirportMovementStateToString(a->state));
			b += seprintf(b, lastof(buffer), ", flags: 0x%X", a->flags);
			output.print(buffer);
			if (BaseStation::IsValidID(a->targetairport)) {
				seprintf(buffer, lastof(buffer), "  Target airport: %u, %s", a->targetairport, BaseStation::Get(a->targetairport)->GetCachedName());
				output.print(buffer);
			}
		}

		seprintf(buffer, lastof(buffer), "  Cached sprite bounds: (%d, %d) to (%d, %d), offs: (%d, %d)",
				v->sprite_seq_bounds.left, v->sprite_seq_bounds.top, v->sprite_seq_bounds.right, v->sprite_seq_bounds.bottom, v->x_offs, v->y_offs);
		output.print(buffer);

		seprintf(buffer, lastof(buffer), "  Current image cacheable: %s (%X), spritenum: %X",
				v->cur_image_valid_dir != INVALID_DIR ? "yes" : "no", v->cur_image_valid_dir, v->spritenum);
		output.print(buffer);

		if (HasBit(v->vehicle_flags, VF_SEPARATION_ACTIVE)) {
			std::vector<TimetableProgress> progress_array = PopulateSeparationState(v);
			if (!progress_array.empty()) {
				output.print("  Separation state:");
			}
			for (const auto &info : progress_array) {
				b = buffer + seprintf(buffer, lastof(buffer), "    %s [%d, %d, %d], %u, ",
						info.id == v->index ? "*" : " ", info.order_count, info.order_ticks, info.cumulative_ticks, info.id);
				SetDParam(0, info.id);
				b = strecpy(b, GetString(STR_VEHICLE_NAME).c_str(), lastof(buffer), true);
				b += seprintf(b, lastof(buffer), ", lateness: %d", Vehicle::Get(info.id)->lateness_counter);
				output.print(buffer);
			}
		}

		if (v->HasUnbunchingOrder()) {
			output.print("  Unbunching state:");
			for (const Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
				b = buffer + seprintf(buffer, lastof(buffer), "  %s %u (unit %u):", u == v ? "*" : " ", u->index, u->unitnumber);

				if (u->unbunch_state == nullptr) {
					b += seprintf(b, lastof(buffer), " [NO DATA]");
				}
				if (u->vehstatus & (VS_STOPPED | VS_CRASHED)) {
					b += seprintf(b, lastof(buffer), " [STOPPED]");
				}
				output.print(buffer);

				if (u->unbunch_state != nullptr) {
					auto print_tick = [&](StateTicks tick, const char *label) {
						b = buffer + seprintf(buffer, lastof(buffer), "      %s: ", label);
						if (tick != INVALID_STATE_TICKS) {
							if (tick > _state_ticks) {
								b += seprintf(b, lastof(buffer), OTTD_PRINTF64 " (in " OTTD_PRINTF64 " mins)", tick.base(), (tick - _state_ticks).base() / _settings_time.ticks_per_minute);
							} else {
								b += seprintf(b, lastof(buffer), OTTD_PRINTF64 " (" OTTD_PRINTF64 " mins ago)", tick.base(), (_state_ticks - tick).base() / _settings_time.ticks_per_minute);
							}
						} else {
							b += seprintf(b, lastof(buffer), "invalid");
						}
						output.print(buffer);
					};
					print_tick(u->unbunch_state->depot_unbunching_last_departure, "Last unbunch departure");
					print_tick(u->unbunch_state->depot_unbunching_next_departure, "Next unbunch departure");
					seprintf(buffer, lastof(buffer), "      RTT: %d (%d mins)", u->unbunch_state->round_trip_time, u->unbunch_state->round_trip_time / _settings_time.ticks_per_minute);
					output.print(buffer);
				}
			}
		}

		if (show_engine) {
			const Engine *e = Engine::GetIfValid(v->engine_type);
			char *b = buffer + seprintf(buffer, lastof(buffer), "  Engine: %u", v->engine_type);
			if (e->grf_prop.grffile != nullptr) {
				b += seprintf(b, lastof(buffer), " (local ID: %u)", e->grf_prop.local_id);
			}
			if (e->info.variant_id != INVALID_ENGINE) {
				b += seprintf(b, lastof(buffer), ", variant of: %u", e->info.variant_id);
				const Engine *variant_e = Engine::GetIfValid(e->info.variant_id);
				if (variant_e->grf_prop.grffile != nullptr) {
					b += seprintf(b, lastof(buffer), " (local ID: %u)", variant_e->grf_prop.local_id);
				}
			}
			output.print(buffer);

			if (e != nullptr) {
				seprintf(buffer, lastof(buffer), "    Callbacks: 0x%X, CB36 Properties: 0x" OTTD_PRINTFHEX64,
						e->callbacks_used, e->cb36_properties_used);
				output.print(buffer);
				uint64_t cb36_properties = e->cb36_properties_used;
				if (!e->sprite_group_cb36_properties_used.empty()) {
					const SpriteGroup *root_spritegroup = nullptr;
					if (v->IsGroundVehicle()) root_spritegroup = GetWagonOverrideSpriteSet(v->engine_type, v->cargo_type, v->GetGroundVehicleCache()->first_engine);
					if (root_spritegroup == nullptr) {
						CargoID cargo = v->cargo_type;
						assert(cargo < std::size(e->grf_prop.spritegroup));
						root_spritegroup = e->grf_prop.spritegroup[cargo] != nullptr ? e->grf_prop.spritegroup[cargo] : e->grf_prop.spritegroup[SpriteGroupCargo::SG_DEFAULT];
					}
					auto iter = e->sprite_group_cb36_properties_used.find(root_spritegroup);
					if (iter != e->sprite_group_cb36_properties_used.end()) {
						cb36_properties = iter->second;
						seprintf(buffer, lastof(buffer), "    Current sprite group: CB36 Properties: 0x" OTTD_PRINTFHEX64, iter->second);
						output.print(buffer);
					}
				}
				if (cb36_properties != UINT64_MAX) {
					uint64_t props = cb36_properties;
					while (props) {
						PropertyID prop = (PropertyID)FindFirstBit(props);
						props = KillFirstBit(props);
						uint16_t res = GetVehicleProperty(v, prop, CALLBACK_FAILED);
						if (res == CALLBACK_FAILED) {
							seprintf(buffer, lastof(buffer), "      CB36: 0x%X --> FAILED", prop);
						} else {
							seprintf(buffer, lastof(buffer), "      CB36: 0x%X --> 0x%X", prop, res);
						}
						output.print(buffer);
					}
				}
				if (e->refit_capacity_values != nullptr) {
					const EngineRefitCapacityValue *caps = e->refit_capacity_values.get();
					CargoTypes seen = 0;
					while (seen != ALL_CARGOTYPES) {
						seprintf(buffer, lastof(buffer), "    Refit capacity cache: cargoes: 0x" OTTD_PRINTFHEX64 " --> 0x%X", caps->cargoes, caps->capacity);
						output.print(buffer);
						seen |= caps->cargoes;
						caps++;
					}
				}
				CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(e->intro_date);
				CalTime::YearMonthDay base_ymd = CalTime::ConvertDateToYMD(e->info.base_intro);
				seprintf(buffer, lastof(buffer), "    Intro: %4i-%02i-%02i (base: %4i-%02i-%02i), Age: %u, Base life: %u, Durations: %u %u %u (sum: %u)",
						ymd.year.base(), ymd.month + 1, ymd.day, base_ymd.year.base(), base_ymd.month + 1, base_ymd.day,
						e->age, e->info.base_life.base(), e->duration_phase_1, e->duration_phase_2, e->duration_phase_3,
						e->duration_phase_1 + e->duration_phase_2 + e->duration_phase_3);
				output.print(buffer);
				seprintf(buffer, lastof(buffer), "    Reliability: %u, spd_dec: %u (%u), start: %u, max: %u, final: %u",
						e->reliability, e->reliability_spd_dec, e->info.decay_speed, e->reliability_start, e->reliability_max, e->reliability_final);
				output.print(buffer);
				seprintf(buffer, lastof(buffer), "    Cargo type: %u, refit mask: 0x" OTTD_PRINTFHEX64 ", refit cost: %u",
						e->info.cargo_type, e->info.refit_mask, e->info.refit_cost);
				output.print(buffer);
				seprintf(buffer, lastof(buffer), "    Cargo age period: %u, cargo load speed: %u",
						e->info.cargo_age_period, e->info.load_amount);
				output.print(buffer);
				seprintf(buffer, lastof(buffer), "    Company availability: %X, climates: %X",
						e->company_avail, e->info.climates);
				output.print(buffer);

				output.register_next_line_click_flag_toggle(2 << flag_shift);
				if (output.flags & (2 << flag_shift)) {
					seprintf(buffer, lastof(buffer), "    [-] Engine Misc Flags:\n");
					output.print(buffer);
					auto print_bit = [&](int bit, const char *name) {
						if (HasBit(e->info.misc_flags, bit)) {
							seprintf(buffer, lastof(buffer), "      %s\n", name);
							output.print(buffer);
						}
					};
					print_bit(EF_RAIL_TILTS,                  "EF_RAIL_TILTS");
					print_bit(EF_USES_2CC,                    "EF_USES_2CC");
					print_bit(EF_RAIL_IS_MU,                  "EF_RAIL_IS_MU");
					print_bit(EF_RAIL_FLIPS,                  "EF_RAIL_FLIPS");
					print_bit(EF_AUTO_REFIT,                  "EF_AUTO_REFIT");
					print_bit(EF_NO_DEFAULT_CARGO_MULTIPLIER, "EF_NO_DEFAULT_CARGO_MULTIPLIER");
					print_bit(EF_NO_BREAKDOWN_SMOKE,          "EF_NO_BREAKDOWN_SMOKE");
					print_bit(EF_SPRITE_STACK,                "EF_SPRITE_STACK");
				} else {
					seprintf(buffer, lastof(buffer), "    [+] Engine Misc Flags: %c%c%c%c%c%c%c%c",
							HasBit(e->info.misc_flags, EF_RAIL_TILTS)                  ? 't' : '-',
							HasBit(e->info.misc_flags, EF_USES_2CC)                    ? '2' : '-',
							HasBit(e->info.misc_flags, EF_RAIL_IS_MU)                  ? 'm' : '-',
							HasBit(e->info.misc_flags, EF_RAIL_FLIPS)                  ? 'f' : '-',
							HasBit(e->info.misc_flags, EF_AUTO_REFIT)                  ? 'r' : '-',
							HasBit(e->info.misc_flags, EF_NO_DEFAULT_CARGO_MULTIPLIER) ? 'c' : '-',
							HasBit(e->info.misc_flags, EF_NO_BREAKDOWN_SMOKE)          ? 'b' : '-',
							HasBit(e->info.misc_flags, EF_SPRITE_STACK)                ? 's' : '-');
					output.print(buffer);
				}

				if (e->type == VEH_TRAIN) {
					const RailTypeInfo *rti = GetRailTypeInfo(e->u.rail.railtype);
					seprintf(buffer, lastof(buffer), "    Railtype: %u (%s), Compatible: 0x" OTTD_PRINTFHEX64 ", Powered: 0x" OTTD_PRINTFHEX64 ", All compatible: 0x" OTTD_PRINTFHEX64,
							e->u.rail.railtype, label_dumper().RailTypeLabel(e->u.rail.railtype), rti->compatible_railtypes, rti->powered_railtypes, rti->all_compatible_railtypes);
					output.print(buffer);
					static const char *engine_types[] = {
						"SINGLEHEAD",
						"MULTIHEAD",
						"WAGON",
					};
					seprintf(buffer, lastof(buffer), "    Rail veh type: %s, power: %u", engine_types[e->u.rail.railveh_type], e->u.rail.power);
					output.print(buffer);
				}
				if (e->type == VEH_ROAD) {
					output.register_next_line_click_flag_toggle(16 << flag_shift);
					const RoadTypeInfo* rti = GetRoadTypeInfo(e->u.road.roadtype);
					seprintf(buffer, lastof(buffer), "    [%c] Roadtype: %u (%s), Powered: 0x" OTTD_PRINTFHEX64,
							(output.flags & (16 << flag_shift)) ? '-' : '+', e->u.road.roadtype, label_dumper().RoadTypeLabel(e->u.road.roadtype), rti->powered_roadtypes);
					output.print(buffer);
					if (output.flags & (16 << flag_shift)) {
						DumpRoadTypeList(output, "      ", rti->powered_roadtypes);
					}
					seprintf(buffer, lastof(buffer), "    Capacity: %u, Weight: %u, Power: %u, TE: %u, Air drag: %u, Shorten: %u",
							e->u.road.capacity, e->u.road.weight, e->u.road.power, e->u.road.tractive_effort, e->u.road.air_drag, e->u.road.shorten_factor);
					output.print(buffer);
				}
				if (e->type == VEH_SHIP) {
					seprintf(buffer, lastof(buffer), "    Capacity: %u, Max speed: %u, Accel: %u, Ocean speed: %u, Canal speed: %u",
							e->u.ship.capacity, e->u.ship.max_speed, e->u.ship.acceleration, e->u.ship.ocean_speed_frac, e->u.ship.canal_speed_frac);
					output.print(buffer);
				}

				output.register_next_line_click_flag_toggle(4 << flag_shift);
				if (output.flags & (4 << flag_shift)) {
					seprintf(buffer, lastof(buffer), "    [-] Extra Engine Flags:\n");
					output.print(buffer);
					auto print_bit = [&](ExtraEngineFlags flag, const char *name) {
						if ((e->info.extra_flags & flag) != ExtraEngineFlags::None) {
							seprintf(buffer, lastof(buffer), "      %s\n", name);
							output.print(buffer);
						}
					};
					print_bit(ExtraEngineFlags::NoNews,          "NoNews");
					print_bit(ExtraEngineFlags::NoPreview,       "NoPreview");
					print_bit(ExtraEngineFlags::JoinPreview,     "JoinPreview");
					print_bit(ExtraEngineFlags::SyncReliability, "SyncReliability");
				} else {
					seprintf(buffer, lastof(buffer), "    [+] Extra Engine Flags: %c%c%c%c",
							(e->info.extra_flags & ExtraEngineFlags::NoNews)          != ExtraEngineFlags::None ? 'n' : '-',
							(e->info.extra_flags & ExtraEngineFlags::NoPreview)       != ExtraEngineFlags::None ? 'p' : '-',
							(e->info.extra_flags & ExtraEngineFlags::JoinPreview)     != ExtraEngineFlags::None ? 'j' : '-',
							(e->info.extra_flags & ExtraEngineFlags::SyncReliability) != ExtraEngineFlags::None ? 's' : '-');
					output.print(buffer);
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
	nullptr,
	_nic_vehicles,
	_niv_vehicles,
	new NIHVehicle(),
};


/*** NewGRF station (tiles) ***/

#define NICS(cb_id, bit) NIC(cb_id, StationSpec, callback_mask, bit)
static const NICallback _nic_stations[] = {
	NICS(CBID_STATION_AVAILABILITY,     CBM_STATION_AVAIL),
	NICS(CBID_STATION_DRAW_TILE_LAYOUT, CBM_STATION_DRAW_TILE_LAYOUT),
	NICS(CBID_STATION_BUILD_TILE_LAYOUT,CBM_NO_BIT),
	NICS(CBID_STATION_ANIM_START_STOP,  CBM_NO_BIT),
	NICS(CBID_STATION_ANIM_NEXT_FRAME,  CBM_STATION_ANIMATION_NEXT_FRAME),
	NICS(CBID_STATION_ANIMATION_SPEED,  CBM_STATION_ANIMATION_SPEED),
	NICS(CBID_STATION_LAND_SLOPE_CHECK, CBM_STATION_SLOPE_CHECK),
	NIC_END()
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
	NIV_END()
};

class NIHStation : public NIHelper {
	bool IsInspectable(uint index) const override        { return GetStationSpec(index) != nullptr; }
	uint GetParent(uint index) const override            { return GetTownInspectWindowNumber(BaseStation::GetByTile(index)->town); }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return GetStationSpec(index); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, GetStationIndex(index), index); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? GetStationSpec(index)->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		StationResolverObject ro(GetStationSpec(index), BaseStation::GetByTile(index), index, INVALID_RAILTYPE);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	/* virtual */ void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		char buffer[1024];

		const StationSpec *statspec = GetStationSpec(index);
		if (statspec == nullptr) return;

		if (statspec->grf_prop.grffile != nullptr) {
			seprintf(buffer, lastof(buffer), "GRF local ID: %u", statspec->grf_prop.local_id);
			output.print(buffer);
		}

		for (size_t i = 0; i < statspec->renderdata.size(); i++) {
			seprintf(buffer, lastof(buffer), "Tile Layout %u:", (uint)i);
			output.print(buffer);
			const NewGRFSpriteLayout &dts = statspec->renderdata[i];

			const TileLayoutRegisters *registers = dts.registers;
			auto print_reg_info = [&](char *b, uint i, bool is_parent) {
				if (registers == nullptr) {
					output.print(buffer);
					return;
				}
				const TileLayoutRegisters *reg = registers + i;
				if (reg->flags == 0) {
					output.print(buffer);
					return;
				}
				seprintf(b, lastof(buffer), ", register flags: %X", reg->flags);
				output.print(buffer);
				auto log_reg = [&](TileLayoutFlags flag, const char *name, uint8_t flag_reg) {
					if (reg->flags & flag) {
						seprintf(buffer, lastof(buffer), "  %s reg: %X", name, flag_reg);
						output.print(buffer);
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
					seprintf(buffer, lastof(buffer), "  TLF_SPRITE_VAR10 value: %X", reg->sprite_var10);
					output.print(buffer);
				}
				if (reg->flags & TLF_PALETTE_VAR10) {
					seprintf(buffer, lastof(buffer), "  TLF_PALETTE_VAR10 value: %X", reg->palette_var10);
					output.print(buffer);
				}
			};

			char *b = buffer + seprintf(buffer, lastof(buffer), "  ground: (%X, %X)",
					dts.ground.sprite, dts.ground.pal);
			print_reg_info(b, 0, false);

			uint offset = 0; // offset 0 is the ground sprite
			const DrawTileSeqStruct *element;
			foreach_draw_tile_seq(element, dts.seq) {
				offset++;
				char *b = buffer;
				if (element->IsParentSprite()) {
					b += seprintf(buffer, lastof(buffer), "  section: %X, image: (%X, %X), d: (%d, %d, %d), s: (%d, %d, %d)",
							offset, element->image.sprite, element->image.pal,
							element->delta_x, element->delta_y, element->delta_z,
							element->size_x, element->size_y, element->size_z);
				} else {
					b += seprintf(buffer, lastof(buffer), "  section: %X, image: (%X, %X), d: (%d, %d)",
							offset, element->image.sprite, element->image.pal,
							element->delta_x, element->delta_y);
				}
				print_reg_info(b, offset, element->IsParentSprite());
			}
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		extern void DumpStationSpriteGroup(const StationSpec *statspec, BaseStation *st, SpriteGroupDumper &dumper);
		DumpStationSpriteGroup(GetStationSpec(index), BaseStation::GetByTile(index), dumper);
	}
};

static const NIFeature _nif_station = {
	nullptr,
	_nic_stations,
	_niv_stations,
	new NIHStation(),
};


/*** NewGRF house tiles ***/

#define NICH(cb_id, bit) NIC(cb_id, HouseSpec, callback_mask, bit)
static const NICallback _nic_house[] = {
	NICH(CBID_HOUSE_ALLOW_CONSTRUCTION,        CBM_HOUSE_ALLOW_CONSTRUCTION),
	NICH(CBID_HOUSE_ANIMATION_NEXT_FRAME,      CBM_HOUSE_ANIMATION_NEXT_FRAME),
	NICH(CBID_HOUSE_ANIMATION_START_STOP,      CBM_HOUSE_ANIMATION_START_STOP),
	NICH(CBID_HOUSE_CONSTRUCTION_STATE_CHANGE, CBM_HOUSE_CONSTRUCTION_STATE_CHANGE),
	NICH(CBID_HOUSE_COLOUR,                    CBM_HOUSE_COLOUR),
	NICH(CBID_HOUSE_CARGO_ACCEPTANCE,          CBM_HOUSE_CARGO_ACCEPTANCE),
	NICH(CBID_HOUSE_ANIMATION_SPEED,           CBM_HOUSE_ANIMATION_SPEED),
	NICH(CBID_HOUSE_DESTRUCTION,               CBM_HOUSE_DESTRUCTION),
	NICH(CBID_HOUSE_ACCEPT_CARGO,              CBM_HOUSE_ACCEPT_CARGO),
	NICH(CBID_HOUSE_PRODUCE_CARGO,             CBM_HOUSE_PRODUCE_CARGO),
	NICH(CBID_HOUSE_DENY_DESTRUCTION,          CBM_HOUSE_DENY_DESTRUCTION),
	NICH(CBID_HOUSE_WATCHED_CARGO_ACCEPTED,    CBM_NO_BIT),
	NICH(CBID_HOUSE_CUSTOM_NAME,               CBM_NO_BIT),
	NICH(CBID_HOUSE_DRAW_FOUNDATIONS,          CBM_HOUSE_DRAW_FOUNDATIONS),
	NICH(CBID_HOUSE_AUTOSLOPE,                 CBM_HOUSE_AUTOSLOPE),
	NIC_END()
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
	NIV_END()
};

class NIHHouse : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return HouseSpec::Get(GetHouseType(index))->grf_prop.grffile == nullptr; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, GetTownIndex(index)); }
	const void *GetInstance(uint)const override          { return nullptr; }
	const void *GetSpec(uint index) const override       { return HouseSpec::Get(GetHouseType(index)); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_TOWN_NAME, GetTownIndex(index), index); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? HouseSpec::Get(GetHouseType(index))->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		HouseResolverObject ro(GetHouseType(index), index, Town::GetByTile(index));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		const HouseSpec *hs = HouseSpec::Get(GetHouseType(index));
		char buffer[1024];
		output.print("Debug Info:");
		char *b = buffer + seprintf(buffer, lastof(buffer), "  House Type: %u", GetHouseType(index));
		if (hs->grf_prop.grffile != nullptr) {
			b += seprintf(b, lastof(buffer), "  (local ID: %u)", hs->grf_prop.local_id);
		}
		output.print(buffer);
		seprintf(buffer, lastof(buffer), "  building_flags: 0x%X", hs->building_flags);
		output.print(buffer);
		seprintf(buffer, lastof(buffer), "  extra_flags: 0x%X, ctrl_flags: 0x%X", hs->extra_flags, hs->ctrl_flags);
		output.print(buffer);
		seprintf(buffer, lastof(buffer), "  remove_rating_decrease: %u, minimum_life: %u", hs->remove_rating_decrease, hs->minimum_life);
		output.print(buffer);
		seprintf(buffer, lastof(buffer), "  population: %u, mail_generation: %u", hs->population, hs->mail_generation);
		output.print(buffer);
		seprintf(buffer, lastof(buffer), "  animation: frames: %u, status: %u, speed: %u, triggers: 0x%X", hs->animation.frames, hs->animation.status, hs->animation.speed, hs->animation.triggers);
		output.print(buffer);

		{
			char *b = buffer + seprintf(buffer, lastof(buffer), "  min year: %d", hs->min_year.base());
			if (hs->max_year < CalTime::MAX_YEAR) {
				seprintf(b, lastof(buffer), ", max year %d", hs->max_year.base());
			}
			output.print(buffer);
		}

		if (GetCleanHouseType(index) != GetHouseType(index)) {
			hs = HouseSpec::Get(GetCleanHouseType(index));
			b = buffer + seprintf(buffer, lastof(buffer), "  Untranslated House Type: %u", GetCleanHouseType(index));
			if (hs->grf_prop.grffile != nullptr) {
				b += seprintf(b, lastof(buffer), "  (local ID: %u)", hs->grf_prop.local_id);
			}
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "    building_flags: 0x%X", hs->building_flags);
			output.print(buffer);
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		dumper.DumpSpriteGroup(HouseSpec::Get(GetHouseType(index))->grf_prop.spritegroup[0], 0);
	}
};

static const NIFeature _nif_house = {
	nullptr,
	_nic_house,
	_niv_house,
	new NIHHouse(),
};


/*** NewGRF industry tiles ***/

#define NICIT(cb_id, bit) NIC(cb_id, IndustryTileSpec, callback_mask, bit)
static const NICallback _nic_industrytiles[] = {
	NICIT(CBID_INDTILE_ANIM_START_STOP,  CBM_NO_BIT),
	NICIT(CBID_INDTILE_ANIM_NEXT_FRAME,  CBM_INDT_ANIM_NEXT_FRAME),
	NICIT(CBID_INDTILE_ANIMATION_SPEED,  CBM_INDT_ANIM_SPEED),
	NICIT(CBID_INDTILE_CARGO_ACCEPTANCE, CBM_INDT_CARGO_ACCEPTANCE),
	NICIT(CBID_INDTILE_ACCEPT_CARGO,     CBM_INDT_ACCEPT_CARGO),
	NICIT(CBID_INDTILE_SHAPE_CHECK,      CBM_INDT_SHAPE_CHECK),
	NICIT(CBID_INDTILE_DRAW_FOUNDATIONS, CBM_INDT_DRAW_FOUNDATIONS),
	NICIT(CBID_INDTILE_AUTOSLOPE,        CBM_INDT_AUTOSLOPE),
	NIC_END()
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
	NIV_END()
};

class NIHIndustryTile : public NIHelper {
	bool IsInspectable(uint index) const override        { return GetIndustryTileSpec(GetIndustryGfx(index))->grf_prop.grffile != nullptr; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_INDUSTRIES, GetIndustryIndex(index)); }
	const void *GetInstance(uint)const override          { return nullptr; }
	const void *GetSpec(uint index) const override       { return GetIndustryTileSpec(GetIndustryGfx(index)); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_INDUSTRY_NAME, GetIndustryIndex(index), index); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? GetIndustryTileSpec(GetIndustryGfx(index))->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		IndustryTileResolverObject ro(GetIndustryGfx(index), index, Industry::GetByTile(index));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		char buffer[1024];
		output.print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  Gfx Index: %u, animated tile: %d", GetIndustryGfx(index), _animated_tiles.find(index) != _animated_tiles.end());
		output.print(buffer);
		const IndustryTileSpec *indts = GetIndustryTileSpec(GetIndustryGfx(index));
		if (indts) {
			seprintf(buffer, lastof(buffer), "  anim_production: %u, anim_next: %u, anim_state: %u, ", indts->anim_production, indts->anim_next, indts->anim_state);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  animation: frames: %u, status: %u, speed: %u, triggers: 0x%X", indts->animation.frames, indts->animation.status, indts->animation.speed, indts->animation.triggers);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  special_flags: 0x%X, enabled: %u", indts->special_flags, indts->enabled);
			output.print(buffer);
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		const IndustryTileSpec *indts = GetIndustryTileSpec(GetIndustryGfx(index));
		if (indts) {
			extern void DumpIndustryTileSpriteGroup(const IndustryTileSpec *spec, SpriteGroupDumper &dumper);
			DumpIndustryTileSpriteGroup(indts, dumper);
		}
	}
};

static const NIFeature _nif_industrytile = {
	nullptr,
	_nic_industrytiles,
	_niv_industrytiles,
	new NIHIndustryTile(),
};


/*** NewGRF industries ***/

static const NIProperty _nip_industries[] = {
	NIP(0x25, Industry, produced_cargo[ 0], NIT_CARGO, "produced cargo 0"),
	NIP(0x25, Industry, produced_cargo[ 1], NIT_CARGO, "produced cargo 1"),
	NIP(0x25, Industry, produced_cargo[ 2], NIT_CARGO, "produced cargo 2"),
	NIP(0x25, Industry, produced_cargo[ 3], NIT_CARGO, "produced cargo 3"),
	NIP(0x25, Industry, produced_cargo[ 4], NIT_CARGO, "produced cargo 4"),
	NIP(0x25, Industry, produced_cargo[ 5], NIT_CARGO, "produced cargo 5"),
	NIP(0x25, Industry, produced_cargo[ 6], NIT_CARGO, "produced cargo 6"),
	NIP(0x25, Industry, produced_cargo[ 7], NIT_CARGO, "produced cargo 7"),
	NIP(0x25, Industry, produced_cargo[ 8], NIT_CARGO, "produced cargo 8"),
	NIP(0x25, Industry, produced_cargo[ 9], NIT_CARGO, "produced cargo 9"),
	NIP(0x25, Industry, produced_cargo[10], NIT_CARGO, "produced cargo 10"),
	NIP(0x25, Industry, produced_cargo[11], NIT_CARGO, "produced cargo 11"),
	NIP(0x25, Industry, produced_cargo[12], NIT_CARGO, "produced cargo 12"),
	NIP(0x25, Industry, produced_cargo[13], NIT_CARGO, "produced cargo 13"),
	NIP(0x25, Industry, produced_cargo[14], NIT_CARGO, "produced cargo 14"),
	NIP(0x25, Industry, produced_cargo[15], NIT_CARGO, "produced cargo 15"),
	NIP(0x26, Industry, accepts_cargo[ 0],  NIT_CARGO, "accepted cargo 0"),
	NIP(0x26, Industry, accepts_cargo[ 1],  NIT_CARGO, "accepted cargo 1"),
	NIP(0x26, Industry, accepts_cargo[ 2],  NIT_CARGO, "accepted cargo 2"),
	NIP(0x26, Industry, accepts_cargo[ 3],  NIT_CARGO, "accepted cargo 3"),
	NIP(0x26, Industry, accepts_cargo[ 4],  NIT_CARGO, "accepted cargo 4"),
	NIP(0x26, Industry, accepts_cargo[ 5],  NIT_CARGO, "accepted cargo 5"),
	NIP(0x26, Industry, accepts_cargo[ 6],  NIT_CARGO, "accepted cargo 6"),
	NIP(0x26, Industry, accepts_cargo[ 7],  NIT_CARGO, "accepted cargo 7"),
	NIP(0x26, Industry, accepts_cargo[ 8],  NIT_CARGO, "accepted cargo 8"),
	NIP(0x26, Industry, accepts_cargo[ 9],  NIT_CARGO, "accepted cargo 9"),
	NIP(0x26, Industry, accepts_cargo[10],  NIT_CARGO, "accepted cargo 10"),
	NIP(0x26, Industry, accepts_cargo[11],  NIT_CARGO, "accepted cargo 11"),
	NIP(0x26, Industry, accepts_cargo[12],  NIT_CARGO, "accepted cargo 12"),
	NIP(0x26, Industry, accepts_cargo[13],  NIT_CARGO, "accepted cargo 13"),
	NIP(0x26, Industry, accepts_cargo[14],  NIT_CARGO, "accepted cargo 14"),
	NIP(0x26, Industry, accepts_cargo[15],  NIT_CARGO, "accepted cargo 15"),
	NIP_END()
};

#define NICI(cb_id, bit) NIC(cb_id, IndustrySpec, callback_mask, bit)
static const NICallback _nic_industries[] = {
	NICI(CBID_INDUSTRY_PROBABILITY,          CBM_IND_PROBABILITY),
	NICI(CBID_INDUSTRY_LOCATION,             CBM_IND_LOCATION),
	NICI(CBID_INDUSTRY_PRODUCTION_CHANGE,    CBM_IND_PRODUCTION_CHANGE),
	NICI(CBID_INDUSTRY_MONTHLYPROD_CHANGE,   CBM_IND_MONTHLYPROD_CHANGE),
	NICI(CBID_INDUSTRY_CARGO_SUFFIX,         CBM_IND_CARGO_SUFFIX),
	NICI(CBID_INDUSTRY_FUND_MORE_TEXT,       CBM_IND_FUND_MORE_TEXT),
	NICI(CBID_INDUSTRY_WINDOW_MORE_TEXT,     CBM_IND_WINDOW_MORE_TEXT),
	NICI(CBID_INDUSTRY_SPECIAL_EFFECT,       CBM_IND_SPECIAL_EFFECT),
	NICI(CBID_INDUSTRY_REFUSE_CARGO,         CBM_IND_REFUSE_CARGO),
	NICI(CBID_INDUSTRY_DECIDE_COLOUR,        CBM_IND_DECIDE_COLOUR),
	NICI(CBID_INDUSTRY_INPUT_CARGO_TYPES,    CBM_IND_INPUT_CARGO_TYPES),
	NICI(CBID_INDUSTRY_OUTPUT_CARGO_TYPES,   CBM_IND_OUTPUT_CARGO_TYPES),
	NICI(CBID_INDUSTRY_PROD_CHANGE_BUILD,    CBM_IND_PROD_CHANGE_BUILD),
	NIC_END()
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
	NIV_END()
};

class NIHIndustry : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { return HasBit(index, 26) ? UINT32_MAX : GetTownInspectWindowNumber(Industry::Get(index)->town); }
	const void *GetInstance(uint index)const override    { return HasBit(index, 26) ? nullptr : Industry::Get(index); }
	uint32_t GetGRFID(uint index) const override         { return (!this->ShowExtraInfoOnly(index)) ? ((const IndustrySpec *)this->GetSpec(index))->grf_prop.grffile->grfid : 0; }

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

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
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
		char buffer[1024];
		output.print("Debug Info:");

		if (!HasBit(index, 26)) {
			seprintf(buffer, lastof(buffer), "  Index: %u", index);
			output.print(buffer);
			const Industry *ind = Industry::GetIfValid(index);
			if (ind) {
				seprintf(buffer, lastof(buffer), "  Location: %ux%u (%X), w: %u, h: %u", TileX(ind->location.tile), TileY(ind->location.tile), ind->location.tile, ind->location.w, ind->location.h);
				output.print(buffer);
				if (ind->neutral_station) {
					seprintf(buffer, lastof(buffer), "  Neutral station: %u: %s", ind->neutral_station->index, ind->neutral_station->GetCachedName());
					output.print(buffer);
				}
				seprintf(buffer, lastof(buffer), "  Nearby stations: %u", (uint) ind->stations_near.size());
				output.print(buffer);
				for (const Station *st : ind->stations_near) {
					seprintf(buffer, lastof(buffer), "    %u: %s", st->index, st->GetCachedName());
					output.print(buffer);
				}
				output.print("  Produces:");
				for (uint i = 0; i < std::size(ind->produced_cargo); i++) {
					if (ind->produced_cargo[i] != INVALID_CARGO) {
						seprintf(buffer, lastof(buffer), "    %s:", GetStringPtr(CargoSpec::Get(ind->produced_cargo[i])->name));
						output.print(buffer);
						seprintf(buffer, lastof(buffer), "      Waiting: %u, rate: %u",
								ind->produced_cargo_waiting[i], ind->production_rate[i]);
						output.print(buffer);
						seprintf(buffer, lastof(buffer), "      This month: production: %u, transported: %u",
								ind->this_month_production[i], ind->this_month_transported[i]);
						output.print(buffer);
						seprintf(buffer, lastof(buffer), "      Last month: production: %u, transported: %u, (%u/255)",
								ind->last_month_production[i], ind->last_month_transported[i], ind->last_month_pct_transported[i]);
						output.print(buffer);
					}
				}
				output.print("  Accepts:");
				for (uint i = 0; i < std::size(ind->accepts_cargo); i++) {
					if (ind->accepts_cargo[i] != INVALID_CARGO) {
						seprintf(buffer, lastof(buffer), "    %s: waiting: %u",
								GetStringPtr(CargoSpec::Get(ind->accepts_cargo[i])->name), ind->incoming_cargo_waiting[i]);
						output.print(buffer);
					}
				}
				seprintf(buffer, lastof(buffer), "  Counter: %u", ind->counter);
				output.print(buffer);
			}
		}

		const IndustrySpec *indsp = (const IndustrySpec *)this->GetSpec(index);
		if (indsp) {
			seprintf(buffer, lastof(buffer), "  CBM_IND_PRODUCTION_CARGO_ARRIVAL: %s", HasBit(indsp->callback_mask, CBM_IND_PRODUCTION_CARGO_ARRIVAL) ? "yes" : "no");
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  CBM_IND_PRODUCTION_256_TICKS: %s", HasBit(indsp->callback_mask, CBM_IND_PRODUCTION_256_TICKS) ? "yes" : "no");
			output.print(buffer);
			if (_industry_cargo_scaler.HasScaling() && HasBit(indsp->callback_mask, CBM_IND_PRODUCTION_256_TICKS)) {
				seprintf(buffer, lastof(buffer), "  Counter production interval: %u", _industry_inverse_cargo_scaler.Scale(INDUSTRY_PRODUCE_TICKS));
				output.print(buffer);
			}
			seprintf(buffer, lastof(buffer), "  Number of layouts: %u", (uint)indsp->layouts.size());
			output.print(buffer);
			for (size_t i = 0; i < indsp->layout_anim_masks.size(); i++) {
				seprintf(buffer, lastof(buffer), "  Layout anim inhibit mask %u: " OTTD_PRINTFHEX64, (uint)i, indsp->layout_anim_masks[i]);
				output.print(buffer);
			}
			if (indsp->grf_prop.grffile != nullptr) {
				seprintf(buffer, lastof(buffer), "  GRF local ID: %u", indsp->grf_prop.local_id);
				output.print(buffer);
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
	new NIHIndustry(),
};


/*** NewGRF cargos ***/

#define NICC(cb_id, bit) NIC(cb_id, CargoSpec, callback_mask, bit)
static const NICallback _nic_cargo[] = {
	NICC(CBID_CARGO_PROFIT_CALC,               CBM_CARGO_PROFIT_CALC),
	NICC(CBID_CARGO_STATION_RATING_CALC,       CBM_CARGO_STATION_RATING_CALC),
	NIC_END()
};

class NIHCargo : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return CargoSpec::Get(index)->grffile == nullptr; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return CargoSpec::Get(index); }
	void SetStringParameters(uint index) const override  { SetDParam(0, CargoSpec::Get(index)->name); }
	uint32_t GetGRFID(uint index) const override         { return (!this->ShowExtraInfoOnly(index)) ? CargoSpec::Get(index)->grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		return 0;
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		char buffer[1024];

		output.print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  Index: %u", index);
		output.print(buffer);

		const CargoSpec *spec = CargoSpec::Get(index);
		seprintf(buffer, lastof(buffer), "  Bit: %2u, Label: %c%c%c%c, Callback mask: 0x%02X",
				spec->bitnum,
				spec->label.base() >> 24, spec->label.base() >> 16, spec->label.base() >> 8, spec->label.base(),
				spec->callback_mask);
		output.print(buffer);
		int written = seprintf(buffer, lastof(buffer), "  Cargo class: %s%s%s%s%s%s%s%s%s%s%s",
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
		if (written >= 2 && buffer[written - 2] == ',') buffer[written - 2] = 0;
		output.print(buffer);

		seprintf(buffer, lastof(buffer), "  Weight: %u, Capacity multiplier: %u", spec->weight, spec->multiplier);
		output.print(buffer);
		seprintf(buffer, lastof(buffer), "  Initial payment: %d, Current payment: " OTTD_PRINTF64 ", Transit periods: (%u, %u)",
				spec->initial_payment, (int64_t)spec->current_payment, spec->transit_periods[0], spec->transit_periods[1]);
		output.print(buffer);
		seprintf(buffer, lastof(buffer), "  Freight: %s, Town acceptance effect: %u, Town production effect: %u",
				spec->is_freight ? "yes" : "no", spec->town_acceptance_effect, spec->town_production_effect);
		output.print(buffer);
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		dumper.DumpSpriteGroup(CargoSpec::Get(index)->group, 0);
	}
};

static const NIFeature _nif_cargo = {
	nullptr,
	_nic_cargo,
	nullptr,
	new NIHCargo(),
};


/*** NewGRF signals ***/
void DumpTileSignalsInfo(char *buffer, const char *last, uint index, NIExtraInfoOutput &output)
{
	for (Trackdir td = TRACKDIR_BEGIN; td < TRACKDIR_END; td = (Trackdir)(td + 1)) {
		if (!IsValidTrackdir(td)) continue;
		if (HasTrack(index, TrackdirToTrack(td)) && HasSignalOnTrackdir(index, td)) {
			char *b = buffer;
			const SignalState state = GetSignalStateByTrackdir(index, td);
			b += seprintf(b, last, "  trackdir: %d, state: %d", td, state);
			if (_extra_aspects > 0 && state == SIGNAL_STATE_GREEN) seprintf(b, last, ", aspect: %d", GetSignalAspect(index, TrackdirToTrack(td)));
			if (GetSignalAlwaysReserveThrough(index, TrackdirToTrack(td))) seprintf(b, last, ", always reserve through");
			if (GetSignalSpecialPropagationFlag(index, TrackdirToTrack(td))) seprintf(b, last, ", special propagation flag");
			output.print(buffer);
		}
	}
}

void DumpTunnelBridgeSignalsInfo(char *buffer, const char *last, uint index, NIExtraInfoOutput &output)
{
	if (IsTunnelBridgeSignalSimulationEntrance(index)) {
		char *b = buffer;
		const SignalState state = GetTunnelBridgeEntranceSignalState(index);
		b += seprintf(b, last, "  Entrance: state: %d", state);
		if (_extra_aspects > 0 && state == SIGNAL_STATE_GREEN) b += seprintf(b, last, ", aspect: %d", GetTunnelBridgeEntranceSignalAspect(index));
		output.print(buffer);
	}
	if (IsTunnelBridgeSignalSimulationExit(index)) {
		char *b = buffer;
		const SignalState state = GetTunnelBridgeExitSignalState(index);
		b += seprintf(b, last, "  Exit: state: %d", state);
		if (_extra_aspects > 0 && state == SIGNAL_STATE_GREEN) b += seprintf(b, last, ", aspect: %d", GetTunnelBridgeExitSignalAspect(index));
		output.print(buffer);
	}
	if (GetTunnelBridgeSignalSpecialPropagationFlag(index)) {
		seprintf(buffer, last, "  Special propagation flag");
		output.print(buffer);
	}
	TileIndex end = GetOtherTunnelBridgeEnd(index);
	extern uint GetTunnelBridgeSignalSimulationSignalCount(TileIndex begin, TileIndex end);
	seprintf(buffer, last, "  Spacing: %d, total signals: %d", GetTunnelBridgeSignalSimulationSpacing(index), GetTunnelBridgeSignalSimulationSignalCount(index, end));
	output.print(buffer);
}

static const NIVariable _niv_signals[] = {
	NIV(0x40, "terrain type"),
	NIV(A2VRI_SIGNALS_SIGNAL_RESTRICTION_INFO, "restriction info"),
	NIV(A2VRI_SIGNALS_SIGNAL_CONTEXT, "context"),
	NIV(A2VRI_SIGNALS_SIGNAL_STYLE, "style"),
	NIV(A2VRI_SIGNALS_SIGNAL_SIDE, "side"),
	NIV(A2VRI_SIGNALS_SIGNAL_VERTICAL_CLEARANCE, "vertical_clearance"),
	NIV_END()
};

class NIHSignals : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return _new_signals_grfs.empty(); }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_SIGNALS, INVALID_STRING_ID, index); }
	uint32_t GetGRFID(uint index) const override         { return 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		extern TraceRestrictProgram *GetFirstTraceRestrictProgramOnTile(TileIndex t);
		CustomSignalSpriteContext ctx = { CSSC_TRACK };
		uint8_t style = 0;
		uint z = 0;
		if (IsTunnelBridgeWithSignalSimulation(index)) {
			ctx = { IsTunnelBridgeSignalSimulationEntrance(index) ? CSSC_TUNNEL_BRIDGE_ENTRANCE : CSSC_TUNNEL_BRIDGE_EXIT };
			if (IsTunnel(index)) ctx.ctx_flags |= CSSCF_TUNNEL;
			style = GetTunnelBridgeSignalStyle(index);
			z = GetTunnelBridgeSignalZ(index, !IsTunnelBridgeSignalSimulationEntrance(index));
		} else if (IsTileType(index, MP_RAILWAY) && HasSignals(index)) {
			TrackBits bits = GetTrackBits(index);
			do {
				Track track = RemoveFirstTrack(&bits);
				if (HasSignalOnTrack(index, track)) {
					style = GetSignalStyle(index, track);
					Trackdir td = TrackToTrackdir(track);
					if (!HasSignalOnTrackdir(index, td)) td = ReverseTrackdir(td);

					uint x, y;
					GetSignalXYZByTrackdir(index, td, HasBit(_signal_style_masks.signal_opposite_side, style), x, y, z);
					break;
				}
			} while (bits != TRACK_BIT_NONE);
		}
		NewSignalsResolverObject ro(nullptr, index, TCX_NORMAL, 0, 0, ctx, style, GetFirstTraceRestrictProgramOnTile(index), z);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		char buffer[1024];
		output.print("Debug Info:");
		if (IsTileType(index, MP_RAILWAY) && HasSignals(index)) {
			output.print("Signals:");
			DumpTileSignalsInfo(buffer, lastof(buffer), index, output);
		}
		if (IsTunnelBridgeWithSignalSimulation(index)) {
			output.print("Signals:");
			DumpTunnelBridgeSignalsInfo(buffer, lastof(buffer), index, output);
		}
		if (_settings_game.vehicle.train_speed_adaptation) {
			SignalSpeedKey speed_key = { index, 0, (Trackdir)0 };
			for (auto iter = _signal_speeds.lower_bound(speed_key); iter != _signal_speeds.end() && iter->first.signal_tile == index; ++iter) {
				const auto &it = *iter;
				char *b = buffer + seprintf(buffer, lastof(buffer), "Speed adaptation: Track: %X, last dir: %X --> speed: %u",
						it.first.signal_track, it.first.last_passing_train_dir, it.second.train_speed);
				if (it.second.IsOutOfDate()) {
					b += seprintf(b, lastof(buffer), ", expired");
				} else {
					b += seprintf(b, lastof(buffer), ", expires in %u ticks", (uint)(it.second.time_stamp - _state_ticks).base());
				}
				output.print(buffer);
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
		list.push_back(MakeDropDownListStringItem(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_RAIL_TYPE, 0, !IsTileType(index, MP_RAILWAY)));
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
	nullptr,
	nullptr,
	_niv_signals,
	new NIHSignals(),
};

/*** NewGRF objects ***/

#define NICO(cb_id, bit) NIC(cb_id, ObjectSpec, callback_mask, bit)
static const NICallback _nic_objects[] = {
	NICO(CBID_OBJECT_LAND_SLOPE_CHECK,     CBM_OBJ_SLOPE_CHECK),
	NICO(CBID_OBJECT_ANIMATION_NEXT_FRAME, CBM_OBJ_ANIMATION_NEXT_FRAME),
	NICO(CBID_OBJECT_ANIMATION_START_STOP, CBM_NO_BIT),
	NICO(CBID_OBJECT_ANIMATION_SPEED,      CBM_OBJ_ANIMATION_SPEED),
	NICO(CBID_OBJECT_COLOUR,               CBM_OBJ_COLOUR),
	NICO(CBID_OBJECT_FUND_MORE_TEXT,       CBM_OBJ_FUND_MORE_TEXT),
	NICO(CBID_OBJECT_AUTOSLOPE,            CBM_OBJ_AUTOSLOPE),
	NIC_END()
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
	NIV_END()
};

class NIHObject : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return ObjectSpec::GetByTile(index)->grf_prop.grffile == nullptr; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { return GetTownInspectWindowNumber(Object::GetByTile(index)->town); }
	const void *GetInstance(uint index)const override    { return Object::GetByTile(index); }
	const void *GetSpec(uint index) const override       { return ObjectSpec::GetByTile(index); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_OBJECT, INVALID_STRING_ID, index); }
	uint32_t GetGRFID(uint index) const override         { return (!this->ShowExtraInfoOnly(index)) ? ObjectSpec::GetByTile(index)->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		ObjectResolverObject ro(ObjectSpec::GetByTile(index), Object::GetByTile(index), index);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		char buffer[1024];
		output.print("Debug Info:");
		const ObjectSpec *spec = ObjectSpec::GetByTile(index);
		if (spec != nullptr) {
			ObjectID id = GetObjectIndex(index);
			const Object *obj = Object::Get(id);
			char *b = buffer + seprintf(buffer, lastof(buffer), "  index: %u, type ID: %u", id, GetObjectType(index));
			if (spec->grf_prop.grffile != nullptr) {
				b += seprintf(b, lastof(buffer), "  (local ID: %u)", spec->grf_prop.local_id);
			}
			if (spec->class_index != INVALID_OBJECT_CLASS) {
				uint class_id = ObjectClass::Get(spec->class_index)->global_id;
				b += seprintf(b, lastof(buffer), ", class ID: %c%c%c%c", class_id >> 24, class_id >> 16, class_id >> 8, class_id);
			}
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  view: %u, colour: %u, effective foundation: %u", obj->view, obj->colour, GetObjectEffectiveFoundationType(index));
			output.print(buffer);
			if (spec->ctrl_flags & OBJECT_CTRL_FLAG_USE_LAND_GROUND) {
				seprintf(buffer, lastof(buffer), "  ground type: %u, density: %u, counter: %u, water class: %u", GetObjectGroundType(index), GetObjectGroundDensity(index), GetObjectGroundCounter(index), GetWaterClass(index));
				output.print(buffer);
			}
			seprintf(buffer, lastof(buffer), "  animation: frames: %u, status: %u, speed: %u, triggers: 0x%X", spec->animation.frames, spec->animation.status, spec->animation.speed, spec->animation.triggers);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  size: %ux%u, height: %u, views: %u", GB(spec->size, 4, 4), GB(spec->size, 0, 4), spec->height, spec->views);
			output.print(buffer);

			{
				CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(spec->introduction_date);
				char *b = buffer + seprintf(buffer, lastof(buffer), "  intro: %i-%02i-%02i",
						ymd.year.base(), ymd.month + 1, ymd.day);
				if (spec->end_of_life_date < CalTime::MAX_DATE) {
					ymd = CalTime::ConvertDateToYMD(spec->end_of_life_date);
					seprintf(b, lastof(buffer), ", end of life: %i-%02i-%02i",
							ymd.year.base(), ymd.month + 1, ymd.day);
				}
				output.print(buffer);
			}

			output.register_next_line_click_flag_toggle(1);
			seprintf(buffer, lastof(buffer), "  [%c] flags: 0x%X", output.flags & 1 ? '-' : '+', spec->flags);
			output.print(buffer);
			if (output.flags & 1) {
				auto print = [&](const char *name) {
					seprintf(buffer, lastof(buffer), "    %s", name);
					output.print(buffer);
				};
				auto check_flag = [&](ObjectFlags flag, const char *name) {
					if (spec->flags & flag) print(name);
				};
				auto check_ctrl_flag = [&](ObjectCtrlFlags flag, const char *name) {
					if (spec->ctrl_flags & flag) print(name);
				};
				check_flag(OBJECT_FLAG_ONLY_IN_SCENEDIT,   "OBJECT_FLAG_ONLY_IN_SCENEDIT");
				check_flag(OBJECT_FLAG_CANNOT_REMOVE,      "OBJECT_FLAG_CANNOT_REMOVE");
				check_flag(OBJECT_FLAG_AUTOREMOVE,         "OBJECT_FLAG_AUTOREMOVE");
				check_flag(OBJECT_FLAG_BUILT_ON_WATER,     "OBJECT_FLAG_BUILT_ON_WATER");
				check_flag(OBJECT_FLAG_CLEAR_INCOME,       "OBJECT_FLAG_CLEAR_INCOME");
				check_flag(OBJECT_FLAG_HAS_NO_FOUNDATION,  "OBJECT_FLAG_HAS_NO_FOUNDATION");
				check_flag(OBJECT_FLAG_ANIMATION,          "OBJECT_FLAG_ANIMATION");
				check_flag(OBJECT_FLAG_ONLY_IN_GAME,       "OBJECT_FLAG_ONLY_IN_GAME");
				check_flag(OBJECT_FLAG_2CC_COLOUR,         "OBJECT_FLAG_2CC_COLOUR");
				check_flag(OBJECT_FLAG_NOT_ON_LAND,        "OBJECT_FLAG_NOT_ON_LAND");
				check_flag(OBJECT_FLAG_DRAW_WATER,         "OBJECT_FLAG_DRAW_WATER");
				check_flag(OBJECT_FLAG_ALLOW_UNDER_BRIDGE, "OBJECT_FLAG_ALLOW_UNDER_BRIDGE");
				check_flag(OBJECT_FLAG_ANIM_RANDOM_BITS,   "OBJECT_FLAG_ANIM_RANDOM_BITS");
				check_flag(OBJECT_FLAG_SCALE_BY_WATER,     "OBJECT_FLAG_SCALE_BY_WATER");
				check_ctrl_flag(OBJECT_CTRL_FLAG_USE_LAND_GROUND, "OBJECT_CTRL_FLAG_USE_LAND_GROUND");
				check_ctrl_flag(OBJECT_CTRL_FLAG_EDGE_FOUNDATION, "OBJECT_CTRL_FLAG_EDGE_FOUNDATION");
				check_ctrl_flag(OBJECT_CTRL_FLAG_FLOOD_RESISTANT, "OBJECT_CTRL_FLAG_FLOOD_RESISTANT");
				check_ctrl_flag(OBJECT_CTRL_FLAG_VPORT_MAP_TYPE,  "OBJECT_CTRL_FLAG_VPORT_MAP_TYPE");
			}
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		extern void DumpObjectSpriteGroup(const ObjectSpec *spec, SpriteGroupDumper &dumper);
		DumpObjectSpriteGroup(ObjectSpec::GetByTile(index), dumper);
	}
};

static const NIFeature _nif_object = {
	nullptr,
	_nic_objects,
	_niv_objects,
	new NIHObject(),
};


/*** NewGRF rail types ***/

static const NIVariable _niv_railtypes[] = {
	NIV(0x40, "terrain type"),
	NIV(0x41, "enhanced tunnels"),
	NIV(0x42, "level crossing status"),
	NIV(0x43, "construction date"),
	NIV(0x44, "town zone"),
	NIV(A2VRI_RAILTYPE_ADJACENT_CROSSING, "adjacent crossing"),
	NIV_END()
};

static void PrintTypeLabels(char *buffer, const char *last, const char *prefix, uint32_t label, const uint32_t *alternate_labels, size_t alternate_labels_count, std::function<void(const char *)> &print)
{
	if (alternate_labels_count > 0) {
		char *b = buffer;
		b += seprintf(b, last, "%sAlternate labels: ", prefix);
		for (size_t i = 0; i < alternate_labels_count; i++) {
			if (i != 0) b += seprintf(b, last, ", ");
			uint32_t l = alternate_labels[i];
			b += seprintf(b, last, "%s", label_dumper().Label(l));
		}
		print(buffer);
	}
}

class NIHRailType : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_RAIL_TYPE, INVALID_STRING_ID, index); }
	uint32_t GetGRFID(uint index) const override         { return 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		/* There is no unique GRFFile for the tile. Multiple GRFs can define different parts of the railtype.
		 * However, currently the NewGRF Debug GUI does not display variables depending on the GRF (like 0x7F) anyway. */
		RailTypeResolverObject ro(nullptr, index, TCX_NORMAL, RTSG_END);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		char buffer[1024];

		RailType primary = GetTileRailType(index);
		RailType secondary = GetTileSecondaryRailTypeIfValid(index);

		auto writeRailType = [&](RailType type) {
			const RailTypeInfo *info = GetRailTypeInfo(type);
			seprintf(buffer, lastof(buffer), "  Type: %u (%s)", type, label_dumper().RailTypeLabel(type));
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  Flags: %c%c%c%c%c%c",
					HasBit(info->flags, RTF_CATENARY) ? 'c' : '-',
					HasBit(info->flags, RTF_NO_LEVEL_CROSSING) ? 'l' : '-',
					HasBit(info->flags, RTF_HIDDEN) ? 'h' : '-',
					HasBit(info->flags, RTF_NO_SPRITE_COMBINE) ? 's' : '-',
					HasBit(info->flags, RTF_ALLOW_90DEG) ? 'a' : '-',
					HasBit(info->flags, RTF_DISALLOW_90DEG) ? 'd' : '-');
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  Ctrl flags: %c%c%c%c",
					HasBit(info->ctrl_flags, RTCF_PROGSIG) ? 'p' : '-',
					HasBit(info->ctrl_flags, RTCF_RESTRICTEDSIG) ? 'r' : '-',
					HasBit(info->ctrl_flags, RTCF_NOREALISTICBRAKING) ? 'b' : '-',
					HasBit(info->ctrl_flags, RTCF_NOENTRYSIG) ? 'n' : '-');
			output.print(buffer);

			uint bit = 1;
			auto dump_railtypes = [&](const char *name, RailTypes types, RailTypes mark) {
				output.register_next_line_click_flag_toggle(bit);
				seprintf(buffer, lastof(buffer), "  [%c] %s: 0x" OTTD_PRINTFHEX64, (output.flags & bit) ? '-' : '+', name, types);
				output.print(buffer);
				if (output.flags & bit) {
					DumpRailTypeList(output, "    ", types, mark);
				}

				bit <<= 1;
			};
			dump_railtypes("Powered", info->powered_railtypes, RAILTYPES_NONE);
			dump_railtypes("Compatible", info->compatible_railtypes, RAILTYPES_NONE);
			dump_railtypes("All compatible", info->all_compatible_railtypes, ~info->compatible_railtypes);

			PrintTypeLabels(buffer, lastof(buffer), "  ", info->label, (const uint32_t*) info->alternate_labels.data(), info->alternate_labels.size(), output.print);
			seprintf(buffer, lastof(buffer), "  Cost multiplier: %u/8, Maintenance multiplier: %u/8", info->cost_multiplier, info->maintenance_multiplier);
			output.print(buffer);

			CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(info->introduction_date);
			seprintf(buffer, lastof(buffer), "  Introduction date: %4i-%02i-%02i", ymd.year.base(), ymd.month + 1, ymd.day);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  Intro required railtypes: 0x" OTTD_PRINTFHEX64, info->introduction_required_railtypes);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  Intro railtypes: 0x" OTTD_PRINTFHEX64, info->introduces_railtypes);
			output.print(buffer);
		};

		output.print("Debug Info:");
		writeRailType(primary);
		if (secondary != INVALID_RAILTYPE) {
			writeRailType(secondary);
		}

		if (IsTileType(index, MP_RAILWAY) && HasSignals(index)) {
			output.print("Signals:");
			DumpTileSignalsInfo(buffer, lastof(buffer), index, output);
		}
		if (IsTileType(index, MP_RAILWAY) && IsRailDepot(index)) {
			seprintf(buffer, lastof(buffer), "Depot: reserved: %u", HasDepotReservation(index));
			output.print(buffer);
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		extern void DumpRailTypeSpriteGroup(RailType rt, SpriteGroupDumper &dumper);
		DumpRailTypeSpriteGroup(GetTileRailType(index), dumper);
	}

	/* virtual */ bool ShowOptionsDropDown(uint index) const override
	{
		return true;
	}

	/* virtual */ void FillOptionsDropDown(uint index, DropDownList &list) const override
	{
		list.push_back(MakeDropDownListStringItem(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_ROAD_TYPE, 0, !IsLevelCrossingTile(index)));
		list.push_back(MakeDropDownListStringItem(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_SIGNALS, 1, !(IsTileType(index, MP_RAILWAY) && HasSignals(index))));
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
	nullptr,
	nullptr,
	_niv_railtypes,
	new NIHRailType(),
};


/*** NewGRF airport tiles ***/

#define NICAT(cb_id, bit) NIC(cb_id, AirportTileSpec, callback_mask, bit)
static const NICallback _nic_airporttiles[] = {
	NICAT(CBID_AIRPTILE_DRAW_FOUNDATIONS, CBM_AIRT_DRAW_FOUNDATIONS),
	NICAT(CBID_AIRPTILE_ANIM_START_STOP,  CBM_NO_BIT),
	NICAT(CBID_AIRPTILE_ANIM_NEXT_FRAME,  CBM_AIRT_ANIM_NEXT_FRAME),
	NICAT(CBID_AIRPTILE_ANIMATION_SPEED,  CBM_AIRT_ANIM_SPEED),
	NIC_END()
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
	NIV_END()
};

class NIHAirportTile : public NIHelper {
	bool IsInspectable(uint index) const override        { return AirportTileSpec::Get(GetAirportGfx(index))->grf_prop.grffile != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_AIRPORTS, GetStationIndex(index)); }
	const void *GetInstance(uint)const override          { return nullptr; }
	const void *GetSpec(uint index) const override       { return AirportTileSpec::Get(GetAirportGfx(index)); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, GetStationIndex(index), index); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? AirportTileSpec::Get(GetAirportGfx(index))->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		AirportTileResolverObject ro(AirportTileSpec::GetByTile(index), index, Station::GetByTile(index));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		char buffer[1024];
		output.print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  Gfx Index: %u", GetAirportGfx(index));
		output.print(buffer);
		const AirportTileSpec *spec = AirportTileSpec::Get(GetAirportGfx(index));
		if (spec) {
			seprintf(buffer, lastof(buffer), "  animation: frames: %u, status: %u, speed: %u, triggers: 0x%X", spec->animation.frames, spec->animation.status, spec->animation.speed, spec->animation.triggers);
			output.print(buffer);
		}
	}
};

static const NIFeature _nif_airporttile = {
	nullptr,
	_nic_airporttiles,
	_niv_airporttiles,
	new NIHAirportTile(),
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
	NIV_END()
};

class NIHAirport : public NIHelper {
	bool IsInspectable(uint index) const override        { return AirportSpec::Get(Station::Get(index)->airport.type)->grf_prop.grffile != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, Station::Get(index)->town->index); }
	const void *GetInstance(uint index)const override    { return Station::Get(index); }
	const void *GetSpec(uint index) const override       { return AirportSpec::Get(Station::Get(index)->airport.type); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, index, Station::Get(index)->airport.tile); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? AirportSpec::Get(Station::Get(index)->airport.type)->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		Station *st = Station::Get(index);
		AirportResolverObject ro(st->airport.tile, st, st->airport.type, st->airport.layout);
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
	nullptr,
	nullptr,
	_niv_airports,
	new NIHAirport(),
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
	NIV_END()
};

class NIHTown : public NIHelper {
	bool IsInspectable(uint index) const override        { return Town::IsValidID(index); }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index)const override    { return Town::Get(index); }
	const void *GetSpec(uint) const override             { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetSimpleStringParameters(STR_TOWN_NAME, index); }
	uint32_t GetGRFID(uint index) const override         { return 0; }
	bool PSAWithParameter() const override               { return true; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
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
		char buffer[1024];

		output.print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  Index: %u", index);
		output.print(buffer);
		seprintf(buffer, lastof(buffer), "  Churches: %u, Stadiums: %u", t->church_count, t->stadium_count);
		output.print(buffer);

		seprintf(buffer, lastof(buffer), "  Nearby stations: %u", (uint) t->stations_near.size());
		output.print(buffer);
		for (const Station *st : t->stations_near) {
			seprintf(buffer, lastof(buffer), "    %u: %s", st->index, st->GetCachedName());
			output.print(buffer);
		}

		seprintf(buffer, lastof(buffer), "  Growth rate: %u, Growth Counter: %u, T to Rebuild: %u, Growing: %u, Custom growth: %u",
				t->growth_rate, t->grow_counter, t->time_until_rebuild, HasBit(t->flags, TOWN_IS_GROWING) ? 1 : 0,HasBit(t->flags, TOWN_CUSTOM_GROWTH) ? 1 : 0);
		output.print(buffer);

		if (t->have_ratings != 0) {
			output.print("  Company ratings:");
			for (uint8_t bit : SetBitIterator(t->have_ratings)) {
				seprintf(buffer, lastof(buffer), "    %u: %d", bit, t->ratings[bit]);
				output.print(buffer);
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
	nullptr,
	nullptr,
	_niv_towns,
	new NIHTown(),
};

class NIHStationStruct : public NIHelper {
	bool IsInspectable(uint index) const override        { return BaseStation::IsValidID(index); }
	bool ShowExtraInfoOnly(uint index) const override    { return true; }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }

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

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		return 0;
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		char buffer[1024];
		output.print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  Index: %u", index);
		output.print(buffer);
		const BaseStation *bst = BaseStation::GetIfValid(index);
		if (!bst) return;
		seprintf(buffer, lastof(buffer), "  Tile: %X (%u x %u)", bst->xy, TileX(bst->xy), TileY(bst->xy));
		output.print(buffer);
		if (bst->rect.IsEmpty()) {
			output.print("  rect: empty");
		} else {
			seprintf(buffer, lastof(buffer), "  rect: left: %u, right: %u, top: %u, bottom: %u", bst->rect.left, bst->rect.right, bst->rect.top, bst->rect.bottom);
			output.print(buffer);
		}
		const Station *st = Station::GetIfValid(index);
		if (st) {
			if (st->industry) {
				seprintf(buffer, lastof(buffer), "  Neutral industry: %u: %s", st->industry->index, st->industry->GetCachedName().c_str());
				output.print(buffer);
			}
			seprintf(buffer, lastof(buffer), "  Nearby industries: %u", (uint) st->industries_near.size());
			output.print(buffer);
			for (const auto &i : st->industries_near) {
				seprintf(buffer, lastof(buffer), "    %u: %s, distance: %u", i.industry->index, i.industry->GetCachedName().c_str(), i.distance);
				output.print(buffer);
			}
			seprintf(buffer, lastof(buffer), "  Station tiles: %u", st->station_tiles);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  Delete counter: %u", st->delete_ctr);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  Docking tiles: %X, %u x %u", st->docking_station.tile, st->docking_station.w, st->docking_station.h);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  Time since: load: %u, unload: %u", st->time_since_load, st->time_since_unload);
			output.print(buffer);

			if (st->airport.tile != INVALID_TILE) {
				seprintf(buffer, lastof(buffer), "  Airport: type: %u (local: %u), layout: %u, rotation: %u",
						st->airport.type, st->airport.GetSpec()->grf_prop.local_id, st->airport.layout, st->airport.rotation);
				output.print(buffer);
			}

			for (const CargoSpec *cs : CargoSpec::Iterate()) {
				const GoodsEntry *ge = &st->goods[cs->Index()];

				if (ge->data == nullptr && ge->status == 0) {
					/* Nothing of note to show */
					continue;
				}

				const StationCargoPacketMap *pkts = ge->data != nullptr ? ge->data->cargo.Packets() : nullptr;

				seprintf(buffer, lastof(buffer), "  Goods entry: %u: %s", cs->Index(), GetStringPtr(cs->name));
				output.print(buffer);
				char *b = buffer + seprintf(buffer, lastof(buffer), "    Status: %c%c%c%c%c%c%c",
						HasBit(ge->status, GoodsEntry::GES_ACCEPTANCE)       ? 'a' : '-',
						HasBit(ge->status, GoodsEntry::GES_RATING)           ? 'r' : '-',
						HasBit(ge->status, GoodsEntry::GES_EVER_ACCEPTED)    ? 'e' : '-',
						HasBit(ge->status, GoodsEntry::GES_LAST_MONTH)       ? 'l' : '-',
						HasBit(ge->status, GoodsEntry::GES_CURRENT_MONTH)    ? 'c' : '-',
						HasBit(ge->status, GoodsEntry::GES_ACCEPTED_BIGTICK) ? 'b' : '-',
						HasBit(ge->status, GoodsEntry::GES_NO_CARGO_SUPPLY)  ? 'n' : '-');
				if (ge->data != nullptr && ge->data->MayBeRemoved()) b += seprintf(b, lastof(buffer), ", (removable)");
				if (ge->data == nullptr) b += seprintf(b, lastof(buffer), ", (no data)");
				output.print(buffer);

				if (ge->amount_fract > 0) {
					seprintf(buffer, lastof(buffer), "    Amount fract: %u", ge->amount_fract);
					output.print(buffer);
				}
				if (pkts != nullptr && (pkts->MapSize() > 0 || ge->CargoTotalCount() > 0)) {
					seprintf(buffer, lastof(buffer), "    Cargo packets: %u, cargo packet keys: %u, available: %u, reserved: %u",
							(uint)pkts->size(), (uint)pkts->MapSize(), ge->CargoAvailableCount(), ge->CargoReservedCount());
					output.print(buffer);
				}
				if (ge->link_graph != INVALID_LINK_GRAPH) {
					seprintf(buffer, lastof(buffer), "    Link graph: %u, node: %u", ge->link_graph, ge->node);
					output.print(buffer);
				}
				if (ge->max_waiting_cargo > 0) {
					seprintf(buffer, lastof(buffer), "    Max waiting cargo: %u", ge->max_waiting_cargo);
					output.print(buffer);
				}
				if (ge->data != nullptr && ge->data->flows.size() > 0) {
					size_t total_shares = 0;
					for (const FlowStat &fs : ge->data->flows.IterateUnordered()) {
						total_shares += fs.size();
					}
					seprintf(buffer, lastof(buffer), "    Flows: %u, total shares: %u", (uint)ge->data->flows.size(), (uint)total_shares);
					output.print(buffer);
				}
			}
		}
		const Waypoint *wp = Waypoint::GetIfValid(index);
		if (wp) {
			output.register_next_line_click_flag_toggle(1);
			seprintf(buffer, lastof(buffer), "  [%c] flags: 0x%X", output.flags & 1 ? '-' : '+', wp->waypoint_flags);
			output.print(buffer);
			if (output.flags & 1) {
				auto print = [&](const char *name) {
					seprintf(buffer, lastof(buffer), "    %s", name);
					output.print(buffer);
				};
				auto check_flag = [&](WaypointFlags flag, const char *name) {
					if (HasBit(wp->waypoint_flags, flag)) print(name);
				};
				check_flag(WPF_HIDE_LABEL,   "WPF_HIDE_LABEL");
				check_flag(WPF_ROAD,         "WPF_ROAD");
			}

			seprintf(buffer, lastof(buffer), "  road_waypoint_area: tile: %X (%u x %u), width: %u, height: %u",
					wp->road_waypoint_area.tile, TileX(wp->road_waypoint_area.tile), TileY(wp->road_waypoint_area.tile), wp->road_waypoint_area.w, wp->road_waypoint_area.h);
			output.print(buffer);
		}
	}
};

static const NIFeature _nif_station_struct = {
	nullptr,
	nullptr,
	nullptr,
	new NIHStationStruct(),
};

class NIHTraceRestrict : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return true; }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }

	void SetStringParameters(uint index) const override
	{
		SetDParam(0, STR_NEWGRF_INSPECT_CAPTION_TRACERESTRICT);
		SetDParam(1, GetTraceRestrictRefIdTileIndex(static_cast<TraceRestrictRefId>(index)));
		SetDParam(2, GetTraceRestrictRefIdTrack(static_cast<TraceRestrictRefId>(index)));
	}

	uint32_t GetGRFID(uint index) const override         { return 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		return 0;
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		TraceRestrictRefId ref = static_cast<TraceRestrictRefId>(index);
		const TraceRestrictProgram *prog = GetTraceRestrictProgram(ref, false);

		if (prog == nullptr) {
			output.print("No program");
			return;
		}

		char buffer[1024];
		seprintf(buffer, lastof(buffer), "Index: %u", prog->index);
		output.print(buffer);
		output.print("");

		seprintf(buffer, lastof(buffer), "Actions used: 0x%X", prog->actions_used_flags);
		output.print(buffer);
		auto check_action = [&](TraceRestrictProgramActionsUsedFlags flag, const char *label) {
			if (prog->actions_used_flags & flag) {
				seprintf(buffer, lastof(buffer), "  %s", label);
				output.print(buffer);
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
		output.print("");

		seprintf(buffer, lastof(buffer), "Ref count: %u", prog->refcount);
		output.print(buffer);
		const TraceRestrictRefId *refs = prog->GetRefIdsPtr();
		for (uint32_t i = 0; i < prog->refcount; i++) {
			TileIndex tile = GetTraceRestrictRefIdTileIndex(refs[i]);
			seprintf(buffer, lastof(buffer), "  %X x %X, track: %X", TileX(tile), TileY(tile), GetTraceRestrictRefIdTrack(refs[i]));
			output.print(buffer);
		}
		output.print("");

		seprintf(buffer, lastof(buffer), "Program: items: %u, instructions: %u", (uint)prog->items.size(), (uint)prog->GetInstructionCount());
		output.print(buffer);
		auto iter = prog->items.begin();
		auto end = prog->items.end();
		while (iter != end) {
			if (IsTraceRestrictDoubleItem(*iter)) {
				seprintf(buffer, lastof(buffer), "  %08X %08X", *iter, *(iter + 1));
				iter += 2;
			} else {
				seprintf(buffer, lastof(buffer), "  %08X", *iter);
				++iter;
			}
			output.print(buffer);
		}
	}
};

static const NIFeature _nif_tracerestrict = {
	nullptr,
	nullptr,
	nullptr,
	new NIHTraceRestrict(),
};

/*** NewGRF road types ***/

static const NIVariable _niv_roadtypes[] = {
	NIV(0x40, "terrain type"),
	NIV(0x41, "enhanced tunnels"),
	NIV(0x42, "level crossing status"),
	NIV(0x43, "construction date"),
	NIV(0x44, "town zone"),
	NIV_END()
};

class NIHRoadType : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index) const override   { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_ROAD_TYPE, INVALID_STRING_ID, index); }
	uint32_t GetGRFID(uint index) const override         { return 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		/* There is no unique GRFFile for the tile. Multiple GRFs can define different parts of the railtype.
		 * However, currently the NewGRF Debug GUI does not display variables depending on the GRF (like 0x7F) anyway. */
		RoadTypeResolverObject ro(nullptr, index, TCX_NORMAL, ROTSG_END);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		output.print("Debug Info:");
		auto writeInfo = [&](RoadTramType rtt) {
			RoadType type = GetRoadType(index, rtt);
			if (type == INVALID_ROADTYPE) return;

			char buffer[1024];
			const RoadTypeInfo* rti = GetRoadTypeInfo(type);
			seprintf(buffer, lastof(buffer), "  %s Type: %u (%s)", rtt == RTT_TRAM ? "Tram" : "Road", type, label_dumper().RoadTypeLabel(type));
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "    Flags: %c%c%c%c%c",
					HasBit(rti->flags, ROTF_CATENARY) ? 'c' : '-',
					HasBit(rti->flags, ROTF_NO_LEVEL_CROSSING) ? 'l' : '-',
					HasBit(rti->flags, ROTF_NO_HOUSES) ? 'X' : '-',
					HasBit(rti->flags, ROTF_HIDDEN) ? 'h' : '-',
					HasBit(rti->flags, ROTF_TOWN_BUILD) ? 'T' : '-');
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "    Extra Flags: %c%c%c%c",
					HasBit(rti->extra_flags, RXTF_NOT_AVAILABLE_AI_GS) ? 's' : '-',
					HasBit(rti->extra_flags, RXTF_NO_TOWN_MODIFICATION) ? 't' : '-',
					HasBit(rti->extra_flags, RXTF_NO_TUNNELS) ? 'T' : '-',
					HasBit(rti->extra_flags, RXTF_NO_TRAIN_COLLISION) ? 'c' : '-');
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "    Collision mode: %u", rti->collision_mode);
			output.print(buffer);

			output.register_next_line_click_flag_toggle((1 << rtt));
			seprintf(buffer, lastof(buffer), "    [%c] Powered: 0x" OTTD_PRINTFHEX64, (output.flags & (1 << rtt)) ? '-' : '+', rti->powered_roadtypes);
			output.print(buffer);
			if (output.flags & (1 << rtt)) {
				DumpRoadTypeList(output, "      ", rti->powered_roadtypes);
			}
			PrintTypeLabels(buffer, lastof(buffer), "    ", rti->label, (const uint32_t*) rti->alternate_labels.data(), rti->alternate_labels.size(), output.print);
			seprintf(buffer, lastof(buffer), "    Cost multiplier: %u/8, Maintenance multiplier: %u/8", rti->cost_multiplier, rti->maintenance_multiplier);
			output.print(buffer);
		};
		writeInfo(RTT_ROAD);
		writeInfo(RTT_TRAM);
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		for (RoadTramType rtt : { RTT_ROAD, RTT_TRAM }) {
			RoadType rt = GetRoadType(index, rtt);
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
		list.push_back(MakeDropDownListStringItem(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_RAIL_TYPE, 0, !IsLevelCrossingTile(index)));
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
	nullptr,
	nullptr,
	_niv_roadtypes,
	new NIHRoadType(),
};

#define NICRS(cb_id, bit) NIC(cb_id, RoadStopSpec, callback_mask, bit)
static const NICallback _nic_roadstops[] = {
	NICRS(CBID_STATION_AVAILABILITY,     CBM_ROAD_STOP_AVAIL),
	NICRS(CBID_STATION_ANIM_START_STOP,  CBM_NO_BIT),
	NICRS(CBID_STATION_ANIM_NEXT_FRAME,  CBM_ROAD_STOP_ANIMATION_NEXT_FRAME),
	NICRS(CBID_STATION_ANIMATION_SPEED,  CBM_ROAD_STOP_ANIMATION_SPEED),
	NIC_END()
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
	NIV_END(),
};

class NIHRoadStop : public NIHelper {
	bool IsInspectable(uint index) const override        { return GetRoadStopSpec(index) != nullptr; }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { return GetTownInspectWindowNumber(BaseStation::GetByTile(index)->town); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return GetRoadStopSpec(index); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, GetStationIndex(index), index); }
	uint32_t GetGRFID(uint index) const override         { return (this->IsInspectable(index)) ? GetRoadStopSpec(index)->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		int view = GetRoadStopDir(index);
		if (IsDriveThroughStopTile(index)) view += 4;
		RoadStopResolverObject ro(GetRoadStopSpec(index), BaseStation::GetByTile(index), index, INVALID_ROADTYPE, GetStationType(index), view);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		char buffer[1024];
		output.print("Debug Info:");
		const RoadStopSpec *spec = GetRoadStopSpec(index);
		if (spec != nullptr) {
			uint class_id = RoadStopClass::Get(spec->class_index)->global_id;
			char *b = buffer + seprintf(buffer, lastof(buffer), "  class ID: %c%c%c%c", class_id >> 24, class_id >> 16, class_id >> 8, class_id);
			if (spec->grf_prop.grffile != nullptr) {
				b += seprintf(b, lastof(buffer), "  (local ID: %u)", spec->grf_prop.local_id);
			}
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  spec: stop type: %X, draw mode: %X, cargo triggers: " OTTD_PRINTFHEX64, spec->stop_type, spec->draw_mode, spec->cargo_triggers);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  spec: callback mask: %X, flags: %X, intl flags: %X", spec->callback_mask, spec->flags, spec->internal_flags);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  spec: build: %u, clear: %u, height: %u", spec->build_cost_multiplier, spec->clear_cost_multiplier, spec->height);
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "  animation: frames: %u, status: %u, speed: %u, triggers: 0x%X", spec->animation.frames, spec->animation.status, spec->animation.speed, spec->animation.triggers);
			output.print(buffer);

			const BaseStation *st = BaseStation::GetByTile(index);
			seprintf(buffer, lastof(buffer), "  road stop: random bits: %02X, animation frame: %02X", st->GetRoadStopRandomBits(index), st->GetRoadStopAnimationFrame(index));
			output.print(buffer);
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		extern void DumpRoadStopSpriteGroup(const BaseStation *st, const RoadStopSpec *spec, SpriteGroupDumper &dumper);
		DumpRoadStopSpriteGroup(BaseStation::GetByTile(index), GetRoadStopSpec(index), dumper);
	}
};

static const NIFeature _nif_roadstop = {
	nullptr,
	_nic_roadstops,
	_nif_roadstops,
	new NIHRoadStop(),
};

static const NIVariable _niv_newlandscape[] = {
	NIV(0x40, "terrain type"),
	NIV(0x41, "tile slope"),
	NIV(0x42, "tile height"),
	NIV(0x43, "tile hash"),
	NIV(0x44, "landscape type"),
	NIV(0x45, "ground info"),
	NIV(0x60, "land info of nearby tiles"),
	NIV_END(),
};

class NIHNewLandscape : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	bool ShowExtraInfoOnly(uint index) const override    { return _new_landscape_rocks_grfs.empty(); }
	bool ShowSpriteDumpButton(uint index) const override { return true; }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_LAI_CLEAR_DESCRIPTION_ROCKS, INVALID_STRING_ID, index); }
	uint32_t GetGRFID(uint index) const override         { return 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		if (!IsTileType(index, MP_CLEAR)) return 0;

		TileInfo ti;
		ti.x = TileX(index);
		ti.y = TileY(index);
		std::tie(ti.tileh, ti.z) = GetTilePixelSlope(index);
		ti.tile = index;

		NewLandscapeResolverObject ro(nullptr, &ti, NEW_LANDSCAPE_ROCKS);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, NIExtraInfoOutput &output) const override
	{
		char buffer[1024];
		output.print("New Landscape GRFs:");
		for (const GRFFile *grf : _new_landscape_rocks_grfs) {
			seprintf(buffer, lastof(buffer), "  GRF: %08X", BSWAP32(grf->grfid));
			output.print(buffer);
			seprintf(buffer, lastof(buffer), "    Enable rocks recolour: %d, Enable drawing snowy rocks: %d",
					HasBit(grf->new_landscape_ctrl_flags, NLCF_ROCKS_RECOLOUR_ENABLED), HasBit(grf->new_landscape_ctrl_flags, NLCF_ROCKS_DRAW_SNOWY_ENABLED));
			output.print(buffer);
		}
	}

	/* virtual */ void SpriteDump(uint index, SpriteGroupDumper &dumper) const override
	{
		extern void DumpNewLandscapeRocksSpriteGroups(SpriteGroupDumper &dumper);
		DumpNewLandscapeRocksSpriteGroups(dumper);
	}
};

static const NIFeature _nif_newlandscape = {
	nullptr,
	nullptr,
	_niv_newlandscape,
	new NIHNewLandscape(),
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
	&_nif_roadtype,     // GSF_TRAMTYPES
	&_nif_roadstop,     // GSF_ROADSTOPS
	&_nif_newlandscape, // GSF_NEWLANDSCAPE
	&_nif_town,         // GSF_FAKE_TOWNS
	&_nif_station_struct,  // GSF_FAKE_STATION_STRUCT
	&_nif_tracerestrict,   // GSF_FAKE_TRACERESTRICT
};
static_assert(lengthof(_nifeatures) == GSF_FAKE_END);
