/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_sl.cpp Code handling saving and loading of vehicles */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/vehicle_sl_compat.h"

#include "../vehicle_func.h"
#include "../train.h"
#include "../roadveh.h"
#include "../ship.h"
#include "../aircraft.h"
#include "../station_base.h"
#include "../effectvehicle_base.h"
#include "../company_base.h"
#include "../company_func.h"
#include "../disaster_vehicle.h"

#include <map>
#include <vector>

#include "../safeguards.h"

void AfterLoadVehicles(bool part_of_load);
bool TrainController(Train *v, Vehicle *nomove, bool reverse = true); // From train_cmd.cpp
void ReverseTrainDirection(Train *v);
void ReverseTrainSwapVeh(Train *v, int l, int r);

static std::vector<Trackdir> _path_td;
static std::vector<TileIndex> _path_tile;

namespace upstream_sl {

static uint8_t  _cargo_periods;
static uint16_t _cargo_source;
static uint32_t _cargo_source_xy;
static uint16_t _cargo_count;
static uint16_t _cargo_paid_for;
static Money  _cargo_feeder_share;
static VehicleUnbunchState _unbunch_state;

class SlVehicleCommon : public DefaultSaveLoadHandler<SlVehicleCommon, Vehicle> {
public:
#if defined(_MSC_VER) && (_MSC_VER == 1915 || _MSC_VER == 1916)
	/* This table access private members of other classes; they have this
	 * class as friend. For MSVC CL 19.15 and 19.16 this doesn't work for
	 * "inline static const", so we are forced to wrap the table in a
	 * function. CL 19.16 is the latest for VS2017. */
	inline static const SaveLoad description[] = {{}};
	SaveLoadTable GetDescription() const override {
#else
	inline
#endif
	static const SaveLoad description[] = {
		    SLE_VAR(Vehicle, subtype,               SLE_UINT8),

		    SLE_REF(Vehicle, next,                  REF_VEHICLE_OLD),
		//SLE_CONDVAR(Vehicle, name,                  SLE_NAME,                     SL_MIN_VERSION,  SLV_84),
		SLE_CONDSTR(Vehicle, name,                  SLE_STR | SLF_ALLOW_CONTROL, 0, SLV_84, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, unitnumber,            SLE_FILE_U8  | SLE_VAR_U16,   SL_MIN_VERSION,   SLV_8),
		SLE_CONDVAR(Vehicle, unitnumber,            SLE_UINT16,                   SLV_8, SL_MAX_VERSION),
		    SLE_VAR(Vehicle, owner,                 SLE_UINT8),
		SLE_CONDVAR(Vehicle, tile,                  SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		SLE_CONDVAR(Vehicle, tile,                  SLE_UINT32,                   SLV_6, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, dest_tile,             SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		SLE_CONDVAR(Vehicle, dest_tile,             SLE_UINT32,                   SLV_6, SL_MAX_VERSION),

		SLE_CONDVAR(Vehicle, x_pos,                 SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		SLE_CONDVAR(Vehicle, x_pos,                 SLE_UINT32,                   SLV_6, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, y_pos,                 SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		SLE_CONDVAR(Vehicle, y_pos,                 SLE_UINT32,                   SLV_6, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, z_pos,                 SLE_FILE_U8  | SLE_VAR_I32,   SL_MIN_VERSION, SLV_164),
		SLE_CONDVAR(Vehicle, z_pos,                 SLE_INT32,                  SLV_164, SL_MAX_VERSION),
		    SLE_VAR(Vehicle, direction,             SLE_UINT8),

		    SLE_VAR(Vehicle, spritenum,             SLE_UINT8),
		    SLE_VAR(Vehicle, engine_type,           SLE_UINT16),
		    SLE_VAR(Vehicle, cur_speed,             SLE_UINT16),
		    SLE_VAR(Vehicle, subspeed,              SLE_UINT8),
		    SLE_VAR(Vehicle, acceleration,          SLE_UINT8),
		SLE_CONDVAR(Vehicle, motion_counter,        SLE_UINT32,                   SLV_VEH_MOTION_COUNTER, SL_MAX_VERSION),
		    SLE_VAR(Vehicle, progress,              SLE_UINT8),

		    SLE_VAR(Vehicle, vehstatus,             SLE_UINT8),
		SLE_CONDVAR(Vehicle, last_station_visited,  SLE_FILE_U8  | SLE_VAR_U16,   SL_MIN_VERSION,   SLV_5),
		SLE_CONDVAR(Vehicle, last_station_visited,  SLE_UINT16,                   SLV_5, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, last_loading_station,  SLE_UINT16,                 SLV_182, SL_MAX_VERSION),

		    SLE_VAR(Vehicle, cargo_type,            SLE_UINT8),
		SLE_CONDVAR(Vehicle, cargo_subtype,         SLE_UINT8,                   SLV_35, SL_MAX_VERSION),
		SLEG_CONDVAR("cargo_days", _cargo_periods,  SLE_UINT8,                    SL_MIN_VERSION,  SLV_68),
		SLEG_CONDVAR("cargo_source", _cargo_source, SLE_FILE_U8  | SLE_VAR_U16,   SL_MIN_VERSION,   SLV_7),
		SLEG_CONDVAR("cargo_source", _cargo_source, SLE_UINT16,                   SLV_7,  SLV_68),
		SLEG_CONDVAR("cargo_source_xy", _cargo_source_xy, SLE_UINT32,             SLV_44,  SLV_68),
		    SLE_VAR(Vehicle, cargo_cap,             SLE_UINT16),
		SLE_CONDVAR(Vehicle, refit_cap,             SLE_UINT16,                 SLV_182, SL_MAX_VERSION),
		SLEG_CONDVAR("cargo_count", _cargo_count,   SLE_UINT16,                   SL_MIN_VERSION,  SLV_68),
		SLE_CONDREFRING(Vehicle, cargo.packets,     REF_CARGO_PACKET,            SLV_68, SL_MAX_VERSION),
		SLE_CONDARR(Vehicle, cargo.action_counts,   SLE_UINT, VehicleCargoList::NUM_MOVE_TO_ACTION, SLV_181, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, cargo_age_counter,     SLE_UINT16,                 SLV_162, SL_MAX_VERSION),

		    SLE_VAR(Vehicle, day_counter,           SLE_UINT8),
		    SLE_VAR(Vehicle, tick_counter,          SLE_UINT8),
		SLE_CONDVAR(Vehicle, running_ticks,         SLE_FILE_U8  | SLE_VAR_U16,  SLV_88, SL_MAX_VERSION),

		    SLE_VAR(Vehicle, cur_implicit_order_index,  SLE_FILE_U8 | SLE_VAR_U16),
		SLE_CONDVAR(Vehicle, cur_real_order_index,      SLE_FILE_U8 | SLE_VAR_U16,                  SLV_158, SL_MAX_VERSION),

		/* This next line is for version 4 and prior compatibility.. it temporarily reads
		type and flags (which were both 4 bits) into type. Later on this is
		converted correctly */
		SLE_CONDVAR(Vehicle, current_order.type,    SLE_UINT8,                    SL_MIN_VERSION,   SLV_5),
		SLE_CONDVAR(Vehicle, current_order.dest,    SLE_FILE_U8  | SLE_VAR_U16,   SL_MIN_VERSION,   SLV_5),

		/* Orders for version 5 and on */
		SLE_CONDVAR(Vehicle, current_order.type,    SLE_UINT8,                    SLV_5, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, current_order.flags,   SLE_FILE_U8 | SLE_VAR_U16,    SLV_5, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, current_order.dest,    SLE_UINT16,                   SLV_5, SL_MAX_VERSION),

		/* Refit in current order */
		SLE_CONDVAR(Vehicle, current_order.refit_cargo,   SLE_UINT8,             SLV_36, SL_MAX_VERSION),

		/* Timetable in current order */
		SLE_CONDVAR(Vehicle, current_order.wait_time,     SLE_FILE_U16 | SLE_VAR_U32, SLV_67, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, current_order.travel_time,   SLE_FILE_U16 | SLE_VAR_U32, SLV_67, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, current_order.max_speed,     SLE_UINT16,           SLV_174, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, timetable_start,       SLE_FILE_I32 | SLE_VAR_I64, SLV_129, SLV_TIMETABLE_START_TICKS),
		SLE_CONDVAR(Vehicle, timetable_start,       SLE_FILE_U64 | SLE_VAR_I64, SLV_TIMETABLE_START_TICKS, SL_MAX_VERSION),

		SLE_CONDREF(Vehicle, orders,                REF_ORDER,                    SL_MIN_VERSION, SLV_105),
		SLE_CONDREF(Vehicle, orders,                REF_ORDERLIST,              SLV_105, SL_MAX_VERSION),

		SLE_CONDVAR(Vehicle, age,                   SLE_FILE_U16 | SLE_VAR_I32,   SL_MIN_VERSION,  SLV_31),
		SLE_CONDVAR(Vehicle, age,                   SLE_INT32,                   SLV_31, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, economy_age,           SLE_INT32,  SLV_VEHICLE_ECONOMY_AGE, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, max_age,               SLE_FILE_U16 | SLE_VAR_I32,   SL_MIN_VERSION,  SLV_31),
		SLE_CONDVAR(Vehicle, max_age,               SLE_INT32,                   SLV_31, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, date_of_last_service,  SLE_FILE_U16 | SLE_VAR_I32,   SL_MIN_VERSION,  SLV_31),
		SLE_CONDVAR(Vehicle, date_of_last_service,  SLE_INT32,                   SLV_31, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, date_of_last_service_newgrf, SLE_INT32,             SLV_NEWGRF_LAST_SERVICE, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, service_interval,      SLE_UINT16,                   SL_MIN_VERSION,  SLV_31),
		SLE_CONDVAR(Vehicle, service_interval,      SLE_FILE_U32 | SLE_VAR_U16,  SLV_31, SLV_180),
		SLE_CONDVAR(Vehicle, service_interval,      SLE_UINT16,                 SLV_180, SL_MAX_VERSION),
		    SLE_VAR(Vehicle, reliability,           SLE_UINT16),
		    SLE_VAR(Vehicle, reliability_spd_dec,   SLE_UINT16),
		    SLE_VAR(Vehicle, breakdown_ctr,         SLE_UINT8),
		    SLE_VAR(Vehicle, breakdown_delay,       SLE_UINT8),
		    SLE_VAR(Vehicle, breakdowns_since_last_service, SLE_UINT8),
		    SLE_VAR(Vehicle, breakdown_chance,      SLE_UINT8),
		SLE_CONDVAR(Vehicle, build_year,            SLE_FILE_U8 | SLE_VAR_I32,    SL_MIN_VERSION,  SLV_31),
		SLE_CONDVAR(Vehicle, build_year,            SLE_INT32,                   SLV_31, SL_MAX_VERSION),

		    SLE_VAR(Vehicle, load_unload_ticks,     SLE_UINT16),
		SLEG_CONDVAR("cargo_paid_for", _cargo_paid_for, SLE_UINT16,              SLV_45, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, vehicle_flags,         SLE_FILE_U8  | SLE_VAR_U32,   SLV_40, SLV_180),
		SLE_CONDVAR(Vehicle, vehicle_flags,         SLE_FILE_U16 | SLE_VAR_U32,  SLV_180, SL_MAX_VERSION),

		SLE_CONDVAR(Vehicle, profit_this_year,      SLE_FILE_I32 | SLE_VAR_I64,   SL_MIN_VERSION,  SLV_65),
		SLE_CONDVAR(Vehicle, profit_this_year,      SLE_INT64,                   SLV_65, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, profit_last_year,      SLE_FILE_I32 | SLE_VAR_I64,   SL_MIN_VERSION,  SLV_65),
		SLE_CONDVAR(Vehicle, profit_last_year,      SLE_INT64,                   SLV_65, SL_MAX_VERSION),
		SLEG_CONDVAR("cargo_feeder_share", _cargo_feeder_share, SLE_FILE_I32 | SLE_VAR_I64,  SLV_51,  SLV_65),
		SLEG_CONDVAR("cargo_feeder_share", _cargo_feeder_share, SLE_INT64,                   SLV_65,  SLV_68),
		SLE_CONDVAR(Vehicle, value,                 SLE_FILE_I32 | SLE_VAR_I64,   SL_MIN_VERSION,  SLV_65),
		SLE_CONDVAR(Vehicle, value,                 SLE_INT64,                   SLV_65, SL_MAX_VERSION),

		SLE_CONDVAR(Vehicle, random_bits,           SLE_FILE_U8 | SLE_VAR_U16,    SLV_2, SLV_EXTEND_VEHICLE_RANDOM),
		SLE_CONDVAR(Vehicle, random_bits,           SLE_UINT16,                   SLV_EXTEND_VEHICLE_RANDOM, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, waiting_triggers,      SLE_UINT8,                    SLV_2, SL_MAX_VERSION),

		SLE_CONDREF(Vehicle, next_shared,           REF_VEHICLE,                  SLV_2, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, group_id,              SLE_UINT16,                  SLV_60, SL_MAX_VERSION),

		SLE_CONDVAR(Vehicle, current_order_time,    SLE_UINT32,                  SLV_67, SLV_TIMETABLE_TICKS_TYPE),
		SLE_CONDVAR(Vehicle, current_order_time,    SLE_FILE_I32 | SLE_VAR_U32,  SLV_TIMETABLE_TICKS_TYPE, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, last_loading_tick,     SLE_FILE_U64 | SLE_VAR_I64,  SLV_LAST_LOADING_TICK, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, lateness_counter,      SLE_INT32,                   SLV_67, SL_MAX_VERSION),

		SLEG_CONDVAR("depot_unbunching_last_departure", _unbunch_state.depot_unbunching_last_departure, SLE_UINT64, SLV_DEPOT_UNBUNCHING, SL_MAX_VERSION),
		SLEG_CONDVAR("depot_unbunching_next_departure", _unbunch_state.depot_unbunching_next_departure, SLE_UINT64, SLV_DEPOT_UNBUNCHING, SL_MAX_VERSION),
		SLEG_CONDVAR("round_trip_time",                 _unbunch_state.round_trip_time,                 SLE_INT32,  SLV_DEPOT_UNBUNCHING, SL_MAX_VERSION),
	};
#if defined(_MSC_VER) && (_MSC_VER == 1915 || _MSC_VER == 1916)
		return description;
	}
#endif
	inline const static SaveLoadCompatTable compat_description = _vehicle_common_sl_compat;

	void Save(Vehicle *v) const override
	{
		SlObject(v, this->GetDescription());
	}

	void Load(Vehicle *v) const override
	{
		SlObject(v, this->GetLoadDescription());
	}

	void FixPointers(Vehicle *v) const override
	{
		SlObject(v, this->GetDescription());
	}
};

class SlVehicleTrain : public DefaultSaveLoadHandler<SlVehicleTrain, Vehicle> {
public:
	inline static const SaveLoad description[] = {
		 SLEG_STRUCT("common", SlVehicleCommon),
		     SLE_VAR(Train, crash_anim_pos,      SLE_UINT16),
		     SLE_VAR(Train, force_proceed,       SLE_UINT8),
		     SLE_VAR(Train, railtype,            SLE_UINT8),
		     SLE_VAR(Train, track,               SLE_UINT8),

		 SLE_CONDVAR(Train, flags,               SLE_FILE_U8  | SLE_VAR_U32,   SLV_2, SLV_100),
		 SLE_CONDVAR(Train, flags,               SLE_FILE_U16 | SLE_VAR_U32, SLV_100, SL_MAX_VERSION),
		 SLE_CONDVAR(Train, wait_counter,        SLE_UINT16,                 SLV_136, SL_MAX_VERSION),
		 SLE_CONDVAR(Train, gv_flags,            SLE_UINT16,                 SLV_139, SL_MAX_VERSION),
	};
	inline const static SaveLoadCompatTable compat_description = _vehicle_train_sl_compat;

	void Save(Vehicle *v) const override
	{
		if (v->type != VEH_TRAIN) return;
		SlObject(v, this->GetDescription());
	}

	void Load(Vehicle *v) const override
	{
		if (v->type != VEH_TRAIN) return;
		SlObject(v, this->GetLoadDescription());
		if (v->cur_real_order_index == 0xFF) v->cur_real_order_index = INVALID_VEH_ORDER_ID;
		if (v->cur_implicit_order_index == 0xFF) v->cur_implicit_order_index = INVALID_VEH_ORDER_ID;
	}

	void FixPointers(Vehicle *v) const override
	{
		if (v->type != VEH_TRAIN) return;
		SlObject(v, this->GetDescription());
	}
};

class SlVehicleRoadVeh : public DefaultSaveLoadHandler<SlVehicleRoadVeh, Vehicle> {
public:
	inline static const SaveLoad description[] = {
		  SLEG_STRUCT("common", SlVehicleCommon),
		      SLE_VAR(RoadVehicle, state,                SLE_UINT8),
		      SLE_VAR(RoadVehicle, frame,                SLE_UINT8),
		      SLE_VAR(RoadVehicle, blocked_ctr,          SLE_UINT16),
		      SLE_VAR(RoadVehicle, overtaking,           SLE_UINT8),
		      SLE_VAR(RoadVehicle, overtaking_ctr,       SLE_UINT8),
		      SLE_VAR(RoadVehicle, crashed_ctr,          SLE_UINT16),
		      SLE_VAR(RoadVehicle, reverse_ctr,          SLE_UINT8),
		SLEG_CONDVECTOR("path.td", _path_td,             SLE_UINT8,                  SLV_ROADVEH_PATH_CACHE, SL_MAX_VERSION),
		SLEG_CONDVECTOR("path.tile", _path_tile,         SLE_UINT32,                 SLV_ROADVEH_PATH_CACHE, SL_MAX_VERSION),
		  SLE_CONDVAR(RoadVehicle, gv_flags,             SLE_UINT16,                 SLV_139, SL_MAX_VERSION),
	};
	inline const static SaveLoadCompatTable compat_description = _vehicle_roadveh_sl_compat;

	void Save(Vehicle *v) const override
	{
		if (v->type != VEH_ROAD) return;
		SlObject(v, this->GetDescription());
	}

	void Load(Vehicle *v) const override
	{
		if (v->type != VEH_ROAD) return;
		SlObject(v, this->GetLoadDescription());

		if (!_path_td.empty() && _path_td.size() <= RV_PATH_CACHE_SEGMENTS && _path_td.size() == _path_tile.size()) {
			RoadVehicle *rv = RoadVehicle::From(v);
			rv->cached_path.reset(new RoadVehPathCache());
			rv->cached_path->count = (uint8_t)_path_td.size();
			for (size_t i = 0; i < _path_td.size(); i++) {
				rv->cached_path->td[i] = _path_td[i];
				rv->cached_path->tile[i] = _path_tile[i];
			}
		}
		_path_td.clear();
		_path_tile.clear();
	}

	void FixPointers(Vehicle *v) const override
	{
		if (v->type != VEH_ROAD) return;
		SlObject(v, this->GetDescription());
	}
};

class SlVehicleShip : public DefaultSaveLoadHandler<SlVehicleShip, Vehicle> {
public:
	inline static const SaveLoad description[] = {
		  SLEG_STRUCT("common", SlVehicleCommon),
		      SLE_VAR(Ship, state,                     SLE_UINT8),
		SLEG_CONDVECTOR("path", _path_td,              SLE_UINT8,                  SLV_SHIP_PATH_CACHE, SL_MAX_VERSION),
		  SLE_CONDVAR(Ship, rotation,                  SLE_UINT8,                  SLV_SHIP_ROTATION, SL_MAX_VERSION),
	};
	inline const static SaveLoadCompatTable compat_description = _vehicle_ship_sl_compat;

	void Save(Vehicle *v) const override
	{
		if (v->type != VEH_SHIP) return;
		SlObject(v, this->GetDescription());
	}

	void Load(Vehicle *v) const override
	{
		if (v->type != VEH_SHIP) return;
		SlObject(v, this->GetLoadDescription());

		if (!_path_td.empty()) {
			Ship *s = Ship::From(v);
			s->cached_path.insert(s->cached_path.end(), _path_td.begin(), _path_td.end());
		}
		_path_td.clear();
	}

	void FixPointers(Vehicle *v) const override
	{
		if (v->type != VEH_SHIP) return;
		SlObject(v, this->GetDescription());
	}
};

class SlVehicleAircraft : public DefaultSaveLoadHandler<SlVehicleAircraft, Vehicle> {
public:
	inline static const SaveLoad description[] = {
		 SLEG_STRUCT("common", SlVehicleCommon),
		     SLE_VAR(Aircraft, crashed_counter,       SLE_UINT16),
		     SLE_VAR(Aircraft, pos,                   SLE_UINT8),

		 SLE_CONDVAR(Aircraft, targetairport,         SLE_FILE_U8  | SLE_VAR_U16,   SL_MIN_VERSION, SLV_5),
		 SLE_CONDVAR(Aircraft, targetairport,         SLE_UINT16,                   SLV_5, SL_MAX_VERSION),

		     SLE_VAR(Aircraft, state,                 SLE_UINT8),

		 SLE_CONDVAR(Aircraft, previous_pos,          SLE_UINT8,                    SLV_2, SL_MAX_VERSION),
		 SLE_CONDVAR(Aircraft, last_direction,        SLE_UINT8,                    SLV_2, SL_MAX_VERSION),
		 SLE_CONDVAR(Aircraft, number_consecutive_turns, SLE_UINT8,                 SLV_2, SL_MAX_VERSION),

		 SLE_CONDVAR(Aircraft, turn_counter,          SLE_UINT8,                  SLV_136, SL_MAX_VERSION),
		 SLE_CONDVAR(Aircraft, flags,                 SLE_UINT8,                  SLV_167, SL_MAX_VERSION),
	};
	inline const static SaveLoadCompatTable compat_description = _vehicle_aircraft_sl_compat;

	void Save(Vehicle *v) const override
	{
		if (v->type != VEH_AIRCRAFT) return;
		SlObject(v, this->GetDescription());
	}

	void Load(Vehicle *v) const override
	{
		if (v->type != VEH_AIRCRAFT) return;
		SlObject(v, this->GetLoadDescription());
	}

	void FixPointers(Vehicle *v) const override
	{
		if (v->type != VEH_AIRCRAFT) return;
		SlObject(v, this->GetDescription());
	}
};

class SlVehicleEffect : public DefaultSaveLoadHandler<SlVehicleEffect, Vehicle> {
public:
	inline static const SaveLoad description[] = {
		     SLE_VAR(Vehicle, subtype,               SLE_UINT8),

		 SLE_CONDVAR(Vehicle, tile,                  SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, tile,                  SLE_UINT32,                   SLV_6, SL_MAX_VERSION),

		 SLE_CONDVAR(Vehicle, x_pos,                 SLE_FILE_I16 | SLE_VAR_I32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, x_pos,                 SLE_INT32,                    SLV_6, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, y_pos,                 SLE_FILE_I16 | SLE_VAR_I32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, y_pos,                 SLE_INT32,                    SLV_6, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, z_pos,                 SLE_FILE_U8  | SLE_VAR_I32,   SL_MIN_VERSION, SLV_164),
		 SLE_CONDVAR(Vehicle, z_pos,                 SLE_INT32,                  SLV_164, SL_MAX_VERSION),

		    SLE_VAR2(Vehicle, "sprite_cache.sprite_seq.seq[0].sprite", sprite_seq.seq[0].sprite, SLE_FILE_U16 | SLE_VAR_U32),
		     SLE_VAR(Vehicle, progress,              SLE_UINT8),
		     SLE_VAR(Vehicle, vehstatus,             SLE_UINT8),

		     SLE_VAR(EffectVehicle, animation_state,    SLE_UINT16),
		     SLE_VAR(EffectVehicle, animation_substate, SLE_UINT8),

		 SLE_CONDVAR(Vehicle, spritenum,             SLE_UINT8,                    SLV_2, SL_MAX_VERSION),
	};
	inline const static SaveLoadCompatTable compat_description = _vehicle_effect_sl_compat;

	void Save(Vehicle *v) const override
	{
		if (v->type != VEH_EFFECT) return;
		SlObject(v, this->GetDescription());
	}

	void Load(Vehicle *v) const override
	{
		if (v->type != VEH_EFFECT) return;
		SlObject(v, this->GetLoadDescription());
	}

	void FixPointers(Vehicle *v) const override
	{
		if (v->type != VEH_EFFECT) return;
		SlObject(v, this->GetDescription());
	}
};

class SlVehicleDisaster : public DefaultSaveLoadHandler<SlVehicleDisaster, Vehicle> {
public:
#if defined(_MSC_VER) && (_MSC_VER == 1915 || _MSC_VER == 1916)
	/* This table access private members of other classes; they have this
	 * class as friend. For MSVC CL 19.15 and 19.16 this doesn't work for
	 * "inline static const", so we are forced to wrap the table in a
	 * function. CL 19.16 is the latest for VS2017. */
	inline static const SaveLoad description[] = {{}};
	SaveLoadTable GetDescription() const override {
#else
	inline
#endif
	static const SaveLoad description[] = {
		    SLE_REF(Vehicle, next,                  REF_VEHICLE_OLD),

		    SLE_VAR(Vehicle, subtype,               SLE_UINT8),
		SLE_CONDVAR(Vehicle, tile,                  SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		SLE_CONDVAR(Vehicle, tile,                  SLE_UINT32,                   SLV_6, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, dest_tile,             SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		SLE_CONDVAR(Vehicle, dest_tile,             SLE_UINT32,                   SLV_6, SL_MAX_VERSION),

		SLE_CONDVAR(Vehicle, x_pos,                 SLE_FILE_I16 | SLE_VAR_I32,   SL_MIN_VERSION,   SLV_6),
		SLE_CONDVAR(Vehicle, x_pos,                 SLE_INT32,                    SLV_6, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, y_pos,                 SLE_FILE_I16 | SLE_VAR_I32,   SL_MIN_VERSION,   SLV_6),
		SLE_CONDVAR(Vehicle, y_pos,                 SLE_INT32,                    SLV_6, SL_MAX_VERSION),
		SLE_CONDVAR(Vehicle, z_pos,                 SLE_FILE_U8  | SLE_VAR_I32,   SL_MIN_VERSION, SLV_164),
		SLE_CONDVAR(Vehicle, z_pos,                 SLE_INT32,                  SLV_164, SL_MAX_VERSION),
		    SLE_VAR(Vehicle, direction,             SLE_UINT8),

		    SLE_VAR(Vehicle, owner,                 SLE_UINT8),
		    SLE_VAR(Vehicle, vehstatus,             SLE_UINT8),
		SLE_CONDVARNAME(DisasterVehicle, state, "current_order.dest", SLE_FILE_U8 | SLE_VAR_U16, SL_MIN_VERSION,         SLV_5),
		SLE_CONDVARNAME(DisasterVehicle, state, "current_order.dest", SLE_UINT16,                SLV_5,                  SLV_DISASTER_VEH_STATE),
		SLE_CONDVAR(DisasterVehicle,     state,                       SLE_UINT16,                SLV_DISASTER_VEH_STATE, SL_MAX_VERSION),


		   SLE_VAR2(Vehicle, "sprite_cache.sprite_seq.seq[0].sprite", sprite_seq.seq[0].sprite, SLE_FILE_U16 | SLE_VAR_U32),
		SLE_CONDVAR(Vehicle, age,                   SLE_FILE_U16 | SLE_VAR_I32,   SL_MIN_VERSION,  SLV_31),
		SLE_CONDVAR(Vehicle, age,                   SLE_INT32,                   SLV_31, SL_MAX_VERSION),
		    SLE_VAR(Vehicle, tick_counter,          SLE_UINT8),

		SLE_CONDVAR(DisasterVehicle, image_override,            SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION, SLV_191),
		SLE_CONDVAR(DisasterVehicle, image_override,            SLE_UINT32,                 SLV_191, SL_MAX_VERSION),
		SLE_CONDVAR(DisasterVehicle, big_ufo_destroyer_target,  SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION, SLV_191),
		SLE_CONDVAR(DisasterVehicle, big_ufo_destroyer_target,  SLE_UINT32,                 SLV_191, SL_MAX_VERSION),
		SLE_CONDVAR(DisasterVehicle, flags,                     SLE_UINT8,                  SLV_194, SL_MAX_VERSION),
	};
#if defined(_MSC_VER) && (_MSC_VER == 1915 || _MSC_VER == 1916)
		return description;
	}
#endif
	inline const static SaveLoadCompatTable compat_description = _vehicle_disaster_sl_compat;

	void Save(Vehicle *v) const override
	{
		if (v->type != VEH_DISASTER) return;
		SlObject(v, this->GetDescription());
	}

	void Load(Vehicle *v) const override
	{
		if (v->type != VEH_DISASTER) return;
		SlObject(v, this->GetLoadDescription());
	}

	void FixPointers(Vehicle *v) const override
	{
		if (v->type != VEH_DISASTER) return;
		SlObject(v, this->GetDescription());
	}
};

const static SaveLoad _vehicle_desc[] = {
	SLE_SAVEBYTE(Vehicle, type),
	SLEG_STRUCT("train", SlVehicleTrain),
	SLEG_STRUCT("roadveh", SlVehicleRoadVeh),
	SLEG_STRUCT("ship", SlVehicleShip),
	SLEG_STRUCT("aircraft", SlVehicleAircraft),
	SLEG_STRUCT("effect", SlVehicleEffect),
	SLEG_STRUCT("disaster", SlVehicleDisaster),
};

struct VEHSChunkHandler : ChunkHandler {
	VEHSChunkHandler() : ChunkHandler('VEHS', CH_SPARSE_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_vehicle_desc);

		/* Write the vehicles */
		for (Vehicle *v : Vehicle::Iterate()) {
			SlSetArrayIndex(v->index);
			SlObject(v, _vehicle_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_vehicle_desc, _vehicle_sl_compat);

		int index;

		_cargo_count = 0;

		while ((index = SlIterateArray()) != -1) {
			Vehicle *v;
			VehicleType vtype = (VehicleType)SlReadByte();

			switch (vtype) {
				case VEH_TRAIN:    v = new (index) Train();           break;
				case VEH_ROAD:     v = new (index) RoadVehicle();     break;
				case VEH_SHIP:     v = new (index) Ship();            break;
				case VEH_AIRCRAFT: v = new (index) Aircraft();        break;
				case VEH_EFFECT:   v = new (index) EffectVehicle();   break;
				case VEH_DISASTER: v = new (index) DisasterVehicle(); break;
				case VEH_INVALID: // Savegame shouldn't contain invalid vehicles
				default: SlErrorCorrupt("Invalid vehicle type");
			}

			SlObject(v, slt);

			if (_cargo_count != 0 && IsCompanyBuildableVehicleType(v) && CargoPacket::CanAllocateItem()) {
				/* Don't construct the packet with station here, because that'll fail with old savegames */
				CargoPacket *cp = new CargoPacket(_cargo_count, _cargo_periods, _cargo_source, _cargo_source_xy, _cargo_feeder_share);
				v->cargo.Append(cp);
			}

			if (!IsSavegameVersionBefore(SLV_DEPOT_UNBUNCHING) && _unbunch_state.depot_unbunching_last_departure > 0) {
				v->unbunch_state.reset(new VehicleUnbunchState(_unbunch_state));
				_unbunch_state = {};
			}

#if 0
			/* Old savegames used 'last_station_visited = 0xFF' */
			if (IsSavegameVersionBefore(SLV_5) && v->last_station_visited == 0xFF) {
				v->last_station_visited = INVALID_STATION;
			}

			if (IsSavegameVersionBefore(SLV_182)) v->last_loading_station = INVALID_STATION;

			if (IsSavegameVersionBefore(SLV_5)) {
				/* Convert the current_order.type (which is a mix of type and flags, because
				 *  in those versions, they both were 4 bits big) to type and flags */
				v->current_order.flags = GB(v->current_order.type, 4, 4);
				v->current_order.type &= 0x0F;
			}

			/* Advanced vehicle lists got added */
			if (IsSavegameVersionBefore(SLV_60)) v->group_id = DEFAULT_GROUP;
#endif
		}
	}

	void FixPointers() const override
	{
		for (Vehicle *v : Vehicle::Iterate()) {
			SlObject(v, _vehicle_desc);
		}
	}
};

static const VEHSChunkHandler VEHS;
static const ChunkHandlerRef veh_chunk_handlers[] = {
	VEHS,
};

extern const ChunkHandlerTable _veh_chunk_handlers(veh_chunk_handlers);

}
