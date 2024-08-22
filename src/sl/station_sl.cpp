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

static uint8_t _old_last_vehicle_type;
static uint8_t _num_specs;
static uint8_t _num_roadstop_specs;
static uint32_t _num_roadstop_custom_tiles;
static std::vector<TileIndex> _custom_road_stop_tiles;
static std::vector<uint16_t> _custom_road_stop_data;
static std::vector<uint16_t> _station_history_data_dummy;

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
		CalTime::Date build_date = st->build_date;
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

			st->speclist[i].spec = StationClass::GetByGrf(st->speclist[i].grfid, st->speclist[i].localidx);
		}
		for (uint i = 0; i < st->roadstop_speclist.size(); i++) {
			if (st->roadstop_speclist[i].grfid == 0) continue;

			st->roadstop_speclist[i].spec = RoadStopClass::GetByGrf(st->roadstop_speclist[i].grfid, st->roadstop_speclist[i].localidx);
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

static uint16_t _waiting_acceptance;
static uint32_t _num_flows;
static uint16_t _cargo_source;
static uint32_t _cargo_source_xy;
static uint8_t  _cargo_periods;
static Money  _cargo_feeder_share;
static uint   _cargo_reserved_count;

static const NamedSaveLoad _station_speclist_desc[] = {
	NSL("grfid",      SLE_CONDVAR(StationSpecList, grfid,    SLE_UINT32, SLV_27, SL_MAX_VERSION)),
	NSL("localidx", SLE_CONDVAR_X(StationSpecList, localidx, SLE_FILE_U8 | SLE_VAR_U16, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_NEWGRF_ENTITY_EXTRA, 0, 1))),
	NSL("localidx", SLE_CONDVAR_X(StationSpecList, localidx, SLE_UINT16,                SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_NEWGRF_ENTITY_EXTRA, 2))),
};

static const NamedSaveLoad _roadstop_speclist_desc[] = {
	NSL("grfid",      SLE_CONDVAR(RoadStopSpecList, grfid,    SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION)),
	NSL("localidx", SLE_CONDVAR_X(RoadStopSpecList, localidx, SLE_FILE_U8 | SLE_VAR_U16, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS, 0, 2))),
	NSL("localidx", SLE_CONDVAR_X(RoadStopSpecList, localidx, SLE_UINT16,                SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS, 3))),
};

CargoPacketList _packets;
uint32_t _num_dests;

struct FlowSaveLoad {
	FlowSaveLoad() : source(0), via(0), share(0), restricted(false), flags(0) {}
	StationID source;
	StationID via;
	uint32_t share;
	bool restricted;
	uint16_t flags;
};

#if 0
static const SaveLoad _flow_desc[] = {
	    SLE_VAR(FlowSaveLoad, source,     SLE_UINT16),
	    SLE_VAR(FlowSaveLoad, via,        SLE_UINT16),
	    SLE_VAR(FlowSaveLoad, share,      SLE_UINT32),
	SLE_CONDVAR(FlowSaveLoad, restricted, SLE_BOOL, SLV_187, SL_MAX_VERSION),
};
#endif

static const NamedSaveLoad _inner_flow_desc[] = {
	NSL("via",        SLTAG(SLTAG_CUSTOM_0, SLE_VAR(FlowSaveLoad, via,        SLE_UINT16))),
	NSL("share",      SLTAG(SLTAG_CUSTOM_1, SLE_VAR(FlowSaveLoad, share,      SLE_UINT32))),
	NSL("restricted", SLTAG(SLTAG_CUSTOM_2, SLE_CONDVAR(FlowSaveLoad, restricted, SLE_BOOL, SLV_187, SL_MAX_VERSION))),
};

struct StationGoodsInnerFlowStructHandler final : public HeaderOnlySaveLoadStructHandler {
	NamedSaveLoadTable GetDescription() const override
	{
		return _inner_flow_desc;
	}

	void LoadedTableDescription() override
	{
		SaveLoadTable slt = this->GetLoadDescription();
		if (slt.size() != 3 || slt[0].label_tag != SLTAG_CUSTOM_0 || slt[1].label_tag != SLTAG_CUSTOM_1 || slt[2].label_tag != SLTAG_CUSTOM_2) {
			SlErrorCorrupt("Station goods flow inner sub-chunk fields not as expected");
		}
	}
};

static const NamedSaveLoad _outer_flow_desc[] = {
	NSL("source",     SLTAG(SLTAG_CUSTOM_0, SLE_VAR(FlowSaveLoad, source,     SLE_UINT16))),
	NSL("flags",      SLTAG(SLTAG_CUSTOM_1, SLE_VAR(FlowSaveLoad, flags,      SLE_UINT16))),
	NSLTAG(SLTAG_CUSTOM_2, NSLT_STRUCTLIST<StationGoodsInnerFlowStructHandler>("flow")),
};

struct StationGoodsFlowStructHandler final : public TypedSaveLoadStructHandler<StationGoodsFlowStructHandler, GoodsEntry> {
	NamedSaveLoadTable GetDescription() const override
	{
		return _outer_flow_desc;
	}

	void Save(GoodsEntry *ge) const override
	{
		const GoodsEntryData *ged = ge->data.get();
		if (ged == nullptr) {
			SlSetStructListLength(0);
			return;
		}

		MemoryDumper *dumper = MemoryDumper::GetCurrent();

		SlSetStructListLength(ged->flows.size());

		for (const FlowStat &stat : ged->flows) {
			uint32_t sum_shares = 0;

			RawMemoryDumper dump = dumper->BorrowRawWriteBytes(2 + 2 + SlGetMaxGammaLength());
			dump.RawWriteUint16(stat.GetOrigin());
			dump.RawWriteUint16(stat.GetRawFlags());
			dump.RawWriteSimpleGamma(stat.size());
			dumper->ReturnRawWriteBytes(dump);

			for (const auto &it : stat) {
				StationID via = it.second;
				uint32_t share = it.first - sum_shares;
				bool restricted = it.first > stat.GetUnrestricted();
				sum_shares = it.first;
				dbg_assert(share > 0);

				/* This is performance-sensitive, manually unroll */
				dump = dumper->RawWriteBytes(2 + 4 + 1);
				dump.RawWriteUint16(via);
				dump.RawWriteUint32(share);
				dump.RawWriteByte(restricted ? 1 : 0);
			}
		}
	}

	void Load(GoodsEntry *ge) const override
	{
		ReadBuffer *reader = ReadBuffer::GetCurrent();

		uint num_flows = static_cast<uint>(SlGetStructListLength(UINT32_MAX));

		FlowStatMap &flows = ge->data->flows;
		flows.reserve(num_flows);
		for (uint32_t j = 0; j < num_flows; ++j) {
			RawReadBuffer buf = reader->ReadRawBytes(2 + 2);
			StationID source = buf.RawReadUint16();
			uint16_t flags = buf.RawReadUint16();
			uint32_t flow_count = SlReadSimpleGamma();

			buf = reader->ReadRawBytes(2 + 4 + 1);
			StationID via = buf.RawReadUint16();
			uint32_t share = buf.RawReadUint32();
			bool restricted = (buf.RawReadByte() != 0);
			FlowStat &fs = *(flows.insert(flows.end(), FlowStat(source, via, share, restricted)));
			fs.SetRawFlags(flags);
			for (uint32_t k = 1; k < flow_count; ++k) {
				buf = reader->ReadRawBytes(2 + 4 + 1);
				via = buf.RawReadUint16();
				share = buf.RawReadUint32();
				restricted = (buf.RawReadByte() != 0);
				fs.AppendShare(via, share, restricted);
			}
		}
	}

	void LoadedTableDescription() override
	{
		if (!SlXvIsFeaturePresent(XSLFI_FLOW_STAT_FLAGS)) {
			SlErrorCorrupt("XSLFI_FLOW_STAT_FLAGS unexpectedly not present");
		}
		SaveLoadTable slt = this->GetLoadDescription();
		if (slt.size() != 3 || slt[0].label_tag != SLTAG_CUSTOM_0 || slt[1].label_tag != SLTAG_CUSTOM_1 || slt[2].label_tag != SLTAG_CUSTOM_2) {
			SlErrorCorrupt("Station goods flow outer sub-chunk fields not as expected");
		}
	}
};

typedef std::pair<const StationID, CargoPacketList> StationCargoPair;

static const NamedSaveLoad _cargo_list_desc[] = {
	NSL("first",      SLE_VAR(StationCargoPair, first,  SLE_UINT16)),
	NSL("second", SLE_PTRRING(StationCargoPair, second, REF_CARGO_PACKET)),
};

struct StationGoodsCargoStructHandler final : public TypedSaveLoadStructHandler<StationGoodsCargoStructHandler, GoodsEntry> {
	NamedSaveLoadTable GetDescription() const override
	{
		return _cargo_list_desc;
	}

	void Save(GoodsEntry *ge) const override
	{
		const GoodsEntryData *ged = ge->data.get();
		if (ged == nullptr) {
			SlSetStructListLength(0);
			return;
		}

		SlSetStructListLength(ged->cargo.Packets()->MapSize());
		for (StationCargoPacketMap::ConstMapIterator it(ged->cargo.Packets()->begin()); it != ged->cargo.Packets()->end(); ++it) {
			SlObjectSaveFiltered(const_cast<StationCargoPacketMap::value_type *>(&(*it)), this->GetLoadDescription());
		}
	}

	void Load(GoodsEntry *ge) const override
	{
		uint num_dests = static_cast<uint>(SlGetStructListLength(UINT32_MAX));

		StationCargoPair pair;
		for (uint j = 0; j < num_dests; ++j) {
			SlObjectLoadFiltered(&pair, this->GetLoadDescription());
			const_cast<StationCargoPacketMap &>(*(ge->data->cargo.Packets()))[pair.first].swap(pair.second);
			assert(pair.second.empty());
		}
	}
};

/**
 * Wrapper function to get the GoodsEntry's internal structure while
 * some of the variables itself are private.
 * @return the saveload description for GoodsEntry.
 */
NamedSaveLoadTable GetGoodsDesc()
{
	static const NamedSaveLoad goods_desc[] = {
		NSL("",                          SLEG_CONDVAR(            _waiting_acceptance,   SLE_UINT16,                 SL_MIN_VERSION, SLV_68)),
		NSL("status",                     SLE_CONDVAR(GoodsEntry, status,                SLE_UINT8,                  SLV_68,         SL_MAX_VERSION)),
		NSL("",                          SLE_CONDNULL(2,                                                             SLV_51,         SLV_68)),
		NSL("time_since_pickup",              SLE_VAR(GoodsEntry, time_since_pickup,     SLE_UINT8)),
		NSL("",                        SLE_CONDNULL_X(6,                                                             SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 4))),
		NSL("rating",                         SLE_VAR(GoodsEntry, rating,                SLE_UINT8)),
		NSL("",                          SLEG_CONDVAR(            _cargo_source,         SLE_FILE_U8 | SLE_VAR_U16,  SL_MIN_VERSION, SLV_7)),
		NSL("",                          SLEG_CONDVAR(            _cargo_source,         SLE_UINT16,                 SLV_7,          SLV_68)),
		NSL("",                          SLEG_CONDVAR(            _cargo_source_xy,      SLE_UINT32,                 SLV_44,         SLV_68)),
		NSL("",                          SLEG_CONDVAR(            _cargo_periods,        SLE_UINT8,                  SL_MIN_VERSION, SLV_68)),
		NSL("last_speed",                     SLE_VAR(GoodsEntry, last_speed,            SLE_UINT8)),
		NSL("last_age",                       SLE_VAR(GoodsEntry, last_age,              SLE_UINT8)),
		NSL("",                          SLEG_CONDVAR(            _cargo_feeder_share,   SLE_FILE_U32 | SLE_VAR_I64, SLV_14,         SLV_65)),
		NSL("",                          SLEG_CONDVAR(            _cargo_feeder_share,   SLE_INT64,                  SLV_65,         SLV_68)),
		NSL("amount_fract",               SLE_CONDVAR(GoodsEntry, amount_fract,          SLE_UINT8,                  SLV_150,        SL_MAX_VERSION)),
		NSL("",                    SLEG_CONDPTRRING_X(            _packets,              REF_CARGO_PACKET,           SLV_68,         SLV_183,        SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, 0, 0))),
		NSL("",                        SLEG_CONDVAR_X(            _num_dests,            SLE_UINT32,                 SLV_183,        SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_OR, XSLFI_CHILLPP))),
		NSL("cargo.reserved_count",      SLEG_CONDVAR(            _cargo_reserved_count, SLE_UINT,                   SLV_181,        SL_MAX_VERSION)),
		NSL("link_graph",                 SLE_CONDVAR(GoodsEntry, link_graph,            SLE_UINT16,                 SLV_183,        SL_MAX_VERSION)),
		NSL("node",                       SLE_CONDVAR(GoodsEntry, node,                  SLE_UINT16,                 SLV_183,        SL_MAX_VERSION)),
		NSL("",                          SLEG_CONDVAR(            _num_flows,            SLE_UINT32,                 SLV_183,        SL_MAX_VERSION)),
		NSL("max_waiting_cargo",          SLE_CONDVAR(GoodsEntry, max_waiting_cargo,     SLE_UINT32,                 SLV_183,        SL_MAX_VERSION)),
		NSL("",                        SLE_CONDNULL_X(4,                                                             SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP))),
		NSL("last_vehicle_type",        SLE_CONDVAR_X(GoodsEntry, last_vehicle_type,     SLE_UINT8,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ST_LAST_VEH_TYPE, 1))),

		NSLT_STRUCTLIST<StationGoodsFlowStructHandler>("flow"),
		NSLT_STRUCTLIST<StationGoodsCargoStructHandler>("cargo"),
	};

	return goods_desc;
}

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

	std::vector<SaveLoad> goods_desc = SlFilterNamedSaveLoadTable(GetGoodsDesc());
	std::vector<SaveLoad> speclist_desc = SlFilterNamedSaveLoadTable(_station_speclist_desc);

	uint num_cargo = IsSavegameVersionBefore(SLV_55) ? 12 : IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	int index;
	while ((index = SlIterateArray()) != -1) {
		Station *st = new (index) Station();

		SlObject(st, _old_station_desc);

		_waiting_acceptance = 0;

		for (CargoID i = 0; i < num_cargo; i++) {
			GoodsEntry *ge = &st->goods[i];
			SlObjectLoadFiltered(ge, goods_desc);
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
				SlObjectLoadFiltered(&st->speclist[i], speclist_desc);
			}
		}
	}
}

static void Ptrs_STNS()
{
	/* Don't run when savegame version is higher than or equal to 123. */
	if (!IsSavegameVersionBefore(SLV_123)) return;

	std::vector<SaveLoad> goods_desc = SlFilterNamedSaveLoadTable(GetGoodsDesc());

	uint num_cargo = IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	for (Station *st : Station::Iterate()) {
		if (!IsSavegameVersionBefore(SLV_68)) {
			for (CargoID i = 0; i < num_cargo; i++) {
				GoodsEntry *ge = &st->goods[i];
				SwapPackets(ge);
				SlObject(ge, goods_desc);
				SwapPackets(ge);
			}
		}
		SlObject(st, _old_station_desc);
	}
}


static const NamedSaveLoad _base_station_desc[] = {
	NSL("xy",                                     SLE_VAR(BaseStation, xy,                        SLE_UINT32)),
	NSL("town",                                   SLE_REF(BaseStation, town,                      REF_TOWN)),
	NSL("string_id",                              SLE_VAR(BaseStation, string_id,                 SLE_STRINGID)),
	NSL("name",                                   SLE_STR(BaseStation, name,                      SLE_STR | SLF_ALLOW_CONTROL, 0)),

	NSL("delete_ctr",                       SLE_CONDVAR_X(Station,     delete_ctr,                SLE_UINT8,                   SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 0, 3))),
	NSL("delete_ctr",                       SLE_CONDVAR_X(Station,     delete_ctr,                SLE_FILE_U16  | SLE_VAR_U8,  SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 4))),
	NSL("owner",                                  SLE_VAR(BaseStation, owner,                     SLE_UINT8)),
	NSL("facilities",                             SLE_VAR(BaseStation, facilities,                SLE_UINT8)),
	NSL("build_date",                             SLE_VAR(BaseStation, build_date,                SLE_INT32)),

	/* Used by newstations for graphic variations */
	NSL("random_bits",                            SLE_VAR(BaseStation, random_bits,               SLE_UINT16)),
	NSL("waiting_triggers",                       SLE_VAR(BaseStation, waiting_triggers,          SLE_UINT8)),
	NSL("",                                      SLEG_VAR(_num_specs,                             SLE_UINT8)),
	NSL("",                                SLEG_CONDVAR_X(_num_roadstop_specs,                    SLE_UINT8,                   SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS))),
	NSL("",                             SLEG_CONDVARVEC_X(_custom_road_stop_tiles,                SLE_UINT32,                  SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS, 1, 1))),
	NSL("",                             SLEG_CONDVARVEC_X(_custom_road_stop_data,                 SLE_UINT16,                  SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS, 1, 1))),
	NSL("",                                SLEG_CONDVAR_X(_num_roadstop_custom_tiles,             SLE_UINT32,                  SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_GRF_ROADSTOPS, 2))),
};

void IncludeBaseStationDescription(std::vector<SaveLoad> &slt)
{
	SlFilterNamedSaveLoadTable(_base_station_desc, slt);
}

struct BaseStationStructHandler final : public TypedSaveLoadStructHandler<BaseStationStructHandler, BaseStation> {
	NamedSaveLoadTable GetDescription() const override
	{
		return _base_station_desc;
	}

	void Save(BaseStation *bst) const override
	{
		SlObjectSaveFiltered(bst, this->GetLoadDescription());
	}

	void Load(BaseStation *bst) const override
	{
		SlObjectLoadFiltered(bst, this->GetLoadDescription());
	}
};

static const NamedSaveLoad _station_cargo_history_desc[] = {
	NSL("cargoes", SLTAG(SLTAG_CUSTOM_0,     SLE_CONDVAR_X(Station, station_cargo_history_cargoes, SLE_UINT64,                  SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_STATION_CARGO_HISTORY)))),
	NSL("history", SLTAG(SLTAG_CUSTOM_1, SLEG_CONDVARVEC_X(_station_history_data_dummy,            SLE_UINT16,                  SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_STATION_CARGO_HISTORY)))),
};

static void LoadStationCargoHistoryData(Station *st)
{
	if (!st->station_cargo_history.empty()) {
		uint16_t *data = st->station_cargo_history[0].data();
		ReadBuffer::GetCurrent()->ReadUint16sToHandler(st->station_cargo_history.size() * MAX_STATION_CARGO_HISTORY_DAYS, [&](uint16_t val) {
			*data = val;
			data++;
		});
		if (SlXvIsFeaturePresent(XSLFI_STATION_CARGO_HISTORY, 1, 1)) {
			for (auto &history : st->station_cargo_history) {
				for (uint16_t &amount : history) {
					amount = RXCompressUint(amount);
				}
			}
		}
	}
	st->station_cargo_history_offset = 0;
}

struct StationGoodsStructHandler final : public TypedSaveLoadStructHandler<StationGoodsStructHandler, Station> {
	mutable std::unique_ptr<GoodsEntryData> spare_ged;

	NamedSaveLoadTable GetDescription() const override
	{
		return GetGoodsDesc();
	}

	void Save(Station *st) const override
	{
		SlSetStructListLength(NUM_CARGO);

		for (GoodsEntry &ge : st->goods) {
			if (ge.data != nullptr) {
				_cargo_reserved_count = ge.data->cargo.ReservedCount();
			} else {
				_cargo_reserved_count = 0;
			}
			SlObjectSaveFiltered(&ge, this->GetLoadDescription());
		}
	}

	void Load(Station *st) const override
	{
		uint num_cargo = static_cast<uint>(SlGetStructListLength(NUM_CARGO));

		for (CargoID i = 0; i < num_cargo; i++) {
			GoodsEntry &ge = st->goods[i];
			if (ge.data == nullptr) {
				if (this->spare_ged != nullptr) {
					ge.data = std::move(this->spare_ged);
				} else {
					ge.data.reset(new GoodsEntryData());
				}
			}
			SlObjectLoadFiltered(&ge, this->GetLoadDescription());
			ge.data->cargo.LoadSetReservedCount(_cargo_reserved_count);
			if (SlXvIsFeatureMissing(XSLFI_ST_LAST_VEH_TYPE)) ge.last_vehicle_type = _old_last_vehicle_type;
			if (ge.data->MayBeRemoved()) {
				this->spare_ged = std::move(ge.data);
			}
		}
	}
};

struct StationCargoHistoryStructHandler final : public TypedSaveLoadStructHandler<StationCargoHistoryStructHandler, Station> {
	NamedSaveLoadTable GetDescription() const override
	{
		return _station_cargo_history_desc;
	}

	void Save(Station *st) const override
	{
		MemoryDumper *dumper = MemoryDumper::GetCurrent();
		RawMemoryDumper dump = dumper->BorrowRawWriteBytes(8 + SlGetMaxGammaLength() + (st->station_cargo_history.size() * MAX_STATION_CARGO_HISTORY_DAYS * 2));

		dump.RawWriteUint64(st->station_cargo_history_cargoes);
		dump.RawWriteSimpleGamma(st->station_cargo_history.size() * MAX_STATION_CARGO_HISTORY_DAYS);

		for (const auto &history : st->station_cargo_history) {
			uint i = st->station_cargo_history_offset;
			do {
				dump.RawWriteUint16(history[i]);
				i++;
				if (i == MAX_STATION_CARGO_HISTORY_DAYS) i = 0;
			} while (i != st->station_cargo_history_offset);
		}
		dumper->ReturnRawWriteBytes(dump);
	}

	void Load(Station *st) const override
	{
		st->station_cargo_history_cargoes = SlReadUint64();
		st->station_cargo_history.resize(CountBits(st->station_cargo_history_cargoes));
		if (SlReadSimpleGamma() != st->station_cargo_history.size() * MAX_STATION_CARGO_HISTORY_DAYS) {
			SlErrorCorrupt("Station cargo history data of wrong size");
		}
		LoadStationCargoHistoryData(st);
	}

	void LoadedTableDescription() override
	{
		SaveLoadTable slt = this->GetLoadDescription();
		if (slt.size() != 2 || slt[0].label_tag != SLTAG_CUSTOM_0 || slt[1].label_tag != SLTAG_CUSTOM_1) {
			SlErrorCorrupt("Station cargo history sub-chunk fields not as expected");
		}
	}
};

static OldPersistentStorage _old_st_persistent_storage;

static const NamedSaveLoad _station_desc[] = {
	NSL("", SLE_WRITEBYTE(Station, facilities)),
	NSL("", SLE_INCLUDE(IncludeBaseStationDescription)),
	NSLT_STRUCT<BaseStationStructHandler>("base"),

	NSL("train_station.tile",                     SLE_VAR(Station, train_station.tile,            SLE_UINT32)),
	NSL("train_station.w",                        SLE_VAR(Station, train_station.w,               SLE_FILE_U8 | SLE_VAR_U16)),
	NSL("train_station.h",                        SLE_VAR(Station, train_station.h,               SLE_FILE_U8 | SLE_VAR_U16)),

	NSL("bus_stops",                              SLE_REF(Station, bus_stops,                     REF_ROADSTOPS)),
	NSL("truck_stops",                            SLE_REF(Station, truck_stops,                   REF_ROADSTOPS)),
	NSL("ship_station.tile",                SLE_CONDVAR_X(Station, ship_station.tile,             SLE_UINT32,                  SL_MIN_VERSION,        SLV_MULTITILE_DOCKS, SlXvFeatureTest(XSLFTO_AND, XSLFI_MULTIPLE_DOCKS, 0, 0))),
	NSL("",                                SLE_CONDNULL_X(4,                                                                   SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_MULTIPLE_DOCKS, 1, 1))),
	NSL("ship_station.tile",                  SLE_CONDVAR(Station, ship_station.tile,             SLE_UINT32,                  SLV_MULTITILE_DOCKS,   SL_MAX_VERSION)),
	NSL("ship_station.w",                     SLE_CONDVAR(Station, ship_station.w,                SLE_FILE_U8 | SLE_VAR_U16,   SLV_MULTITILE_DOCKS,   SL_MAX_VERSION)),
	NSL("ship_station.h",                     SLE_CONDVAR(Station, ship_station.h,                SLE_FILE_U8 | SLE_VAR_U16,   SLV_MULTITILE_DOCKS,   SL_MAX_VERSION)),
	NSL("docking_station.tile",               SLE_CONDVAR(Station, docking_station.tile,          SLE_UINT32,                  SLV_MULTITILE_DOCKS,   SL_MAX_VERSION)),
	NSL("docking_station.w",                  SLE_CONDVAR(Station, docking_station.w,             SLE_FILE_U8 | SLE_VAR_U16,   SLV_MULTITILE_DOCKS,   SL_MAX_VERSION)),
	NSL("docking_station.h",                  SLE_CONDVAR(Station, docking_station.h,             SLE_FILE_U8 | SLE_VAR_U16,   SLV_MULTITILE_DOCKS,   SL_MAX_VERSION)),
	NSL("docking_tiles",                 SLE_CONDVARVEC_X(Station, docking_tiles,                 SLE_UINT32,                  SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_MULTIPLE_DOCKS, 2))),
	NSL("airport.tile",                           SLE_VAR(Station, airport.tile,                  SLE_UINT32)),
	NSL("airport.w",                          SLE_CONDVAR(Station, airport.w,                     SLE_FILE_U8 | SLE_VAR_U16,   SLV_140,               SL_MAX_VERSION)),
	NSL("airport.h",                          SLE_CONDVAR(Station, airport.h,                     SLE_FILE_U8 | SLE_VAR_U16,   SLV_140,               SL_MAX_VERSION)),
	NSL("airport.type",                           SLE_VAR(Station, airport.type,                  SLE_UINT8)),
	NSL("airport.layout",                     SLE_CONDVAR(Station, airport.layout,                SLE_UINT8,                   SLV_145,               SL_MAX_VERSION)),
	NSL("",                                SLE_CONDNULL_X(1,                                                                   SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 1, 6))),
	NSL("airport.flags",                          SLE_VAR(Station, airport.flags,                 SLE_UINT64)),
	NSL("",                                SLE_CONDNULL_X(8,                                                                   SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 1, 6))),
	NSL("airport.rotation",                   SLE_CONDVAR(Station, airport.rotation,              SLE_UINT8,                   SLV_145,               SL_MAX_VERSION)),
	NSL("",                                  SLEG_CONDARR(_old_st_persistent_storage.storage,     SLE_UINT32, 16,              SLV_145,               SLV_161)),
	NSL("irport.psa",                         SLE_CONDREF(Station, airport.psa,                   REF_STORAGE,                 SLV_161,               SL_MAX_VERSION)),

	NSL("indtype",                                SLE_VAR(Station, indtype,                       SLE_UINT8)),
	NSL("extra_name_index",                 SLE_CONDVAR_X(Station, extra_name_index,              SLE_UINT16,                  SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_EXTRA_STATION_NAMES))),

	NSL("time_since_load",                        SLE_VAR(Station, time_since_load,               SLE_UINT8)),
	NSL("time_since_unload",                      SLE_VAR(Station, time_since_unload,             SLE_UINT8)),
	NSL("",                                SLEG_CONDVAR_X(_old_last_vehicle_type,                 SLE_UINT8,                   SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_ST_LAST_VEH_TYPE, 0, 0))),
	NSL("",                                SLE_CONDNULL_X(1,                                                                   SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP))),
	NSL("had_vehicle_of_type",                    SLE_VAR(Station, had_vehicle_of_type,           SLE_UINT8)),
	NSL("loading_vehicles",                       SLE_VEC(Station, loading_vehicles,              REF_VEHICLE)),
	NSL("always_accepted",                    SLE_CONDVAR(Station, always_accepted,               SLE_FILE_U32 | SLE_VAR_U64,  SLV_127,               SLV_EXTEND_CARGOTYPES)),
	NSL("always_accepted",                    SLE_CONDVAR(Station, always_accepted,               SLE_UINT64,                  SLV_EXTEND_CARGOTYPES, SL_MAX_VERSION)),
	NSL("",                                SLE_CONDNULL_X(32 * 24,                                                             SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP, SL_JOKER_1_22))),

	NSL("",                                 SLE_CONDVAR_X(Station, station_cargo_history_cargoes, SLE_UINT64,                  SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_STATION_CARGO_HISTORY))),
	NSLT_STRUCTLIST<StationGoodsStructHandler>("goods"),
	NSLT_STRUCT<StationCargoHistoryStructHandler>("cargo_history"),
};

static const NamedSaveLoad _waypoint_desc[] = {
	NSL("", SLE_WRITEBYTE(Waypoint, facilities)),
	NSL("", SLE_INCLUDE(IncludeBaseStationDescription)),
	NSLT_STRUCT<BaseStationStructHandler>("base"),

	NSL("town_cn",                                SLE_VAR(Waypoint, town_cn,                      SLE_UINT16)),

	NSL("train_station.tile",                 SLE_CONDVAR(Waypoint, train_station.tile,           SLE_UINT32,                  SLV_124,               SL_MAX_VERSION)),
	NSL("train_station.w",                    SLE_CONDVAR(Waypoint, train_station.w,              SLE_FILE_U8 | SLE_VAR_U16,   SLV_124,               SL_MAX_VERSION)),
	NSL("train_station.h",                    SLE_CONDVAR(Waypoint, train_station.h,              SLE_FILE_U8 | SLE_VAR_U16,   SLV_124,               SL_MAX_VERSION)),
	NSL("waypoint_flags",                   SLE_CONDVAR_X(Waypoint, waypoint_flags,               SLE_UINT16,                  SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_WAYPOINT_FLAGS))),
	NSL("road_waypoint_area.tile",          SLE_CONDVAR_X(Waypoint, road_waypoint_area.tile,      SLE_UINT32,                  SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_ROAD_WAYPOINTS))),
	NSL("road_waypoint_area.w",             SLE_CONDVAR_X(Waypoint, road_waypoint_area.w,         SLE_FILE_U8 | SLE_VAR_U16,   SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_ROAD_WAYPOINTS))),
	NSL("road_waypoint_area.h",             SLE_CONDVAR_X(Waypoint, road_waypoint_area.h,         SLE_FILE_U8 | SLE_VAR_U16,   SL_MIN_VERSION,        SL_MAX_VERSION,      SlXvFeatureTest(XSLFTO_AND, XSLFI_ROAD_WAYPOINTS))),
};

static const NamedSaveLoad _custom_roadstop_tile_data_desc[] = {
	NSL("tile",            SLE_VAR(RoadStopTileData, tile,            SLE_UINT32)),
	NSL("random_bits",     SLE_VAR(RoadStopTileData, random_bits,     SLE_UINT8)),
	NSL("animation_frame", SLE_VAR(RoadStopTileData, animation_frame, SLE_UINT8)),
};

class StationSpecListStructHandler final : public TypedSaveLoadStructHandler<StationSpecListStructHandler, BaseStation> {
public:
	NamedSaveLoadTable GetDescription() const override
	{
		return _station_speclist_desc;
	}

	void Save(BaseStation *bst) const override
	{
		SlSetStructListLength(bst->speclist.size());
		for (StationSpecList &spec : bst->speclist) {
			SlObjectSaveFiltered(&spec, this->GetLoadDescription());
		}
	}

	void Load(BaseStation *bst) const override
	{
		bst->speclist.resize(SlGetStructListLength(UINT8_MAX));
		for (StationSpecList &spec : bst->speclist) {
			SlObjectLoadFiltered(&spec, this->GetLoadDescription());
		}
	}
};

class RoadStopSpecListStructHandler final : public TypedSaveLoadStructHandler<RoadStopSpecListStructHandler, BaseStation> {
public:
	NamedSaveLoadTable GetDescription() const override
	{
		return _roadstop_speclist_desc;
	}

	void Save(BaseStation *bst) const override
	{
		SlSetStructListLength(bst->roadstop_speclist.size());
		for (RoadStopSpecList &spec : bst->roadstop_speclist) {
			SlObjectSaveFiltered(&spec, this->GetLoadDescription());
		}
	}

	void Load(BaseStation *bst) const override
	{
		bst->roadstop_speclist.resize(SlGetStructListLength(UINT8_MAX));
		for (RoadStopSpecList &spec : bst->roadstop_speclist) {
			SlObjectLoadFiltered(&spec, this->GetLoadDescription());
		}
	}
};

class RoadStopTileDataStructHandler final : public TypedSaveLoadStructHandler<RoadStopTileDataStructHandler, BaseStation> {
public:
	NamedSaveLoadTable GetDescription() const override
	{
		return _custom_roadstop_tile_data_desc;
	}

	void Save(BaseStation *bst) const override
	{
		SlSetStructListLength(bst->custom_roadstop_tile_data.size());
		for (RoadStopTileData &data : bst->custom_roadstop_tile_data) {
			SlObjectSaveFiltered(&data, this->GetLoadDescription());
		}
	}

	void Load(BaseStation *bst) const override
	{
		bst->custom_roadstop_tile_data.resize(SlGetStructListLength(UINT32_MAX));
		for (RoadStopTileData &data : bst->custom_roadstop_tile_data) {
			SlObjectLoadFiltered(&data, this->GetLoadDescription());
		}
	}
};

struct NormalStationStructHandler final : public TypedSaveLoadStructHandler<NormalStationStructHandler, BaseStation> {
	NamedSaveLoadTable GetDescription() const override
	{
		return _station_desc;
	}

	void Save(BaseStation *bst) const override
	{
		if ((bst->facilities & FACIL_WAYPOINT) != 0) return;
		SlObjectSaveFiltered(static_cast<Station *>(bst), this->GetLoadDescription());
	}

	void Load(BaseStation *bst) const override
	{
		if ((bst->facilities & FACIL_WAYPOINT) != 0) SlErrorCorrupt("Waypoint with normal station struct");
		SlObjectLoadFiltered(static_cast<Station *>(bst), this->GetLoadDescription());
	}
};

struct WaypointStructHandler final : public TypedSaveLoadStructHandler<WaypointStructHandler, BaseStation> {
	NamedSaveLoadTable GetDescription() const override
	{
		return _waypoint_desc;
	}

	void Save(BaseStation *bst) const override
	{
		if ((bst->facilities & FACIL_WAYPOINT) == 0) return;
		SlObjectSaveFiltered(static_cast<Waypoint *>(bst), this->GetLoadDescription());
	}

	void Load(BaseStation *bst) const override
	{
		if ((bst->facilities & FACIL_WAYPOINT) == 0) SlErrorCorrupt("Normal station with waypoint struct");
		SlObjectLoadFiltered(static_cast<Waypoint *>(bst), this->GetLoadDescription());
	}
};


static const NamedSaveLoad _table_station_desc[] = {
	NSLT("facilities", SLE_WRITEBYTE(BaseStation, facilities)),
	NSLT_STRUCT<NormalStationStructHandler>("normal"),
	NSLT_STRUCT<WaypointStructHandler>("waypoint"),
	NSLT_STRUCTLIST<StationSpecListStructHandler>("speclist"),
	NSLT_STRUCTLIST<RoadStopSpecListStructHandler>("roadstopspeclist"),
	NSLT_STRUCTLIST<RoadStopTileDataStructHandler>("roadstoptiledata"),
};

static void Save_STNN()
{
	SaveLoadTableData slt = SlTableHeader(_table_station_desc);

	/* Write the stations */
	for (BaseStation *st : BaseStation::Iterate()) {
		SlSetArrayIndex(st->index);
		SlObjectSaveFiltered(st, slt);
	}
}

static void PostLoadStation_STNN(BaseStation *bst)
{
	if (SlXvIsFeaturePresent(XSLFI_GRF_ROADSTOPS, 1, 1)) {
		for (size_t i = 0; i < _custom_road_stop_tiles.size(); i++) {
			bst->custom_roadstop_tile_data.push_back({ _custom_road_stop_tiles[i], (uint8_t)GB(_custom_road_stop_data[i], 0, 8), (uint8_t)GB(_custom_road_stop_data[i], 8, 8) });
		}
		_custom_road_stop_tiles.clear();
		_custom_road_stop_data.clear();
	}
}

static void Load_STNN_table()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_table_station_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		bool waypoint = (SlReadByte() & FACIL_WAYPOINT) != 0;

		BaseStation *bst = waypoint ? (BaseStation *)new (index) Waypoint() : new (index) Station();
		SlObjectLoadFiltered(bst, slt);
		PostLoadStation_STNN(bst);
	}
}

static void Load_STNN()
{
	_num_flows = 0;
	_num_specs = 0;
	_num_roadstop_specs = 0;
	_num_roadstop_custom_tiles = 0;
	_cargo_reserved_count = 0;

	if (SlIsTableChunk()) {
		Load_STNN_table();
		return;
	}

	std::vector<SaveLoad> filtered_station_desc = SlFilterNamedSaveLoadTable(_station_desc);
	std::vector<SaveLoad> filtered_waypoint_desc = SlFilterNamedSaveLoadTable(_waypoint_desc);
	std::vector<SaveLoad> filtered_goods_desc = SlFilterNamedSaveLoadTable(GetGoodsDesc());
	std::vector<SaveLoad> cargo_list_desc = SlFilterNamedSaveLoadTable(_cargo_list_desc);
	std::vector<SaveLoad> filtered_station_speclist_desc = SlFilterNamedSaveLoadTable(_station_speclist_desc);
	std::vector<SaveLoad> filtered_roadstop_speclist_desc = SlFilterNamedSaveLoadTable(_roadstop_speclist_desc);
	std::vector<SaveLoad> custom_roadstop_tile_data_desc = SlFilterNamedSaveLoadTable(_custom_roadstop_tile_data_desc);

	std::unique_ptr<GoodsEntryData> spare_ged;

	const uint num_cargo = IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	ReadBuffer *reader = ReadBuffer::GetCurrent();

	const bool read_restricted = !IsSavegameVersionBefore(SLV_187);

	int index;
	while ((index = SlIterateArray()) != -1) {
		bool waypoint = (SlReadByte() & FACIL_WAYPOINT) != 0;

		BaseStation *bst = waypoint ? (BaseStation *)new (index) Waypoint() : new (index) Station();
		SlObjectLoadFiltered(bst, waypoint ? SaveLoadTable(filtered_waypoint_desc) : SaveLoadTable(filtered_station_desc));

		if (!waypoint) {
			Station *st = Station::From(bst);

			/* Before savegame version 161, persistent storages were not stored in a pool. */
			if (IsSavegameVersionBefore(SLV_161) && !IsSavegameVersionBefore(SLV_145) && st->facilities & FACIL_AIRPORT) {
				/* Store the old persistent storage. The GRFID will be added later. */
				assert(PersistentStorage::CanAllocateItem());
				st->airport.psa = new PersistentStorage(0, 0, 0);
				std::copy(std::begin(_old_st_persistent_storage.storage), std::end(_old_st_persistent_storage.storage), std::begin(st->airport.psa->storage));
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
				SlObjectLoadFiltered(&ge, filtered_goods_desc);
				ge.data->cargo.LoadSetReservedCount(_cargo_reserved_count);
				StationID prev_source = INVALID_STATION;
				if (SlXvIsFeaturePresent(XSLFI_FLOW_STAT_FLAGS)) {
					ge.data->flows.reserve(_num_flows);
					for (uint32_t j = 0; j < _num_flows; ++j) {
						FlowSaveLoad flow;
						RawReadBuffer buf = reader->ReadRawBytes(2 + 4);
						flow.source = buf.RawReadUint16();
						uint32_t flow_count = buf.RawReadUint32();

						buf = reader->ReadRawBytes(2 + 4 + 1);
						flow.via = buf.RawReadUint16();
						flow.share = buf.RawReadUint32();
						flow.restricted = (buf.RawReadByte() != 0);
						FlowStat *fs = &(*(ge.data->flows.insert(ge.data->flows.end(), FlowStat(flow.source, flow.via, flow.share, flow.restricted))));
						for (uint32_t k = 1; k < flow_count; ++k) {
							buf = reader->ReadRawBytes(2 + 4 + 1);
							flow.via = buf.RawReadUint16();
							flow.share = buf.RawReadUint32();
							flow.restricted = (buf.RawReadByte() != 0);
							fs->AppendShare(flow.via, flow.share, flow.restricted);
						}
						fs->SetRawFlags(SlReadUint16());
					}
				} else if (SlXvIsFeatureMissing(XSLFI_CHILLPP)) {
					FlowSaveLoad flow;
					FlowStat *fs = nullptr;
					for (uint32_t j = 0; j < _num_flows; ++j) {
						// SlObject(&flow, _flow_desc); /* this is highly performance-sensitive, manually unroll */
						RawReadBuffer buf = reader->ReadRawBytes(2 + 2 + 4 + (read_restricted ? 1 : 0));
						flow.source = buf.RawReadUint16();
						flow.via = buf.RawReadUint16();
						flow.share = buf.RawReadUint32();
						if (read_restricted) flow.restricted = (buf.RawReadByte() != 0);

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
						SlObjectLoadFiltered(&pair, cargo_list_desc);
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
			LoadStationCargoHistoryData(st);
		}

		if (_num_specs != 0) {
			/* Allocate speclist memory when loading a game */
			bst->speclist.resize(_num_specs);
			for (uint i = 0; i < bst->speclist.size(); i++) {
				SlObjectLoadFiltered(&bst->speclist[i], filtered_station_speclist_desc);
			}
		}

		if (_num_roadstop_specs != 0) {
			/* Allocate speclist memory when loading a game */
			bst->roadstop_speclist.resize(_num_roadstop_specs);
			for (uint i = 0; i < bst->roadstop_speclist.size(); i++) {
				SlObjectLoadFiltered(&bst->roadstop_speclist[i], filtered_roadstop_speclist_desc);
			}
		}

		if (_num_roadstop_custom_tiles != 0) {
			/* Allocate custom road stop tile data memory when loading a game */
			bst->custom_roadstop_tile_data.resize(_num_roadstop_custom_tiles);
			for (uint i = 0; i < bst->custom_roadstop_tile_data.size(); i++) {
				SlObjectLoadFiltered(&bst->custom_roadstop_tile_data[i], custom_roadstop_tile_data_desc);
			}
		}

		PostLoadStation_STNN(bst);
	}
}

static void Ptrs_STNN()
{
	/* Don't run when savegame version lower than 123. */
	if (IsSavegameVersionBefore(SLV_123)) return;

	std::vector<SaveLoad> filtered_station_desc = SlFilterNamedSaveLoadTable(_station_desc);
	std::vector<SaveLoad> filtered_waypoint_desc = SlFilterNamedSaveLoadTable(_waypoint_desc);
	std::vector<SaveLoad> filtered_goods_desc = SlFilterNamedSaveLoadTable(GetGoodsDesc());
	std::vector<SaveLoad> cargolist_desc = SlFilterNamedSaveLoadTable(_cargo_list_desc);

	if (!IsSavegameVersionBefore(SLV_183)) {
		assert(filtered_goods_desc.size() == 0);
	}

	uint num_cargo = IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;
	for (Station *st : Station::Iterate()) {
		for (CargoID i = 0; i < num_cargo; i++) {
			GoodsEntry *ge = &st->goods[i];
			if (IsSavegameVersionBefore(SLV_183) && SlXvIsFeatureMissing(XSLFI_CHILLPP)) {
				SwapPackets(ge);
				SlObjectPtrOrNullFiltered(ge, filtered_goods_desc);
				SwapPackets(ge);
			} else {
				//SlObject(ge, GetGoodsDesc());
				if (ge->data != nullptr) {
					for (StationCargoPacketMap::ConstMapIterator it = ge->data->cargo.Packets()->begin(); it != ge->data->cargo.Packets()->end(); ++it) {
						SlObjectPtrOrNullFiltered(const_cast<StationCargoPair *>(&(*it)), cargolist_desc);
					}
				}
			}
		}
		SlObjectPtrOrNullFiltered(st, filtered_station_desc);
	}

	for (Waypoint *wp : Waypoint::Iterate()) {
		SlObjectPtrOrNullFiltered(wp, filtered_waypoint_desc);
	}
}

static void Load_DOCK()
{
	extern void SlSkipArray();
	SlSkipArray();
}

static const ChunkHandler station_chunk_handlers[] = {
	{ 'STNS', nullptr,       Load_STNS,     Ptrs_STNS,     nullptr, CH_READONLY },
	{ 'STNN', Save_STNN,     Load_STNN,     Ptrs_STNN,     nullptr, CH_TABLE },
	MakeUpstreamChunkHandler<'ROAD', GeneralUpstreamChunkLoadInfo>(),
	{ 'DOCK', nullptr,       Load_DOCK,     nullptr,       nullptr, CH_READONLY },
};

extern const ChunkHandlerTable _station_chunk_handlers(station_chunk_handlers);
