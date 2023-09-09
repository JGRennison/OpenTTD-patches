/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargopacket_sl.cpp Code handling saving and loading of cargo packets */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/cargopacket_sl_compat.h"

#include "../vehicle_base.h"
#include "../station_base.h"

#include "../safeguards.h"

namespace upstream_sl {

/**
 * Wrapper function to get the CargoPacket's internal structure while
 * some of the variables itself are private.
 * @return the saveload description for CargoPackets.
 */
SaveLoadTable GetCargoPacketDesc()
{
	static const SaveLoad _cargopacket_desc[] = {
		SLE_VAR(CargoPacket, source,          SLE_UINT16),
		SLE_VAR(CargoPacket, source_xy,       SLE_UINT32),
		SLE_VAR(CargoPacket, count,           SLE_UINT16),
		SLE_CONDVAR(CargoPacket, days_in_transit, SLE_FILE_U8 | SLE_VAR_U16, SL_MIN_VERSION, SLV_MORE_CARGO_AGE),
		SLE_CONDVAR(CargoPacket, days_in_transit, SLE_UINT16, SLV_MORE_CARGO_AGE, SLV_PERIODS_IN_TRANSIT_RENAME),
		SLE_CONDVARNAME(CargoPacket, days_in_transit, "periods_in_transit", SLE_UINT16, SLV_PERIODS_IN_TRANSIT_RENAME, SL_MAX_VERSION),
		SLE_VAR(CargoPacket, feeder_share,    SLE_INT64),
		SLE_CONDVAR(CargoPacket, source_type,     SLE_UINT8,  SLV_125, SL_MAX_VERSION),
		SLE_CONDVAR(CargoPacket, source_id,       SLE_UINT16, SLV_125, SL_MAX_VERSION),
	};
	return _cargopacket_desc;
}

struct CAPAChunkHandler : ChunkHandler {
	CAPAChunkHandler() : ChunkHandler('CAPA', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(GetCargoPacketDesc());

		for (CargoPacket *cp : CargoPacket::Iterate()) {
			SlSetArrayIndex(cp->index);
			SlObject(cp, GetCargoPacketDesc());
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(GetCargoPacketDesc(), _cargopacket_sl_compat);

		int index;

		while ((index = SlIterateArray()) != -1) {
			CargoPacket *cp = new (index) CargoPacket();
			SlObject(cp, slt);
		}
	}
};

static const CAPAChunkHandler CAPA;
static const ChunkHandlerRef cargopacket_chunk_handlers[] = {
	CAPA,
};

extern const ChunkHandlerTable _cargopacket_chunk_handlers(cargopacket_chunk_handlers);

}
