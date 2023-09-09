/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_sl.cpp Code handling saving and loading of stations. */

#include "../stdafx.h"
#include "../station_base.h"
#include "../waypoint_base.h"
#include "../roadstop_base.h"
#include "../vehicle_base.h"
#include "../newgrf_station.h"
#include "../newgrf_roadstop.h"
#include "../core/math_func.hpp"

#include "saveload.h"
#include "saveload_buffer.h"
#include "table/strings.h"

#include "../safeguards.h"

static byte _old_last_vehicle_type;
static uint8 _num_specs;
static uint8 _num_roadstop_specs;
static uint32 _num_roadstop_custom_tiles;
static std::vector<TileIndex> _custom_road_stop_tiles;
static std::vector<uint16> _custom_road_stop_data;

/**
 * Update the buoy orders to be waypoint orders.
 * @param o the order 'list' to check.
 */
static void UpdateWaypointOrder(Order *o)
{
	if (!o->IsType(OT_GOTO_STATION)) return;

	const Station *st = Station::Get(o->GetDestination());
	if ((st->had_vehicle_of_type & HVOT_WAYPOINT) == 0) return;

	o->MakeGoToWaypoint(o->GetDestination());
}

/**
 * Perform all steps to upgrade from the old station buoys to the new version
 * that uses waypoints. This includes some old saveload mechanics.
 */
void MoveBuoysToWaypoints()
{
	/* Buoy orders become waypoint orders */
	for (OrderList *ol : OrderList::Iterate()) {
		VehicleType vt = ol->GetFirstSharedVehicle()->type;
		if (vt != VEH_SHIP && vt != VEH_TRAIN) continue;

		for (Order *o = ol->GetFirstOrder(); o != nullptr; o = o->next) UpdateWaypointOrder(o);
	}

	for (Vehicle *v : Vehicle::Iterate()) {
		VehicleType vt = v->type;
		if (vt != VEH_SHIP && vt != VEH_TRAIN) continue;

		UpdateWaypointOrder(&v->current_order);
	}

	/* Now make the stations waypoints */
	for (Station *st : Station::Iterate()) {
		if ((st->had_vehicle_of_type & HVOT_WAYPOINT) == 0) continue;

		StationID index    = st->index;
		TileIndex xy       = st->xy;
		Town *town         = st->town;
		StringID string_id = st->string_id;
		TinyString name    = std::move(st->name);
		Date build_date    = st->build_date;
		/* TTDPatch could use "buoys with rail station" for rail waypoints */
		bool train         = st->train_station.tile != INVALID_TILE;
		TileArea train_st  = st->train_station;

		/* Delete the station, so we can make it a real waypoint. */
		delete st;

		/* Stations and waypoints are in the same pool, so if a station
		 * is deleted there must be place for a Waypoint. */
		assert(Waypoint::CanAllocateItem());
		Waypoint *wp   = new (index) Waypoint(xy);
		wp->town       = town;
		wp->string_id  = train ? STR_SV_STNAME_WAYPOINT : STR_SV_STNAME_BUOY;
		wp->name       = std::move(name);
		wp->delete_ctr = 0; // Just reset delete counter for once.
		wp->build_date = build_date;
		wp->owner      = train ? GetTileOwner(xy) : OWNER_NONE;

		if (IsInsideBS(string_id, STR_SV_STNAME_BUOY, 9)) wp->town_cn = string_id - STR_SV_STNAME_BUOY;

		if (train) {
			/* When we make a rail waypoint of the station, convert the map as well. */
			for (TileIndex t : train_st) {
				if (!IsTileType(t, MP_STATION) || GetStationIndex(t) != index) continue;

				SB(_me[t].m6, 3, 3, STATION_WAYPOINT);
				wp->rect.BeforeAddTile(t, StationRect::ADD_FORCE);
			}

			wp->train_station = train_st;
			wp->facilities |= FACIL_TRAIN;
		} else if (IsBuoyTile(xy) && GetStationIndex(xy) == index) {
			wp->rect.BeforeAddTile(xy, StationRect::ADD_FORCE);
			wp->facilities |= FACIL_DOCK;
		}
	}
}

void AfterLoadStations()
{
	/* Update the speclists of all stations to point to the currently loaded custom stations. */
	for (BaseStation *st : BaseStation::Iterate()) {
		for (uint i = 0; i < st->speclist.size(); i++) {
			if (st->speclist[i].grfid == 0) continue;

			st->speclist[i].spec = StationClass::GetByGrf(st->speclist[i].grfid, st->speclist[i].localidx, nullptr);
		}
		for (uint i = 0; i < st->roadstop_speclist.size(); i++) {
			if (st->roadstop_speclist[i].grfid == 0) continue;

			st->roadstop_speclist[i].spec = RoadStopClass::GetByGrf(st->roadstop_speclist[i].grfid, st->roadstop_speclist[i].localidx, nullptr);
		}

		if (Station::IsExpected(st)) {
			Station *sta = Station::From(st);
			for (const RoadStop *rs = sta->bus_stops; rs != nullptr; rs = rs->next) sta->bus_station.Add(rs->xy);
			for (const RoadStop *rs = sta->truck_stops; rs != nullptr; rs = rs->next) sta->truck_station.Add(rs->xy);
		}

		StationUpdateCachedTriggers(st);
		StationUpdateRoadStopCachedTriggers(st);
	}
}

/**
 * (Re)building of road stop caches after loading a savegame.
 */
void AfterLoadRoadStops()
{
	/* First construct the drive through entries */
	for (RoadStop *rs : RoadStop::Iterate()) {
		if (IsDriveThroughStopTile(rs->xy)) rs->MakeDriveThrough();
	}
	/* And then rebuild the data in those entries */
	for (RoadStop *rs : RoadStop::Iterate()) {
		if (!HasBit(rs->status, RoadStop::RSSFB_BASE_ENTRY)) continue;

		rs->GetEntry(DIAGDIR_NE)->Rebuild(rs);
		rs->GetEntry(DIAGDIR_NW)->Rebuild(rs);
	}
}

static const SaveLoad _roadstop_desc[] = {
	SLE_VAR(RoadStop, xy,           SLE_UINT32),
	SLE_CONDNULL(1, SL_MIN_VERSION, SLV_45),
	SLE_VAR(RoadStop, status,       SLE_UINT8),
	/* Index was saved in some versions, but this is not needed */
	SLE_CONDNULL(4, SL_MIN_VERSION, SLV_9),
	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_45),
	SLE_CONDNULL(1, SL_MIN_VERSION, SLV_26),

	SLE_REF(RoadStop, next,         REF_ROADSTOPS),
	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_45),

	SLE_CONDNULL(4, SL_MIN_VERSION, SLV_25),
	SLE_CONDNULL(1, SLV_25, SLV_26),
};

static const SaveLoad _old_station_desc[] = {
	SLE_CONDVAR(Station, xy,                         SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6),
	SLE_CONDVAR(Station, xy,                         SLE_UINT32,                  SLV_6, SL_MAX_VERSION),
	SLE_CONDNULL(4, SL_MIN_VERSION, SLV_6),  ///< bus/lorry tile
	SLE_CONDVAR(Station, train_station.tile,         SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6),
	SLE_CONDVAR(Station, train_station.tile,         SLE_UINT32,                  SLV_6, SL_MAX_VERSION),
	SLE_CONDVAR(Station, airport.tile,               SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6),
	SLE_CONDVAR(Station, airport.tile,               SLE_UINT32,                  SLV_6, SL_MAX_VERSION),
	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_6),
	SLE_CONDNULL(4, SLV_6, SLV_MULTITILE_DOCKS),
	    SLE_REF(Station, town,                       REF_TOWN),
	    SLE_VAR(Station, train_station.w,            SLE_FILE_U8 | SLE_VAR_U16),
	SLE_CONDVAR(Station, train_station.h,            SLE_FILE_U8 | SLE_VAR_U16,   SLV_2, SL_MAX_VERSION),

	SLE_CONDNULL(1, SL_MIN_VERSION, SLV_4),  ///< alpha_order

	    SLE_VAR(Station, string_id,                  SLE_STRINGID),
	SLE_CONDSTR(Station, name,                       SLE_STR | SLF_ALLOW_CONTROL, 0, SLV_84, SL_MAX_VERSION),
	SLE_CONDVAR(Station, indtype,                    SLE_UINT8,                 SLV_103, SL_MAX_VERSION),
	SLE_CONDVAR(Station, had_vehicle_of_type,        SLE_FILE_U16 | SLE_VAR_U8,   SL_MIN_VERSION, SLV_122),
	SLE_CONDVAR(Station, had_vehicle_of_type,        SLE_UINT8,                 SLV_122, SL_MAX_VERSION),

	    SLE_VAR(Station, time_since_load,            SLE_UINT8),
	    SLE_VAR(Station, time_since_unload,          SLE_UINT8),
	SLE_CONDVAR_X(Station, delete_ctr,               SLE_UINT8,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 0, 3)),
	SLE_CONDVAR_X(Station, delete_ctr,               SLE_FILE_U16  | SLE_VAR_U8,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 4)),
	    SLE_VAR(Station, owner,                      SLE_UINT8),
	    SLE_VAR(Station, facilities,                 SLE_UINT8),
	    SLE_VAR(Station, airport.type,               SLE_UINT8),

	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_6),  ///< Truck/bus stop status
	SLE_CONDNULL(1, SL_MIN_VERSION, SLV_5),  ///< Blocked months

	SLE_CONDVAR(Station, airport.flags,              SLE_VAR_U64 | SLE_FILE_U16,  SL_MIN_VERSION,  SLV_3),
	SLE_CONDVAR(Station, airport.flags,              SLE_VAR_U64 | SLE_FILE_U32,  SLV_3, SLV_46),
	SLE_CONDVAR(Station, airport.flags,              SLE_UINT64,                 SLV_46, SL_MAX_VERSION),

	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_26), ///< last-vehicle
	SLEG_CONDVAR_X(_old_last_vehicle_type,           SLE_UINT8,                  SLV_26, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ST_LAST_VEH_TYPE, 0, 0)),
	SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),

	SLE_CONDNULL(2, SLV_3, SLV_26), ///< custom station class and id
	SLE_CONDVAR(Station, build_date,                 SLE_FILE_U16 | SLE_VAR_I32,  SLV_3, SLV_31),
	SLE_CONDVAR(Station, build_date,                 SLE_INT32,                  SLV_31, SL_MAX_VERSION),

	SLE_CONDREF(Station, bus_stops,                  REF_ROADSTOPS,               SLV_6, SL_MAX_VERSION),
	SLE_CONDREF(Station, truck_stops,                REF_ROADSTOPS,               SLV_6, SL_MAX_VERSION),

	/* Used by newstations for graphic variations */
	SLE_CONDVAR(Station, random_bits,                SLE_UINT16,                 SLV_27, SL_MAX_VERSION),
	SLE_CONDVAR(Station, waiting_triggers,           SLE_UINT8,                  SLV_27, SL_MAX_VERSION),
	SLEG_CONDVAR(_num_specs,                         SLE_UINT8,                  SLV_27, SL_MAX_VERSION),

	SLE_CONDVEC(Station, loading_vehicles,           REF_VEHICLE,                SLV_57, SL_MAX_VERSION),

	/* reserve extra space in savegame here. (currently 32 bytes) */
	SLE_CONDNULL(32, SLV_2, SL_MAX_VERSION),
};

static uint16 _waiting_acceptance;
static uint32 _num_flows;
static uint16 _cargo_source;
static uint32 _cargo_source_xy;
static uint8  _cargo_periods;
static Money  _cargo_feeder_share;
static uint   _cargo_reserved_count;

static const SaveLoad _station_speclist_desc[] = {
	SLE_CONDVAR(StationSpecList, grfid,    SLE_UINT32, SLV_27, SL_MAX_VERSION),
	SLE_CONDVAR_X(StationSpecList, localidx, SLE_FILE_U8 | SLE_VAR_U16, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_NEWGRF_ENTITY_EXTRA, 0, 1)),
	SLE_CONDVAR_X(StationSpecList, localidx, SLE_UINT16,                SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_NEWGRF_ENTITY_EXTRA, 2)),
};

static const SaveLoad _roadstop_speclist_desc[] = {
	SLE_CONDVAR(RoadStopSpecList, grfid,    SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION),
	SLE_CONDVAR_X(RoadStopSpecList, localidx, SLE_FILE_U8 | SLE_VAR_U16, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS, 0, 2)),
	SLE_CONDVAR_X(RoadStopSpecList, localidx, SLE_UINT16,                SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS, 3)),
};

CargoPacketList _packets;
uint32 _num_dests;

struct FlowSaveLoad {
	FlowSaveLoad() : source(0), via(0), share(0), restricted(false) {}
	StationID source;
	StationID via;
	uint32 share;
	bool restricted;
};

#if 0
static const SaveLoad _flow_desc[] = {
	    SLE_VAR(FlowSaveLoad, source,     SLE_UINT16),
	    SLE_VAR(FlowSaveLoad, via,        SLE_UINT16),
	    SLE_VAR(FlowSaveLoad, share,      SLE_UINT32),
	SLE_CONDVAR(FlowSaveLoad, restricted, SLE_BOOL, SLV_187, SL_MAX_VERSION),
};
#endif

/**
 * Wrapper function to get the GoodsEntry's internal structure while
 * some of the variables itself are private.
 * @return the saveload description for GoodsEntry.
 */
SaveLoadTable GetGoodsDesc()
{
	static const SaveLoad goods_desc[] = {
		SLEG_CONDVAR(            _waiting_acceptance,  SLE_UINT16,                  SL_MIN_VERSION, SLV_68),
		 SLE_CONDVAR(GoodsEntry, status,               SLE_UINT8,                  SLV_68, SL_MAX_VERSION),
		SLE_CONDNULL(2,                                                            SLV_51, SLV_68),
		     SLE_VAR(GoodsEntry, time_since_pickup,    SLE_UINT8),
		 SLE_CONDNULL_X(6,                                                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 4)),
		     SLE_VAR(GoodsEntry, rating,               SLE_UINT8),
		SLEG_CONDVAR(            _cargo_source,        SLE_FILE_U8 | SLE_VAR_U16,   SL_MIN_VERSION, SLV_7),
		SLEG_CONDVAR(            _cargo_source,        SLE_UINT16,                  SLV_7, SLV_68),
		SLEG_CONDVAR(            _cargo_source_xy,     SLE_UINT32,                 SLV_44, SLV_68),
		SLEG_CONDVAR(            _cargo_periods,       SLE_UINT8,                   SL_MIN_VERSION, SLV_68),
		     SLE_VAR(GoodsEntry, last_speed,           SLE_UINT8),
		     SLE_VAR(GoodsEntry, last_age,             SLE_UINT8),
		SLEG_CONDVAR(            _cargo_feeder_share,  SLE_FILE_U32 | SLE_VAR_I64, SLV_14, SLV_65),
		SLEG_CONDVAR(            _cargo_feeder_share,  SLE_INT64,                  SLV_65, SLV_68),
		 SLE_CONDVAR(GoodsEntry, amount_fract,         SLE_UINT8,                 SLV_150, SL_MAX_VERSION),
		SLEG_CONDPTRRING_X(      _packets,             REF_CARGO_PACKET,           SLV_68, SLV_183, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, 0, 0)),
		SLEG_CONDVAR_X(          _num_dests,           SLE_UINT32,                SLV_183, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_OR, XSLFI_CHILLPP)),
		SLEG_CONDVAR(            _cargo_reserved_count,SLE_UINT,                  SLV_181, SL_MAX_VERSION),
		 SLE_CONDVAR(GoodsEntry, link_graph,           SLE_UINT16,                SLV_183, SL_MAX_VERSION),
		 SLE_CONDVAR(GoodsEntry, node,                 SLE_UINT16,                SLV_183, SL_MAX_VERSION),
		SLEG_CONDVAR(            _num_flows,           SLE_UINT32,                SLV_183, SL_MAX_VERSION),
		 SLE_CONDVAR(GoodsEntry, max_waiting_cargo,    SLE_UINT32,                SLV_183, SL_MAX_VERSION),
		 SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
		SLE_CONDVAR_X(GoodsEntry, last_vehicle_type,   SLE_UINT8,          SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ST_LAST_VEH_TYPE, 1)),
	};

	return goods_desc;
}

typedef std::pair<const StationID, CargoPacketList> StationCargoPair;

static const SaveLoad _cargo_list_desc[] = {
	SLE_VAR(StationCargoPair, first,  SLE_UINT16),
	SLE_PTRRING(StationCargoPair, second, REF_CARGO_PACKET),
};

/**
 * Swap the temporary packets with the packets without specific destination in
 * the given goods entry. Assert that at least one of those is empty.
 * @param ge Goods entry to swap with.
 */
static void SwapPackets(GoodsEntry *ge)
{
	if (_packets.empty() && ge->data == nullptr) return;

	StationCargoPacketMap &ge_packets = const_cast<StationCargoPacketMap &>(*ge->CreateData().cargo.Packets());

	if (_packets.empty()) {
		std::map<StationID, CargoPacketList>::iterator it(ge_packets.find(INVALID_STATION));
		if (it == ge_packets.end()) {
			return;
		} else {
			it->second.swap(_packets);
		}
	} else {
		assert(ge_packets[INVALID_STATION].empty());
		ge_packets[INVALID_STATION].swap(_packets);
	}
}

static void Load_STNS()
{
	_cargo_source_xy = 0;
	_cargo_periods = 0;
	_cargo_feeder_share = 0;
	_num_specs = 0;
	_cargo_reserved_count = 0;

	uint num_cargo = IsSavegameVersionBefore(SLV_55) ? 12 : IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	int index;
	while ((index = SlIterateArray()) != -1) {
		Station *st = new (index) Station();

		SlObject(st, _old_station_desc);

		_waiting_acceptance = 0;

		for (CargoID i = 0; i < num_cargo; i++) {
			GoodsEntry *ge = &st->goods[i];
			SlObject(ge, GetGoodsDesc());
			if (_cargo_reserved_count) ge->CreateData().cargo.LoadSetReservedCount(_cargo_reserved_count);
			SwapPackets(ge);
			if (IsSavegameVersionBefore(SLV_68)) {
				SB(ge->status, GoodsEntry::GES_ACCEPTANCE, 1, HasBit(_waiting_acceptance, 15));
				if (GB(_waiting_acceptance, 0, 12) != 0) {
					/* In old versions, enroute_from used 0xFF as INVALID_STATION */
					StationID source = (IsSavegameVersionBefore(SLV_7) && _cargo_source == 0xFF) ? INVALID_STATION : _cargo_source;

					/* Make sure we can allocate the CargoPacket. This is safe
					 * as there can only be ~64k stations and 32 cargoes in these
					 * savegame versions. As the CargoPacketPool has more than
					 * 16 million entries; it fits by an order of magnitude. */
					assert(CargoPacket::CanAllocateItem());

					/* Don't construct the packet with station here, because that'll fail with old savegames */
					CargoPacket *cp = new CargoPacket(GB(_waiting_acceptance, 0, 12), _cargo_periods, source, _cargo_source_xy, _cargo_feeder_share);
					ge->CreateData().cargo.Append(cp, INVALID_STATION);
					SB(ge->status, GoodsEntry::GES_RATING, 1, 1);
				}
			}
			if (SlXvIsFeatureMissing(XSLFI_ST_LAST_VEH_TYPE)) ge->last_vehicle_type = _old_last_vehicle_type;
		}

		if (_num_specs != 0) {
			/* Allocate speclist memory when loading a game */
			st->speclist.resize(_num_specs);
			for (uint i = 0; i < st->speclist.size(); i++) {
				SlObject(&st->speclist[i], _station_speclist_desc);
			}
		}
	}
}

static void Ptrs_STNS()
{
	/* Don't run when savegame version is higher than or equal to 123. */
	if (!IsSavegameVersionBefore(SLV_123)) return;

	uint num_cargo = IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	for (Station *st : Station::Iterate()) {
		if (!IsSavegameVersionBefore(SLV_68)) {
			for (CargoID i = 0; i < num_cargo; i++) {
				GoodsEntry *ge = &st->goods[i];
				SwapPackets(ge);
				SlObject(ge, GetGoodsDesc());
				SwapPackets(ge);
			}
		}
		SlObject(st, _old_station_desc);
	}
}


static const SaveLoad _base_station_desc[] = {
	      SLE_VAR(BaseStation, xy,                     SLE_UINT32),
	      SLE_REF(BaseStation, town,                   REF_TOWN),
	      SLE_VAR(BaseStation, string_id,              SLE_STRINGID),
	      SLE_STR(BaseStation, name,                   SLE_STR | SLF_ALLOW_CONTROL, 0),

	SLE_CONDVAR_X(Station,     delete_ctr,             SLE_UINT8,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 0, 3)),
	SLE_CONDVAR_X(Station,     delete_ctr,             SLE_FILE_U16  | SLE_VAR_U8,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 4)),
	      SLE_VAR(BaseStation, owner,                  SLE_UINT8),
	      SLE_VAR(BaseStation, facilities,             SLE_UINT8),
	      SLE_VAR(BaseStation, build_date,             SLE_INT32),

	/* Used by newstations for graphic variations */
	      SLE_VAR(BaseStation, random_bits,            SLE_UINT16),
	      SLE_VAR(BaseStation, waiting_triggers,       SLE_UINT8),
	      SLEG_VAR(_num_specs,                         SLE_UINT8),
	SLEG_CONDVAR_X(_num_roadstop_specs,                SLE_UINT8,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS)),
	SLEG_CONDVARVEC_X(_custom_road_stop_tiles,         SLE_UINT32,             SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS, 1, 1)),
	SLEG_CONDVARVEC_X(_custom_road_stop_data,          SLE_UINT16,             SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS, 1, 1)),
	SLEG_CONDVAR_X(_num_roadstop_custom_tiles,         SLE_UINT32,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS, 2)),
};

static OldPersistentStorage _old_st_persistent_storage;

static const SaveLoad _station_desc[] = {
	SLE_WRITEBYTE(Station, facilities),
	SLE_ST_INCLUDE(),

	      SLE_VAR(Station, train_station.tile,         SLE_UINT32),
	      SLE_VAR(Station, train_station.w,            SLE_FILE_U8 | SLE_VAR_U16),
	      SLE_VAR(Station, train_station.h,            SLE_FILE_U8 | SLE_VAR_U16),

	      SLE_REF(Station, bus_stops,                  REF_ROADSTOPS),
	      SLE_REF(Station, truck_stops,                REF_ROADSTOPS),
	SLE_CONDVAR_X(Station, ship_station.tile,          SLE_UINT32,                SL_MIN_VERSION,      SLV_MULTITILE_DOCKS, SlXvFeatureTest(XSLFTO_AND, XSLFI_MULTIPLE_DOCKS, 0, 0)),
	SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_MULTIPLE_DOCKS, 1, 1)),
	  SLE_CONDVAR(Station, ship_station.tile,          SLE_UINT32,                SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, ship_station.w,             SLE_FILE_U8 | SLE_VAR_U16, SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, ship_station.h,             SLE_FILE_U8 | SLE_VAR_U16, SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, docking_station.tile,       SLE_UINT32,                SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, docking_station.w,          SLE_FILE_U8 | SLE_VAR_U16, SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, docking_station.h,          SLE_FILE_U8 | SLE_VAR_U16, SLV_MULTITILE_DOCKS, SL_MAX_VERSION),
	SLE_CONDVARVEC_X(Station, docking_tiles,           SLE_UINT32,                     SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_MULTIPLE_DOCKS, 2)),
	      SLE_VAR(Station, airport.tile,               SLE_UINT32),
	  SLE_CONDVAR(Station, airport.w,                  SLE_FILE_U8 | SLE_VAR_U16, SLV_140, SL_MAX_VERSION),
	  SLE_CONDVAR(Station, airport.h,                  SLE_FILE_U8 | SLE_VAR_U16, SLV_140, SL_MAX_VERSION),
	      SLE_VAR(Station, airport.type,               SLE_UINT8),
	  SLE_CONDVAR(Station, airport.layout,             SLE_UINT8,                 SLV_145, SL_MAX_VERSION),
	SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 1, 6)),
	      SLE_VAR(Station, airport.flags,              SLE_UINT64),
	SLE_CONDNULL_X(8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 1, 6)),
	  SLE_CONDVAR(Station, airport.rotation,           SLE_UINT8,                 SLV_145, SL_MAX_VERSION),
	 SLEG_CONDARR(_old_st_persistent_storage.storage,  SLE_UINT32, 16,            SLV_145, SLV_161),
	  SLE_CONDREF(Station, airport.psa,                REF_STORAGE,               SLV_161, SL_MAX_VERSION),

	      SLE_VAR(Station, indtype,                    SLE_UINT8),
	SLE_CONDVAR_X(Station, extra_name_index,           SLE_UINT16,          SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_EXTRA_STATION_NAMES)),

	      SLE_VAR(Station, time_since_load,            SLE_UINT8),
	      SLE_VAR(Station, time_since_unload,          SLE_UINT8),
	SLEG_CONDVAR_X(_old_last_vehicle_type,             SLE_UINT8,           SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ST_LAST_VEH_TYPE, 0, 0)),
	SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	      SLE_VAR(Station, had_vehicle_of_type,        SLE_UINT8),
	      SLE_VEC(Station, loading_vehicles,           REF_VEHICLE),
	  SLE_CONDVAR(Station, always_accepted,            SLE_FILE_U32 | SLE_VAR_U64, SLV_127, SLV_EXTEND_CARGOTYPES),
	  SLE_CONDVAR(Station, always_accepted,            SLE_UINT64,                 SLV_EXTEND_CARGOTYPES, SL_MAX_VERSION),
	  SLE_CONDNULL_X(32 * 24, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP, SL_JOKER_1_22)),
	SLE_CONDVAR_X(Station, station_cargo_history_cargoes, SLE_UINT64,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_STATION_CARGO_HISTORY)),
};

static const SaveLoad _waypoint_desc[] = {
	SLE_WRITEBYTE(Waypoint, facilities),
	SLE_ST_INCLUDE(),

	      SLE_VAR(Waypoint, town_cn,                   SLE_UINT16),

	  SLE_CONDVAR(Waypoint, train_station.tile,        SLE_UINT32,                  SLV_124, SL_MAX_VERSION),
	  SLE_CONDVAR(Waypoint, train_station.w,           SLE_FILE_U8 | SLE_VAR_U16,   SLV_124, SL_MAX_VERSION),
	  SLE_CONDVAR(Waypoint, train_station.h,           SLE_FILE_U8 | SLE_VAR_U16,   SLV_124, SL_MAX_VERSION),
	SLE_CONDVAR_X(Waypoint, waypoint_flags,            SLE_UINT16,           SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_WAYPOINT_FLAGS)),
	SLE_CONDVAR_X(Waypoint, road_waypoint_area.tile,   SLE_UINT32,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ROAD_WAYPOINTS)),
	SLE_CONDVAR_X(Waypoint, road_waypoint_area.w,      SLE_FILE_U8 | SLE_VAR_U16,   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ROAD_WAYPOINTS)),
	SLE_CONDVAR_X(Waypoint, road_waypoint_area.h,      SLE_FILE_U8 | SLE_VAR_U16,   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ROAD_WAYPOINTS)),
};

static const SaveLoad _custom_roadstop_tile_data_desc[] = {
	SLE_VAR(RoadStopTileData, tile,            SLE_UINT32),
	SLE_VAR(RoadStopTileData, random_bits,     SLE_UINT8),
	SLE_VAR(RoadStopTileData, animation_frame, SLE_UINT8),
};

/**
 * Get the base station description to be used for SL_ST_INCLUDE
 * @return the base station description.
 */
SaveLoadTable GetBaseStationDescription()
{
	return _base_station_desc;
}

std::vector<SaveLoad> _filtered_station_desc;
std::vector<SaveLoad> _filtered_waypoint_desc;
std::vector<SaveLoad> _filtered_goods_desc;
std::vector<SaveLoad> _filtered_station_speclist_desc;
std::vector<SaveLoad> _filtered_roadstop_speclist_desc;

static void SetupDescs_STNN()
{
	_filtered_station_desc = SlFilterObject(_station_desc);
	_filtered_waypoint_desc = SlFilterObject(_waypoint_desc);
	_filtered_goods_desc = SlFilterObject(GetGoodsDesc());
	_filtered_station_speclist_desc = SlFilterObject(_station_speclist_desc);
	_filtered_roadstop_speclist_desc = SlFilterObject(_roadstop_speclist_desc);
}

std::vector<SaveLoad> _filtered_roadstop_desc;

static void SetupDescs_ROADSTOP()
{
	_filtered_roadstop_desc = SlFilterObject(_roadstop_desc);
}


static void RealSave_STNN(BaseStation *bst)
{
	_num_specs = (uint8)bst->speclist.size();
	_num_roadstop_specs = (uint8)bst->roadstop_speclist.size();
	_num_roadstop_custom_tiles = (uint32)bst->custom_roadstop_tile_data.size();

	bool waypoint = (bst->facilities & FACIL_WAYPOINT) != 0;
	SlObjectSaveFiltered(bst, waypoint ? SaveLoadTable(_filtered_waypoint_desc) : SaveLoadTable(_filtered_station_desc));

	MemoryDumper *dumper = MemoryDumper::GetCurrent();

	if (!waypoint) {
		Station *st = Station::From(bst);
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			const GoodsEntryData *ged = st->goods[i].data.get();
			if (ged != nullptr) {
				_cargo_reserved_count = ged->cargo.ReservedCount();
				_num_dests = (uint32)ged->cargo.Packets()->MapSize();
				_num_flows = (uint32)ged->flows.size();
			} else {
				_cargo_reserved_count = 0;
				_num_dests = 0;
				_num_flows = 0;
			}
			SlObjectSaveFiltered(&st->goods[i], _filtered_goods_desc);
			if (ged == nullptr) continue;
			for (FlowStatMap::const_iterator outer_it(ged->flows.begin()); outer_it != ged->flows.end(); ++outer_it) {
				uint32 sum_shares = 0;
				FlowSaveLoad flow;
				flow.source = outer_it->GetOrigin();
				dumper->CheckBytes(2 + 4);
				dumper->RawWriteUint16(flow.source);
				dumper->RawWriteUint32((uint32)outer_it->size());
				FlowStat::const_iterator inner_it(outer_it->begin());
				const FlowStat::const_iterator end(outer_it->end());
				for (; inner_it != end; ++inner_it) {
					flow.via = inner_it->second;
					flow.share = inner_it->first - sum_shares;
					flow.restricted = inner_it->first > outer_it->GetUnrestricted();
					sum_shares = inner_it->first;
					assert(flow.share > 0);

					// SlObject(&flow, _flow_desc); /* this is highly performance-sensitive, manually unroll */
					dumper->CheckBytes(2 + 4 + 1);
					dumper->RawWriteUint16(flow.via);
					dumper->RawWriteUint32(flow.share);
					dumper->RawWriteByte(flow.restricted != 0);
				}
				SlWriteUint16(outer_it->GetRawFlags());
			}
			for (StationCargoPacketMap::ConstMapIterator it(ged->cargo.Packets()->begin()); it != ged->cargo.Packets()->end(); ++it) {
				SlObjectSaveFiltered(const_cast<StationCargoPacketMap::value_type *>(&(*it)), _cargo_list_desc); // _cargo_list_desc has no conditionals
			}
		}

		assert(st->station_cargo_history.size() == CountBits(st->station_cargo_history_cargoes));
		dumper->CheckBytes(st->station_cargo_history.size() * MAX_STATION_CARGO_HISTORY_DAYS * 2);
		for (const auto &history : st->station_cargo_history) {
			uint i = st->station_cargo_history_offset;
			do {
				dumper->RawWriteUint16(history[i]);
				i++;
				if (i == MAX_STATION_CARGO_HISTORY_DAYS) i = 0;
			} while (i != st->station_cargo_history_offset);
		}
	}

	for (uint i = 0; i < bst->speclist.size(); i++) {
		SlObjectSaveFiltered(&bst->speclist[i], _filtered_station_speclist_desc);
	}

	for (uint i = 0; i < bst->roadstop_speclist.size(); i++) {
		SlObjectSaveFiltered(&bst->roadstop_speclist[i], _filtered_roadstop_speclist_desc);
	}

	for (uint i = 0; i < bst->custom_roadstop_tile_data.size(); i++) {
		SlObjectSaveFiltered(&bst->custom_roadstop_tile_data[i], _custom_roadstop_tile_data_desc); // _custom_roadstop_tile_data_desc has no conditionals
	}
}

static void Save_STNN()
{
	SetupDescs_STNN();

	/* Write the stations */
	for (BaseStation *st : BaseStation::Iterate()) {
		SlSetArrayIndex(st->index);
		SlAutolength((AutolengthProc*)RealSave_STNN, st);
	}
}

static void Load_STNN()
{
	SetupDescs_STNN();

	_num_flows = 0;
	_num_specs = 0;
	_num_roadstop_specs = 0;
	_num_roadstop_custom_tiles = 0;
	_cargo_reserved_count = 0;

	std::unique_ptr<GoodsEntryData> spare_ged;

	const uint num_cargo = IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	ReadBuffer *buffer = ReadBuffer::GetCurrent();

	int index;
	while ((index = SlIterateArray()) != -1) {
		bool waypoint = (SlReadByte() & FACIL_WAYPOINT) != 0;

		BaseStation *bst = waypoint ? (BaseStation *)new (index) Waypoint() : new (index) Station();
		SlObjectLoadFiltered(bst, waypoint ? SaveLoadTable(_filtered_waypoint_desc) : SaveLoadTable(_filtered_station_desc));

		if (!waypoint) {
			Station *st = Station::From(bst);

			/* Before savegame version 161, persistent storages were not stored in a pool. */
			if (IsSavegameVersionBefore(SLV_161) && !IsSavegameVersionBefore(SLV_145) && st->facilities & FACIL_AIRPORT) {
				/* Store the old persistent storage. The GRFID will be added later. */
				assert(PersistentStorage::CanAllocateItem());
				st->airport.psa = new PersistentStorage(0, 0, 0);
				memcpy(st->airport.psa->storage, _old_st_persistent_storage.storage, sizeof(_old_st_persistent_storage.storage));
			}

			for (CargoID i = 0; i < num_cargo; i++) {
				GoodsEntry &ge = st->goods[i];
				if (ge.data == nullptr) {
					if (spare_ged != nullptr) {
						ge.data = std::move(spare_ged);
					} else {
						ge.data.reset(new GoodsEntryData());
					}
				}
				SlObjectLoadFiltered(&ge, _filtered_goods_desc);
				ge.data->cargo.LoadSetReservedCount(_cargo_reserved_count);
				StationID prev_source = INVALID_STATION;
				if (SlXvIsFeaturePresent(XSLFI_FLOW_STAT_FLAGS)) {
					ge.data->flows.reserve(_num_flows);
					for (uint32 j = 0; j < _num_flows; ++j) {
						FlowSaveLoad flow;
						buffer->CheckBytes(2 + 4);
						flow.source = buffer->RawReadUint16();
						uint32 flow_count = buffer->RawReadUint32();

						buffer->CheckBytes(2 + 4 + 1);
						flow.via = buffer->RawReadUint16();
						flow.share = buffer->RawReadUint32();
						flow.restricted = (buffer->RawReadByte() != 0);
						FlowStat *fs = &(*(ge.data->flows.insert(ge.data->flows.end(), FlowStat(flow.source, flow.via, flow.share, flow.restricted))));
						for (uint32 k = 1; k < flow_count; ++k) {
							buffer->CheckBytes(2 + 4 + 1);
							flow.via = buffer->RawReadUint16();
							flow.share = buffer->RawReadUint32();
							flow.restricted = (buffer->RawReadByte() != 0);
							fs->AppendShare(flow.via, flow.share, flow.restricted);
						}
						fs->SetRawFlags(SlReadUint16());
					}
				} else if (SlXvIsFeatureMissing(XSLFI_CHILLPP)) {
					FlowSaveLoad flow;
					FlowStat *fs = nullptr;
					for (uint32 j = 0; j < _num_flows; ++j) {
						// SlObject(&flow, _flow_desc); /* this is highly performance-sensitive, manually unroll */
						buffer->CheckBytes(2 + 2 + 4);
						flow.source = buffer->RawReadUint16();
						flow.via = buffer->RawReadUint16();
						flow.share = buffer->RawReadUint32();
						if (!IsSavegameVersionBefore(SLV_187)) flow.restricted = (buffer->ReadByte() != 0);

						if (fs == nullptr || prev_source != flow.source) {
							fs = &(*(ge.data->flows.insert(ge.data->flows.end(), FlowStat(flow.source, flow.via, flow.share, flow.restricted))));
						} else {
							fs->AppendShare(flow.via, flow.share, flow.restricted);
						}
						prev_source = flow.source;
					}
				}
				if (IsSavegameVersionBefore(SLV_183) && SlXvIsFeatureMissing(XSLFI_CHILLPP)) {
					SwapPackets(&ge);
				} else {
					if (SlXvIsFeaturePresent(XSLFI_CHILLPP)) {
						SlSkipBytes(8);
						uint num_links = SlReadUint16();
						uint num_flows = SlReadUint32();
						SlSkipBytes(6);
						SlSkipBytes(18 * num_links);
						SlSkipBytes(16 * num_flows);
					}

					StationCargoPair pair;
					for (uint j = 0; j < _num_dests; ++j) {
						SlObjectLoadFiltered(&pair, _cargo_list_desc); // _cargo_list_desc has no conditionals
						const_cast<StationCargoPacketMap &>(*(ge.data->cargo.Packets()))[pair.first].swap(pair.second);
						assert(pair.second.empty());
					}
				}
				if (SlXvIsFeatureMissing(XSLFI_ST_LAST_VEH_TYPE)) ge.last_vehicle_type = _old_last_vehicle_type;
				if (ge.data->MayBeRemoved()) {
					spare_ged = std::move(ge.data);
				}
			}

			st->station_cargo_history.resize(CountBits(st->station_cargo_history_cargoes));
			buffer->CheckBytes(st->station_cargo_history.size() * MAX_STATION_CARGO_HISTORY_DAYS * 2);
			for (auto &history : st->station_cargo_history) {
				for (uint16 &amount : history) {
					amount = buffer->RawReadUint16();
				}
			}
			if (SlXvIsFeaturePresent(XSLFI_STATION_CARGO_HISTORY, 1, 1)) {
				for (auto &history : st->station_cargo_history) {
					for (uint16 &amount : history) {
						amount = RXCompressUint(amount);
					}
				}
			}
			st->station_cargo_history_offset = 0;
		}

		if (_num_specs != 0) {
			/* Allocate speclist memory when loading a game */
			bst->speclist.resize(_num_specs);
			for (uint i = 0; i < bst->speclist.size(); i++) {
				SlObjectLoadFiltered(&bst->speclist[i], _filtered_station_speclist_desc);
			}
		}

		if (_num_roadstop_specs != 0) {
			/* Allocate speclist memory when loading a game */
			bst->roadstop_speclist.resize(_num_roadstop_specs);
			for (uint i = 0; i < bst->roadstop_speclist.size(); i++) {
				SlObjectLoadFiltered(&bst->roadstop_speclist[i], _filtered_roadstop_speclist_desc);
			}
		}

		if (_num_roadstop_custom_tiles != 0) {
			/* Allocate custom road stop tile data memory when loading a game */
			bst->custom_roadstop_tile_data.resize(_num_roadstop_custom_tiles);
			for (uint i = 0; i < bst->custom_roadstop_tile_data.size(); i++) {
				SlObjectLoadFiltered(&bst->custom_roadstop_tile_data[i], _custom_roadstop_tile_data_desc); // _custom_roadstop_tile_data_desc has no conditionals
			}
		}

		if (SlXvIsFeaturePresent(XSLFI_GRF_ROADSTOPS, 1, 1)) {
			for (size_t i = 0; i < _custom_road_stop_tiles.size(); i++) {
				bst->custom_roadstop_tile_data.push_back({ _custom_road_stop_tiles[i], (uint8)GB(_custom_road_stop_data[i], 0, 8), (uint8)GB(_custom_road_stop_data[i], 8, 8) });
			}
			_custom_road_stop_tiles.clear();
			_custom_road_stop_data.clear();
		}
	}
}

static void Ptrs_STNN()
{
	/* Don't run when savegame version lower than 123. */
	if (IsSavegameVersionBefore(SLV_123)) return;

	SetupDescs_STNN();

	if (!IsSavegameVersionBefore(SLV_183)) {
		assert(_filtered_goods_desc.size() == 0);
	}

	uint num_cargo = IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	for (Station *st : Station::Iterate()) {
		for (CargoID i = 0; i < num_cargo; i++) {
			GoodsEntry *ge = &st->goods[i];
			if (IsSavegameVersionBefore(SLV_183) && SlXvIsFeatureMissing(XSLFI_CHILLPP)) {
				SwapPackets(ge);
				SlObjectPtrOrNullFiltered(ge, _filtered_goods_desc);
				SwapPackets(ge);
			} else {
				//SlObject(ge, GetGoodsDesc());
				if (ge->data != nullptr) {
					for (StationCargoPacketMap::ConstMapIterator it = ge->data->cargo.Packets()->begin(); it != ge->data->cargo.Packets()->end(); ++it) {
						SlObjectPtrOrNullFiltered(const_cast<StationCargoPair *>(&(*it)), _cargo_list_desc); // _cargo_list_desc has no conditionals
					}
				}
			}
		}
		SlObjectPtrOrNullFiltered(st, _filtered_station_desc);
	}

	for (Waypoint *wp : Waypoint::Iterate()) {
		SlObjectPtrOrNullFiltered(wp, _filtered_waypoint_desc);
	}
}

static void Save_ROADSTOP()
{
	SetupDescs_ROADSTOP();
	for (RoadStop *rs : RoadStop::Iterate()) {
		SlSetArrayIndex(rs->index);
		SlObjectSaveFiltered(rs, _filtered_roadstop_desc);
	}
}

static void Load_ROADSTOP()
{
	SetupDescs_ROADSTOP();
	int index;
	while ((index = SlIterateArray()) != -1) {
		RoadStop *rs = new (index) RoadStop(INVALID_TILE);

		SlObjectLoadFiltered(rs, _filtered_roadstop_desc);
	}
}

static void Ptrs_ROADSTOP()
{
	SetupDescs_ROADSTOP();
	for (RoadStop *rs : RoadStop::Iterate()) {
		SlObjectPtrOrNullFiltered(rs, _filtered_roadstop_desc);
	}
}

static void Load_DOCK()
{
	extern void SlSkipArray();
	SlSkipArray();
}

static const ChunkHandler station_chunk_handlers[] = {
	{ 'STNS', nullptr,       Load_STNS,     Ptrs_STNS,     nullptr, CH_ARRAY },
	{ 'STNN', Save_STNN,     Load_STNN,     Ptrs_STNN,     nullptr, CH_ARRAY },
	{ 'ROAD', Save_ROADSTOP, Load_ROADSTOP, Ptrs_ROADSTOP, nullptr, CH_ARRAY },
	{ 'DOCK', nullptr,       Load_DOCK,     nullptr,       nullptr, CH_ARRAY },
};

extern const ChunkHandlerTable _station_chunk_handlers(station_chunk_handlers);
