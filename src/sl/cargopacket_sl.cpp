/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargopacket_sl.cpp Code handling saving and loading of cargo packets */

#include "../stdafx.h"
#include "../vehicle_base.h"
#include "../station_base.h"
#include "../scope_info.h"
#include "../3rdparty/cpp-btree/btree_map.h"

#include "saveload.h"

#include "../safeguards.h"

extern btree::btree_map<uint64, Money> _cargo_packet_deferred_payments;

/**
 * Savegame conversion for cargopackets.
 */
/* static */ void CargoPacket::AfterLoad()
{
	if (IsSavegameVersionBefore(SLV_44)) {
		/* If we remove a station while cargo from it is still en route, payment calculation will assume
		 * 0, 0 to be the first_station of the cargo, resulting in very high payments usually. v->source_xy
		 * stores the coordinates, preserving them even if the station is removed. However, if a game is loaded
		 * where this situation exists, the cargo-first_station information is lost. in this case, we set the first_station
		 * to the current tile of the vehicle to prevent excessive profits
		 */
		for (const Vehicle *v : Vehicle::Iterate()) {
			const CargoPacketList *packets = v->cargo.Packets();
			for (VehicleCargoList::ConstIterator it(packets->begin()); it != packets->end(); it++) {
				CargoPacket *cp = *it;
				cp->source_xy = Station::IsValidID(cp->first_station) ? Station::Get(cp->first_station)->xy : v->tile;
			}
		}

		/* Store position of the station where the goods come from, so there
		 * are no very high payments when stations get removed. However, if the
		 * station where the goods came from is already removed, the first_station
		 * information is lost. In that case we set it to the position of this
		 * station */
		for (Station *st : Station::Iterate()) {
			for (CargoID c = 0; c < NUM_CARGO; c++) {
				GoodsEntry *ge = &st->goods[c];

				if (ge->data == nullptr) continue;
				const StationCargoPacketMap *packets = ge->data->cargo.Packets();
				for (StationCargoList::ConstIterator it(packets->begin()); it != packets->end(); it++) {
					CargoPacket *cp = *it;
					cp->source_xy = Station::IsValidID(cp->first_station) ? Station::Get(cp->first_station)->xy : st->xy;
				}
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_120)) {
		/* CargoPacket's first_station should be either INVALID_STATION or a valid station */
		for (CargoPacket *cp : CargoPacket::Iterate()) {
			if (!Station::IsValidID(cp->first_station)) cp->first_station = INVALID_STATION;
		}
	}

	if (!IsSavegameVersionBefore(SLV_68)) {
		/* Only since version 68 we have cargo packets. Savegames from before used
		 * 'new CargoPacket' + cargolist.Append so their caches are already
		 * correct and do not need rebuilding. */
		for (Vehicle *v : Vehicle::Iterate()) v->cargo.InvalidateCache();

		for (Station *st : Station::Iterate()) {
			for (CargoID c = 0; c < NUM_CARGO; c++) {
				if (st->goods[c].data != nullptr) st->goods[c].data->cargo.InvalidateCache();
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_181)) {
		for (Vehicle *v : Vehicle::Iterate()) v->cargo.KeepAll();
	}
}

/**
 * Savegame conversion for cargopackets.
 */
/* static */ void CargoPacket::PostVehiclesAfterLoad()
{
	if (SlXvIsFeaturePresent(XSLFI_CHILLPP)) {
		extern std::map<VehicleID, CargoPacketList> _veh_cpp_packets;
		for (auto &iter : _veh_cpp_packets) {
			if (iter.second.empty()) continue;
			Vehicle *v = Vehicle::Get(iter.first);
			Station *st = Station::Get(v->First()->last_station_visited);
			assert_msg(st != nullptr, "%s", scope_dumper().VehicleInfo(v));
			for (CargoPacket *cp : iter.second) {
				st->goods[v->cargo_type].CreateData().cargo.AfterLoadIncreaseReservationCount(cp->count);
				v->cargo.Append(cp, VehicleCargoList::MTA_LOAD);
			}
		}
		_veh_cpp_packets.clear();
	}
}

/**
 * Wrapper function to get the CargoPacket's internal structure while
 * some of the variables itself are private.
 * @return the saveload description for CargoPackets.
 */
SaveLoadTable GetCargoPacketDesc()
{
	static const SaveLoad _cargopacket_desc[] = {
		     SLE_VAR(CargoPacket, first_station,   SLE_UINT16),
		     SLE_VAR(CargoPacket, source_xy,       SLE_UINT32),
		     SLE_VAR(CargoPacket, next_station,    SLE_FILE_U32 | SLE_VAR_U16),
		     SLE_VAR(CargoPacket, count,           SLE_UINT16),
		SLE_CONDVAR_X(CargoPacket, periods_in_transit, SLE_FILE_U8 | SLE_VAR_U16, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_MORE_CARGO_AGE, 0, 0)),
		SLE_CONDVAR_X(CargoPacket, periods_in_transit, SLE_UINT16, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_MORE_CARGO_AGE)),
		     SLE_VAR(CargoPacket, feeder_share,    SLE_INT64),
		 SLE_CONDVAR(CargoPacket, source_type,     SLE_UINT8,  SLV_125, SL_MAX_VERSION),
		 SLE_CONDVAR(CargoPacket, source_id,       SLE_UINT16, SLV_125, SL_MAX_VERSION),

		/* Used to be paid_for, but that got changed. */
		SLE_CONDNULL(1, SL_MIN_VERSION, SLV_121),
	};
	return _cargopacket_desc;
}

/**
 * Save the cargo packets.
 */
static void Save_CAPA()
{
	std::vector<SaveLoad> filtered_packet_desc = SlFilterObject(GetCargoPacketDesc());
	for (CargoPacket *cp : CargoPacket::Iterate()) {
		SlSetArrayIndex(cp->index);
		SlObjectSaveFiltered(cp, filtered_packet_desc);
	}
}

/**
 * Load the cargo packets.
 */
static void Load_CAPA()
{
	std::vector<SaveLoad> filtered_packet_desc = SlFilterObject(GetCargoPacketDesc());
	int index;
	while ((index = SlIterateArray()) != -1) {
		CargoPacket *cp = new (index) CargoPacket();
		SlObjectLoadFiltered(cp, filtered_packet_desc);
	}
}

/**
 * Save cargo packet deferred payments.
 */
void Save_CPDP()
{
	SlSetLength(16 * _cargo_packet_deferred_payments.size());

	for (auto &it : _cargo_packet_deferred_payments) {
		SlWriteUint64(it.first);
		SlWriteUint64(it.second);
	}
}

/**
 * Load cargo packet deferred payments.
 */
void Load_CPDP()
{
	uint count = static_cast<uint>(SlGetFieldLength() / 16);
	uint last_cargo_packet_id = std::numeric_limits<uint32_t>::max();

	for (uint i = 0; i < count; i++) {
		uint64 k = SlReadUint64();
		uint64 v = SlReadUint64();
		_cargo_packet_deferred_payments[k] = v;
		if (k >> 32 != last_cargo_packet_id) {
			last_cargo_packet_id = k >> 32;
			CargoPacket::Get(last_cargo_packet_id)->flags |= CargoPacket::CPF_HAS_DEFERRED_PAYMENT;
		}
	}
}



/** Chunk handlers related to cargo packets. */
static const ChunkHandler cargopacket_chunk_handlers[] = {
	{ 'CAPA', Save_CAPA, Load_CAPA, nullptr, nullptr, CH_ARRAY },
	{ 'CPDP', Save_CPDP, Load_CPDP, nullptr, nullptr, CH_RIFF  },
};

extern const ChunkHandlerTable _cargopacket_chunk_handlers(cargopacket_chunk_handlers);
