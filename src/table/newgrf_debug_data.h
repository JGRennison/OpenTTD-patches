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
#include "../date_func.h"
#include "../timetable.h"
#include "../ship.h"
#include "../aircraft.h"

/* Helper for filling property tables */
#define NIP(prop, base, variable, type, name) { name, (ptrdiff_t)cpp_offsetof(base, variable), cpp_sizeof(base, variable), prop, type }
#define NIP_END() { nullptr, 0, 0, 0, 0 }

/* Helper for filling callback tables */
#define NIC(cb_id, base, variable, bit) { #cb_id, (ptrdiff_t)cpp_offsetof(base, variable), cpp_sizeof(base, variable), bit, cb_id }
#define NIC_END() { nullptr, 0, 0, 0, 0 }

/* Helper for filling variable tables */
#define NIV(var, name) { name, var }
#define NIV_END() { nullptr, 0 }


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
	uint GetParent(uint index) const override            { const Vehicle *first = Vehicle::Get(index)->First(); return GetInspectWindowNumber(GetGrfSpecFeature(first->type), first->index); }
	const void *GetInstance(uint index)const override    { return Vehicle::Get(index); }
	const void *GetSpec(uint index) const override       { return Vehicle::Get(index)->GetEngine(); }
	void SetStringParameters(uint index) const override  { this->SetSimpleStringParameters(STR_VEHICLE_NAME, index); }
	uint32 GetGRFID(uint index) const override                   { return Vehicle::Get(index)->GetGRFID(); }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		Vehicle *v = Vehicle::Get(index);
		VehicleResolverObject ro(v->engine_type, v, VehicleResolverObject::WO_CACHED);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	/* virtual */ void ExtraInfo(uint index, std::function<void(const char *)> print) const override
	{
		char buffer[1024];
		Vehicle *v = Vehicle::Get(index);
		print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  Index: %u", index);
		print(buffer);
		char *b = buffer;
		b += seprintf(b, lastof(buffer), "  Flags: ");
		b = v->DumpVehicleFlags(b, lastof(buffer), false);
		print(buffer);

		b = buffer + seprintf(buffer, lastof(buffer), "  ");
		b = DumpTileInfo(b, lastof(buffer), v->tile);
		if (buffer[2] == 't') buffer[2] = 'T';
		print(buffer);

		TileIndex vtile = TileVirtXY(v->x_pos, v->y_pos);
		if (v->tile != vtile) {
			seprintf(buffer, lastof(buffer), "  VirtXYTile: %X (%u x %u)", vtile, TileX(vtile), TileY(vtile));
			print(buffer);
		}
		b = buffer + seprintf(buffer, lastof(buffer), "  Position: %X, %X, %X", v->x_pos, v->y_pos, v->z_pos);
		if (v->type == VEH_TRAIN) seprintf(b, lastof(buffer), ", tile margin: %d", GetTileMarginInFrontOfTrain(Train::From(v)));
		print(buffer);

		if (v->IsPrimaryVehicle()) {
			seprintf(buffer, lastof(buffer), "  Order indices: real: %u, implicit: %u, tt: %u",
					v->cur_real_order_index, v->cur_implicit_order_index, v->cur_timetable_order_index);
			print(buffer);
		}
		seprintf(buffer, lastof(buffer), "  V Cache: max speed: %u, cargo age period: %u, vis effect: %u",
				v->vcache.cached_max_speed, v->vcache.cached_cargo_age_period, v->vcache.cached_vis_effect);
		print(buffer);
		if (v->cargo_type != CT_INVALID) {
			seprintf(buffer, lastof(buffer), "  V Cargo: type: %u, sub type: %u, cap: %u, transfer: %u, deliver: %u, keep: %u, load: %u",
					v->cargo_type, v->cargo_subtype, v->cargo_cap,
					v->cargo.ActionCount(VehicleCargoList::MTA_TRANSFER), v->cargo.ActionCount(VehicleCargoList::MTA_DELIVER),
					v->cargo.ActionCount(VehicleCargoList::MTA_KEEP), v->cargo.ActionCount(VehicleCargoList::MTA_LOAD));
			print(buffer);
		}
		if (BaseStation::IsValidID(v->last_station_visited)) {
			seprintf(buffer, lastof(buffer), "  V Last station visited: %u, %s", v->last_station_visited, BaseStation::Get(v->last_station_visited)->GetCachedName());
			print(buffer);
		}
		if (BaseStation::IsValidID(v->last_loading_station)) {
			seprintf(buffer, lastof(buffer), "  V Last loading visited: %u, %s", v->last_loading_station, BaseStation::Get(v->last_loading_station)->GetCachedName());
			print(buffer);
		}
		if (v->IsGroundVehicle()) {
			const GroundVehicleCache &gvc = *(v->GetGroundVehicleCache());
			seprintf(buffer, lastof(buffer), "  GV Cache: weight: %u, slope res: %u, max TE: %u, axle res: %u",
					gvc.cached_weight, gvc.cached_slope_resistance, gvc.cached_max_te, gvc.cached_axle_resistance);
			print(buffer);
			seprintf(buffer, lastof(buffer), "  GV Cache: max track speed: %u, power: %u, air drag: %u",
					gvc.cached_max_track_speed, gvc.cached_power, gvc.cached_air_drag);
			print(buffer);
			seprintf(buffer, lastof(buffer), "  GV Cache: total length: %u, veh length: %u",
					gvc.cached_total_length, gvc.cached_veh_length);
			print(buffer);
		}
		if (v->type == VEH_TRAIN) {
			const Train *t = Train::From(v);
			seprintf(buffer, lastof(buffer), "  T cache: tilt: %u, engines: %u, decel: %u, uncapped decel: %u, centre mass: %u",
					t->tcache.cached_tilt, t->tcache.cached_num_engines, t->tcache.cached_deceleration, t->tcache.cached_uncapped_decel, t->tcache.cached_centre_mass);
			print(buffer);
			seprintf(buffer, lastof(buffer), "  T cache: veh weight: %u, user data: %u, curve speed: %u",
					t->tcache.cached_veh_weight, t->tcache.user_def_data, t->tcache.cached_max_curve_speed);
			print(buffer);
			seprintf(buffer, lastof(buffer), "  Wait counter: %u, rev distance: %u, TBSN: %u, speed restriction: %u",
					t->wait_counter, t->reverse_distance, t->tunnel_bridge_signal_num, t->speed_restriction);
			print(buffer);
			seprintf(buffer, lastof(buffer), "  Railtype: %u, compatible_railtypes: 0x" OTTD_PRINTFHEX64,
					t->railtype, t->compatible_railtypes);
			print(buffer);
			if (t->lookahead != nullptr) {
				print ("  Look ahead:");
				const TrainReservationLookAhead &l = *t->lookahead;
				seprintf(buffer, lastof(buffer), "    Position: current: %d, end: %d, remaining: %d", l.current_position, l.reservation_end_position, l.reservation_end_position - l.current_position);
				print(buffer);
				seprintf(buffer, lastof(buffer), "    Reservation ends at %X (%u x %u), trackdir: %02X, z: %d",
						l.reservation_end_tile, TileX(l.reservation_end_tile), TileY(l.reservation_end_tile), l.reservation_end_trackdir, l.reservation_end_z);
				print(buffer);
				b = buffer + seprintf(buffer, lastof(buffer), "    TB reserved tiles: %d, flags:", l.tunnel_bridge_reserved_tiles);
				if (HasBit(l.flags, TRLF_TB_EXIT_FREE)) b += seprintf(b, lastof(buffer), "x");
				if (HasBit(l.flags, TRLF_DEPOT_END)) b += seprintf(b, lastof(buffer), "d");
				if (HasBit(l.flags, TRLF_APPLY_ADVISORY)) b += seprintf(b, lastof(buffer), "a");
				if (HasBit(l.flags, TRLF_CHUNNEL)) b += seprintf(b, lastof(buffer), "c");
				print(buffer);

				seprintf(buffer, lastof(buffer), "    Items: %u", (uint)l.items.size());
				print(buffer);
				for (const TrainReservationLookAheadItem &item : l.items) {
					b = buffer + seprintf(buffer, lastof(buffer), "      Start: %d (dist: %d), end: %d (dist: %d), z: %d, ",
							item.start, item.start - l.current_position, item.end, item.end - l.current_position, item.z_pos);
					switch (item.type) {
						case TRLIT_STATION:
							b += seprintf(b, lastof(buffer), "station: %u, %s", item.data_id, BaseStation::IsValidID(item.data_id) ? BaseStation::Get(item.data_id)->GetCachedName() : "[invalid]");
							break;
						case TRLIT_REVERSE:
							b += seprintf(b, lastof(buffer), "reverse");
							break;
						case TRLIT_TRACK_SPEED:
							b += seprintf(b, lastof(buffer), "track speed: %u", item.data_id);
							break;
						case TRLIT_SPEED_RESTRICTION:
							b += seprintf(b, lastof(buffer), "speed restriction: %u", item.data_id);
							break;
						case TRLIT_SIGNAL:
							b += seprintf(b, lastof(buffer), "signal: target speed: %u", item.data_id);
							break;
						case TRLIT_CURVE_SPEED:
							b += seprintf(b, lastof(buffer), "curve speed: %u", item.data_id);
							break;
					}
					print(buffer);
				}

				seprintf(buffer, lastof(buffer), "    Curves: %u", (uint)l.curves.size());
				print(buffer);
				for (const TrainReservationLookAheadCurve &curve : l.curves) {
					seprintf(buffer, lastof(buffer), "      Pos: %d (dist: %d), dir diff: %d", curve.position, curve.position - l.current_position, curve.dir_diff);
					print(buffer);
				}
			}
		}
		if (v->type == VEH_ROAD) {
			const RoadVehicle *rv = RoadVehicle::From(v);
			seprintf(buffer, lastof(buffer), "  Overtaking: %u, overtaking_ctr: %u, overtaking threshold: %u",
					rv->overtaking, rv->overtaking_ctr, rv->GetOvertakingCounterThreshold());
			print(buffer);
			seprintf(buffer, lastof(buffer), "  Speed: %u, path cache length: %u",
					rv->cur_speed, (uint) rv->path.size());
			print(buffer);
			seprintf(buffer, lastof(buffer), "  Roadtype: %u (0x" OTTD_PRINTFHEX64 "), Compatible: 0x" OTTD_PRINTFHEX64,
					rv->roadtype, (static_cast<RoadTypes>(1) << rv->roadtype), rv->compatible_roadtypes);
			print(buffer);
		}
		if (v->type == VEH_SHIP) {
			const Ship *s = Ship::From(v);
			seprintf(buffer, lastof(buffer), "  Lost counter: %u",
					s->lost_count);
			print(buffer);
		}
		if (v->type == VEH_AIRCRAFT) {
			const Aircraft *a = Aircraft::From(v);
			seprintf(buffer, lastof(buffer), "  Pos: %u, prev pos: %u, state: %u, flags: 0x%X",
					a->pos, a->previous_pos, a->state, a->flags);
			print(buffer);
		}

		seprintf(buffer, lastof(buffer), "  Cached sprite bounds: (%d, %d) to (%d, %d)",
				v->sprite_seq_bounds.left, v->sprite_seq_bounds.top, v->sprite_seq_bounds.right, v->sprite_seq_bounds.bottom);
		print(buffer);

		if (HasBit(v->vehicle_flags, VF_SEPARATION_ACTIVE)) {
			std::vector<TimetableProgress> progress_array = PopulateSeparationState(v);
			if (!progress_array.empty()) {
				print("Separation state:");
			}
			for (const auto &info : progress_array) {
				b = buffer + seprintf(buffer, lastof(buffer), "  %s [%d, %d, %d], %u, ",
						info.id == v->index ? "*" : " ", info.order_count, info.order_ticks, info.cumulative_ticks, info.id);
				SetDParam(0, info.id);
				b = GetString(b, STR_VEHICLE_NAME, lastof(buffer));
				b += seprintf(b, lastof(buffer), ", lateness: %d", Vehicle::Get(info.id)->lateness_counter);
				print(buffer);
			}
		}

		seprintf(buffer, lastof(buffer), "  Engine: %u", v->engine_type);
		print(buffer);
		const Engine *e = Engine::GetIfValid(v->engine_type);
		if (e != nullptr) {
			YearMonthDay ymd;
			ConvertDateToYMD(e->intro_date, &ymd);
			seprintf(buffer, lastof(buffer), "    Intro: %4i-%02i-%02i, Age: %u, Base life: %u, Durations: %u %u %u (sum: %u)",
					ymd.year, ymd.month + 1, ymd.day, e->age, e->info.base_life, e->duration_phase_1, e->duration_phase_2, e->duration_phase_3,
					e->duration_phase_1 + e->duration_phase_2 + e->duration_phase_3);
			print(buffer);
			if (e->type == VEH_TRAIN) {
				const RailtypeInfo *rti = GetRailTypeInfo(e->u.rail.railtype);
				seprintf(buffer, lastof(buffer), "    Railtype: %u (0x" OTTD_PRINTFHEX64 "), Compatible: 0x" OTTD_PRINTFHEX64 ", Powered: 0x" OTTD_PRINTFHEX64 ", All compatible: 0x" OTTD_PRINTFHEX64,
						e->u.rail.railtype, (static_cast<RailTypes>(1) << e->u.rail.railtype), rti->compatible_railtypes, rti->powered_railtypes, rti->all_compatible_railtypes);
				print(buffer);
			}
			if (e->type == VEH_ROAD) {
				const RoadTypeInfo* rti = GetRoadTypeInfo(e->u.road.roadtype);
				seprintf(buffer, lastof(buffer), "    Roadtype: %u (0x" OTTD_PRINTFHEX64 "), Powered: 0x" OTTD_PRINTFHEX64,
						e->u.road.roadtype, (static_cast<RoadTypes>(1) << e->u.road.roadtype), rti->powered_roadtypes);
				print(buffer);
			}
		}

		seprintf(buffer, lastof(buffer), "  Current image cacheable: %s", v->cur_image_valid_dir != INVALID_DIR ? "yes" : "no");
		print(buffer);
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
	NICS(CBID_STATION_SPRITE_LAYOUT,    CBM_STATION_SPRITE_LAYOUT),
	NICS(CBID_STATION_TILE_LAYOUT,      CBM_NO_BIT),
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
	NIV_END()
};

class NIHStation : public NIHelper {
	bool IsInspectable(uint index) const override        { return GetStationSpec(index) != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, Station::GetByTile(index)->town->index); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return GetStationSpec(index); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, GetStationIndex(index), index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? GetStationSpec(index)->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		StationResolverObject ro(GetStationSpec(index), Station::GetByTile(index), index, INVALID_RAILTYPE);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
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
	NIV_END()
};

class NIHHouse : public NIHelper {
	bool IsInspectable(uint index) const override        { return HouseSpec::Get(GetHouseType(index))->grf_prop.grffile != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, GetTownIndex(index)); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return HouseSpec::Get(GetHouseType(index)); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_TOWN_NAME, GetTownIndex(index), index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? HouseSpec::Get(GetHouseType(index))->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		HouseResolverObject ro(GetHouseType(index), index, Town::GetByTile(index));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, std::function<void(const char *)> print) const override
	{
		char buffer[1024];
		print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  House Type: %u", GetHouseType(index));
		print(buffer);
		const HouseSpec *hs = HouseSpec::Get(GetHouseType(index));
		seprintf(buffer, lastof(buffer), "  building_flags: 0x%X", hs->building_flags);
		print(buffer);
		seprintf(buffer, lastof(buffer), "  extra_flags: 0x%X", hs->extra_flags);
		print(buffer);
		seprintf(buffer, lastof(buffer), "  remove_rating_decrease: %u", hs->remove_rating_decrease);
		print(buffer);
		seprintf(buffer, lastof(buffer), "  population: %u, mail_generation: %u", hs->population, hs->mail_generation);
		print(buffer);
		seprintf(buffer, lastof(buffer), "  animation: frames: %u, status: %u, speed: %u, triggers: 0x%X", hs->animation.frames, hs->animation.status, hs->animation.speed, hs->animation.triggers);
		print(buffer);
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
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_INDUSTRIES, GetIndustryIndex(index)); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return GetIndustryTileSpec(GetIndustryGfx(index)); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_INDUSTRY_NAME, GetIndustryIndex(index), index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? GetIndustryTileSpec(GetIndustryGfx(index))->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		IndustryTileResolverObject ro(GetIndustryGfx(index), index, Industry::GetByTile(index));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, std::function<void(const char *)> print) const override
	{
		char buffer[1024];
		print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  Gfx Index: %u", GetIndustryGfx(index));
		print(buffer);
		const IndustryTileSpec *indts = GetIndustryTileSpec(GetIndustryGfx(index));
		if (indts) {
			seprintf(buffer, lastof(buffer), "  anim_production: %u, anim_next: %u, anim_state: %u, ", indts->anim_production, indts->anim_next, indts->anim_state);
			print(buffer);
			seprintf(buffer, lastof(buffer), "  animation: frames: %u, status: %u, speed: %u, triggers: 0x%X", indts->animation.frames, indts->animation.status, indts->animation.speed, indts->animation.triggers);
			print(buffer);
			seprintf(buffer, lastof(buffer), "  special_flags: 0x%X, enabled: %u", indts->special_flags, indts->enabled);
			print(buffer);
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
	bool ShowExtraInfoOnly(uint index) const override    { return GetIndustrySpec(Industry::Get(index)->type)->grf_prop.grffile == nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, Industry::Get(index)->town->index); }
	const void *GetInstance(uint index)const override    { return Industry::Get(index); }
	const void *GetSpec(uint index) const override       { return GetIndustrySpec(Industry::Get(index)->type); }
	void SetStringParameters(uint index) const override  { this->SetSimpleStringParameters(STR_INDUSTRY_NAME, index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? GetIndustrySpec(Industry::Get(index)->type)->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		Industry *i = Industry::Get(index);
		IndustriesResolverObject ro(i->location.tile, i, i->type);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	uint GetPSASize(uint index, uint32 grfid) const override { return cpp_lengthof(PersistentStorage, storage); }

	const int32 *GetPSAFirstPosition(uint index, uint32 grfid) const override
	{
		const Industry *i = (const Industry *)this->GetInstance(index);
		if (i->psa == nullptr) return nullptr;
		return (int32 *)(&i->psa->storage);
	}

	std::vector<uint32> GetPSAGRFIDs(uint index) const override
	{
		return { 0 };
	}

	void ExtraInfo(uint index, std::function<void(const char *)> print) const override
	{
		char buffer[1024];
		print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  Index: %u", index);
		print(buffer);
		const Industry *ind = Industry::GetIfValid(index);
		if (ind) {
			seprintf(buffer, lastof(buffer), "  Location: %ux%u (%X), w: %u, h: %u", TileX(ind->location.tile), TileY(ind->location.tile), ind->location.tile, ind->location.w, ind->location.h);
			print(buffer);
			if (ind->neutral_station) {
				seprintf(buffer, lastof(buffer), "  Neutral station: %u: %s", ind->neutral_station->index, ind->neutral_station->GetCachedName());
				print(buffer);
			}
			seprintf(buffer, lastof(buffer), "  Nearby stations: %u", (uint) ind->stations_near.size());
			print(buffer);
			for (const Station *st : ind->stations_near) {
				seprintf(buffer, lastof(buffer), "    %u: %s", st->index, st->GetCachedName());
				print(buffer);
			}
			print("  Produces:");
			for (uint i = 0; i < lengthof(ind->produced_cargo); i++) {
				if (ind->produced_cargo[i] != CT_INVALID) {
					seprintf(buffer, lastof(buffer), "    %s: waiting: %u, rate: %u, this month: production: %u, transported: %u, last month: production: %u, transported: %u, (%u/255)",
							GetStringPtr(CargoSpec::Get(ind->produced_cargo[i])->name), ind->produced_cargo_waiting[i], ind->production_rate[i], ind->this_month_production[i],
							ind->this_month_transported[i], ind->last_month_production[i], ind->last_month_transported[i], ind->last_month_pct_transported[i]);
					print(buffer);
				}
			}
			print("  Accepts:");
			for (uint i = 0; i < lengthof(ind->accepts_cargo); i++) {
				if (ind->accepts_cargo[i] != CT_INVALID) {
					seprintf(buffer, lastof(buffer), "    %s: waiting: %u",
							GetStringPtr(CargoSpec::Get(ind->accepts_cargo[i])->name), ind->incoming_cargo_waiting[i]);
					print(buffer);
				}
			}

			const IndustrySpec *indsp = GetIndustrySpec(ind->type);
			seprintf(buffer, lastof(buffer), "  CBM_IND_PRODUCTION_CARGO_ARRIVAL: %s", HasBit(indsp->callback_mask, CBM_IND_PRODUCTION_CARGO_ARRIVAL) ? "yes" : "no");
			print(buffer);
			seprintf(buffer, lastof(buffer), "  CBM_IND_PRODUCTION_256_TICKS: %s", HasBit(indsp->callback_mask, CBM_IND_PRODUCTION_256_TICKS) ? "yes" : "no");
			print(buffer);
			seprintf(buffer, lastof(buffer), "  Counter: %u", ind->counter);
			print(buffer);
			if ((_settings_game.economy.industry_cargo_scale_factor != 0) && HasBit(indsp->callback_mask, CBM_IND_PRODUCTION_256_TICKS)) {
				seprintf(buffer, lastof(buffer), "  Counter production interval: %u", ScaleQuantity(INDUSTRY_PRODUCE_TICKS, -_settings_game.economy.industry_cargo_scale_factor));
				print(buffer);
			}
		}
	}
};

static const NIFeature _nif_industry = {
	_nip_industries,
	_nic_industries,
	_niv_industries,
	new NIHIndustry(),
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
	NIV_END()
};

class NIHObject : public NIHelper {
	bool IsInspectable(uint index) const override        { return ObjectSpec::GetByTile(index)->grf_prop.grffile != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, Object::GetByTile(index)->town->index); }
	const void *GetInstance(uint index)const override    { return Object::GetByTile(index); }
	const void *GetSpec(uint index) const override       { return ObjectSpec::GetByTile(index); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_OBJECT, INVALID_STRING_ID, index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? ObjectSpec::GetByTile(index)->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		ObjectResolverObject ro(ObjectSpec::GetByTile(index), Object::GetByTile(index), index);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, std::function<void(const char *)> print) const override
	{
		char buffer[1024];
		print("Debug Info:");
		const ObjectSpec *spec = ObjectSpec::GetByTile(index);
		if (spec) {
			seprintf(buffer, lastof(buffer), "  animation: frames: %u, status: %u, speed: %u, triggers: 0x%X", spec->animation.frames, spec->animation.status, spec->animation.speed, spec->animation.triggers);
			print(buffer);
		}
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
	NIV_END()
};

void PrintTypeLabels(char * buffer,  const char *last, uint32 label, const uint32 *alternate_labels, size_t alternate_labels_count, std::function<void(const char *)> print)
{
	auto is_printable = [](uint32 l) -> bool {
		for (uint i = 0; i < 4; i++) {
			if ((l & 0xFF) < 0x20 || (l & 0xFF) > 0x7F) return false;
			l >>= 8;
		}
		return true;
	};
	if (is_printable(label)) {
		seprintf(buffer, last, "  Label: %c%c%c%c", label >> 24, label >> 16, label >> 8, label);
	} else {
		seprintf(buffer, last, "  Label: 0x%08X", BSWAP32(label));
	}
	print(buffer);
	if (alternate_labels_count > 0) {
		char * b = buffer;
		b += seprintf(b, last, "  Alternate labels: ");
		for (size_t i = 0; i < alternate_labels_count; i++) {
			if (i != 0) b += seprintf(b, last, ", ");
			uint32 l = alternate_labels[i];
			if (is_printable(l)) {
				b += seprintf(b, last, "%c%c%c%c", l >> 24, l >> 16, l >> 8, l);
			} else {
				b += seprintf(b, last, "0x%08X", BSWAP32(l));
			}
		}
		print(buffer);
	}
}

class NIHRailType : public NIHelper {
	bool IsInspectable(uint index) const override        { return true; }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_RAIL_TYPE, INVALID_STRING_ID, index); }
	uint32 GetGRFID(uint index) const override           { return 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		/* There is no unique GRFFile for the tile. Multiple GRFs can define different parts of the railtype.
		 * However, currently the NewGRF Debug GUI does not display variables depending on the GRF (like 0x7F) anyway. */
		RailTypeResolverObject ro(nullptr, index, TCX_NORMAL, RTSG_END);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, std::function<void(const char *)> print) const override
	{
		char buffer[1024];

		RailType primary = GetTileRailType(index);
		RailType secondary = GetTileSecondaryRailTypeIfValid(index);

		auto writeRailType = [&](RailType type) {
			const RailtypeInfo *info = GetRailTypeInfo(type);
			seprintf(buffer, lastof(buffer), "  Type: %u (0x" OTTD_PRINTFHEX64 ")", type, (static_cast<RailTypes>(1) << type));
			print(buffer);
			seprintf(buffer, lastof(buffer), "  Flags: %c%c%c%c%c%c",
					HasBit(info->flags, RTF_CATENARY) ? 'c' : '-',
					HasBit(info->flags, RTF_NO_LEVEL_CROSSING) ? 'l' : '-',
					HasBit(info->flags, RTF_HIDDEN) ? 'h' : '-',
					HasBit(info->flags, RTF_NO_SPRITE_COMBINE) ? 's' : '-',
					HasBit(info->flags, RTF_ALLOW_90DEG) ? 'a' : '-',
					HasBit(info->flags, RTF_DISALLOW_90DEG) ? 'd' : '-');
			print(buffer);
			seprintf(buffer, lastof(buffer), "  Ctrl flags: %c%c",
					HasBit(info->ctrl_flags, RTCF_PROGSIG) ? 'p' : '-',
					HasBit(info->ctrl_flags, RTCF_RESTRICTEDSIG) ? 'r' : '-');
			print(buffer);
			seprintf(buffer, lastof(buffer), "  Powered: 0x" OTTD_PRINTFHEX64, info->powered_railtypes);
			print(buffer);
			seprintf(buffer, lastof(buffer), "  Compatible: 0x" OTTD_PRINTFHEX64, info->compatible_railtypes);
			print(buffer);
			seprintf(buffer, lastof(buffer), "  All compatible: 0x" OTTD_PRINTFHEX64, info->all_compatible_railtypes);
			print(buffer);
			PrintTypeLabels(buffer, lastof(buffer), info->label, (const uint32*) info->alternate_labels.data(), info->alternate_labels.size(), print);
		};

		print("Debug Info:");
		writeRailType(primary);
		if (secondary != INVALID_RAILTYPE) {
			writeRailType(secondary);
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

class NIHAirportTile : public NIHelper {
	bool IsInspectable(uint index) const override        { return AirportTileSpec::Get(GetAirportGfx(index))->grf_prop.grffile != nullptr; }
	uint GetParent(uint index) const override            { return GetInspectWindowNumber(GSF_FAKE_TOWNS, Station::GetByTile(index)->town->index); }
	const void *GetInstance(uint index)const override    { return nullptr; }
	const void *GetSpec(uint index) const override       { return AirportTileSpec::Get(GetAirportGfx(index)); }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_STATION_NAME, GetStationIndex(index), index); }
	uint32 GetGRFID(uint index) const override           { return (this->IsInspectable(index)) ? AirportTileSpec::Get(GetAirportGfx(index))->grf_prop.grffile->grfid : 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		AirportTileResolverObject ro(AirportTileSpec::GetByTile(index), index, Station::GetByTile(index));
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, std::function<void(const char *)> print) const override
	{
		char buffer[1024];
		print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  Gfx Index: %u", GetAirportGfx(index));
		print(buffer);
		const AirportTileSpec *spec = AirportTileSpec::Get(GetAirportGfx(index));
		if (spec) {
			seprintf(buffer, lastof(buffer), "  animation: frames: %u, status: %u, speed: %u, triggers: 0x%X", spec->animation.frames, spec->animation.status, spec->animation.speed, spec->animation.triggers);
			print(buffer);
		}
	}
};

static const NIFeature _nif_airporttile = {
	nullptr,
	_nic_airporttiles,
	_niv_industrytiles, // Yes, they share this (at least now)
	new NIHAirportTile(),
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
	NIV_END()
};

class NIHTown : public NIHelper {
	bool IsInspectable(uint index) const override        { return Town::IsValidID(index); }
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index)const override    { return Town::Get(index); }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetSimpleStringParameters(STR_TOWN_NAME, index); }
	uint32 GetGRFID(uint index) const override           { return 0; }
	bool PSAWithParameter() const override               { return true; }
	uint GetPSASize(uint index, uint32 grfid) const override { return cpp_lengthof(PersistentStorage, storage); }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		TownResolverObject ro(nullptr, Town::Get(index), true);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	const int32 *GetPSAFirstPosition(uint index, uint32 grfid) const override
	{
		Town *t = Town::Get(index);

		std::list<PersistentStorage *>::iterator iter;
		for (iter = t->psa_list.begin(); iter != t->psa_list.end(); iter++) {
			if ((*iter)->grfid == grfid) return (int32 *)(&(*iter)->storage[0]);
		}

		return nullptr;
	}

	virtual std::vector<uint32> GetPSAGRFIDs(uint index) const override
	{
		Town *t = Town::Get(index);

		std::vector<uint32> output;
		for (const auto &iter : t->psa_list) {
			output.push_back(iter->grfid);
		}
		return output;
	}

	void ExtraInfo(uint index, std::function<void(const char *)> print) const override
	{
		const Town *t = Town::Get(index);
		char buffer[1024];

		print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  Index: %u", index);
		print(buffer);
		seprintf(buffer, lastof(buffer), "  Churches: %u, Stadiums: %u", t->church_count, t->stadium_count);
		print(buffer);

		seprintf(buffer, lastof(buffer), "  Nearby stations: %u", (uint) t->stations_near.size());
		print(buffer);
		for (const Station *st : t->stations_near) {
			seprintf(buffer, lastof(buffer), "    %u: %s", st->index, st->GetCachedName());
			print(buffer);
		}

		if (t->have_ratings != 0) {
			print("  Company ratings:");
			uint8 bit;
			FOR_EACH_SET_BIT(bit, t->have_ratings) {
				seprintf(buffer, lastof(buffer), "    %u: %d", bit, t->ratings[bit]);
				print(buffer);
			}
		}
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
	void SetStringParameters(uint index) const override  { this->SetSimpleStringParameters(STR_STATION_NAME, index); }
	uint32 GetGRFID(uint index) const override           { return 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		return 0;
	}

	void ExtraInfo(uint index, std::function<void(const char *)> print) const override
	{
		char buffer[1024];
		print("Debug Info:");
		seprintf(buffer, lastof(buffer), "  Index: %u", index);
		print(buffer);
		const BaseStation *bst = BaseStation::GetIfValid(index);
		if (!bst) return;
		seprintf(buffer, lastof(buffer), "  Tile: %X (%u x %u)", bst->xy, TileX(bst->xy), TileY(bst->xy));
		print(buffer);
		if (bst->rect.IsEmpty()) {
			print("  rect: empty");
		} else {
			seprintf(buffer, lastof(buffer), "  rect: left: %u, right: %u, top: %u, bottom: %u", bst->rect.left, bst->rect.right, bst->rect.top, bst->rect.bottom);
			print(buffer);
		}
		const Station *st = Station::GetIfValid(index);
		if (st) {
			if (st->industry) {
				seprintf(buffer, lastof(buffer), "  Neutral industry: %u: %s", st->industry->index, st->industry->GetCachedName());
				print(buffer);
			}
			seprintf(buffer, lastof(buffer), "  Nearby industries: %u", (uint) st->industries_near.size());
			print(buffer);
			for (const Industry *ind : st->industries_near) {
				seprintf(buffer, lastof(buffer), "    %u: %s", ind->index, ind->GetCachedName());
				print(buffer);
			}
			seprintf(buffer, lastof(buffer), "  Station tiles: %u", st->station_tiles);
			print(buffer);
			seprintf(buffer, lastof(buffer), "  Delete counter: %u", st->delete_ctr);
			print(buffer);
		}
	}
};

static const NIFeature _nif_station_struct = {
	nullptr,
	nullptr,
	nullptr,
	new NIHStationStruct(),
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
	uint GetParent(uint index) const override            { return UINT32_MAX; }
	const void *GetInstance(uint index) const override   { return nullptr; }
	const void *GetSpec(uint index) const override       { return nullptr; }
	void SetStringParameters(uint index) const override  { this->SetObjectAtStringParameters(STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT_ROAD_TYPE, INVALID_STRING_ID, index); }
	uint32 GetGRFID(uint index) const override           { return 0; }

	uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const override
	{
		/* There is no unique GRFFile for the tile. Multiple GRFs can define different parts of the railtype.
		 * However, currently the NewGRF Debug GUI does not display variables depending on the GRF (like 0x7F) anyway. */
		RoadTypeResolverObject ro(nullptr, index, TCX_NORMAL, ROTSG_END);
		return ro.GetScope(VSG_SCOPE_SELF)->GetVariable(var, param, extra);
	}

	void ExtraInfo(uint index, std::function<void(const char *)> print) const override
	{
		print("Debug Info:");
		auto writeInfo = [&](RoadTramType rtt) {
			RoadType type = GetRoadType(index, rtt);
			if (type == INVALID_ROADTYPE) return;

			char buffer[1024];
			const RoadTypeInfo* rti = GetRoadTypeInfo(type);
			seprintf(buffer, lastof(buffer), "  %s Type: %u (0x" OTTD_PRINTFHEX64 ")", rtt == RTT_TRAM ? "Tram" : "Road", type, (static_cast<RoadTypes>(1) << type));
			print(buffer);
			seprintf(buffer, lastof(buffer), "    Flags: %c%c%c%c%c",
					HasBit(rti->flags, ROTF_CATENARY) ? 'c' : '-',
					HasBit(rti->flags, ROTF_NO_LEVEL_CROSSING) ? 'l' : '-',
					HasBit(rti->flags, ROTF_NO_HOUSES) ? 'X' : '-',
					HasBit(rti->flags, ROTF_HIDDEN) ? 'h' : '-',
					HasBit(rti->flags, ROTF_TOWN_BUILD) ? 'T' : '-');
			print(buffer);
			seprintf(buffer, lastof(buffer), "    Extra Flags: %c%c",
					HasBit(rti->extra_flags, RXTF_NOT_AVAILABLE_AI_GS) ? 's' : '-',
					HasBit(rti->extra_flags, RXTF_NO_TOWN_MODIFICATION) ? 't' : '-');
			print(buffer);
			seprintf(buffer, lastof(buffer), "    Powered: 0x" OTTD_PRINTFHEX64, rti->powered_roadtypes);
			print(buffer);
			PrintTypeLabels(buffer, lastof(buffer), rti->label, (const uint32*) rti->alternate_labels.data(), rti->alternate_labels.size(), print);
		};
		writeInfo(RTT_ROAD);
		writeInfo(RTT_TRAM);
	}
};

static const NIFeature _nif_roadtype = {
	nullptr,
	nullptr,
	_niv_roadtypes,
	new NIHRoadType(),
};

/** Table with all NIFeatures. */
static const NIFeature * const _nifeatures[] = {
	&_nif_vehicle,      // GSF_TRAINS
	&_nif_vehicle,      // GSF_ROADVEHICLES
	&_nif_vehicle,      // GSF_SHIPS
	&_nif_vehicle,      // GSF_AIRCRAFT
	&_nif_station,      // GSF_STATIONS
	nullptr,               // GSF_CANALS (no callbacks/action2 implemented)
	nullptr,               // GSF_BRIDGES (no callbacks/action2)
	&_nif_house,        // GSF_HOUSES
	nullptr,               // GSF_GLOBALVAR (has no "physical" objects)
	&_nif_industrytile, // GSF_INDUSTRYTILES
	&_nif_industry,     // GSF_INDUSTRIES
	nullptr,               // GSF_CARGOES (has no "physical" objects)
	nullptr,               // GSF_SOUNDFX (has no "physical" objects)
	nullptr,               // GSF_AIRPORTS (feature not implemented)
	nullptr,               // GSF_SIGNALS (feature not implemented)
	&_nif_object,       // GSF_OBJECTS
	&_nif_railtype,     // GSF_RAILTYPES
	&_nif_airporttile,  // GSF_AIRPORTTILES
	&_nif_roadtype,     // GSF_ROADTYPES
	&_nif_roadtype,     // GSF_TRAMTYPES
	&_nif_town,         // GSF_FAKE_TOWNS
	&_nif_station_struct,  // GSF_FAKE_STATION_STRUCT
};
static_assert(lengthof(_nifeatures) == GSF_FAKE_END);
