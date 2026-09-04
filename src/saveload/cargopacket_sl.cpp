/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file cargopacket_sl.cpp Code handling saving and loading of cargo packets. */

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
		SLE_VARNAME(CargoPacket, first_station, "source", SLE_UINT16),
		SLE_VAR(CargoPacket, source_xy,       SLE_UINT32),
		SLE_CONDVARNAME(CargoPacket, next_hop, "loaded_at_xy", SLE_FILE_U32 | SLE_VAR_U16, SaveLoadVersion::MinVersion, SaveLoadVersion::RemoveLoadedAtXY),
		SLE_CONDVARNAME(CargoPacket, next_hop, "loaded_at_xy", SLE_UINT16, SaveLoadVersion::RemoveLoadedAtXY, SaveLoadVersion::MaxVersion),
		SLE_VAR(CargoPacket, count,           SLE_UINT16),
		SLE_CONDVARNAME(CargoPacket, periods_in_transit, "days_in_transit", SLE_FILE_U8 | SLE_VAR_U16, SaveLoadVersion::MinVersion, SaveLoadVersion::MoreCargoAge),
		SLE_CONDVARNAME(CargoPacket, periods_in_transit, "days_in_transit", SLE_UINT16, SaveLoadVersion::MoreCargoAge, SaveLoadVersion::PeriodsInTransitRename),
		SLE_CONDVAR(CargoPacket, periods_in_transit, SLE_UINT16, SaveLoadVersion::PeriodsInTransitRename, SaveLoadVersion::MaxVersion),
		SLE_VAR(CargoPacket, feeder_share,    SLE_INT64),
		SLE_CONDVARNAME(CargoPacket, source.type, "source_type", SLE_UINT8, SaveLoadVersion::RemoveSubsidyStationBinding, SaveLoadVersion::MaxVersion),
		SLE_CONDVARNAME(CargoPacket, source.id,   "source_id",   SLE_UINT16, SaveLoadVersion::RemoveSubsidyStationBinding, SaveLoadVersion::MaxVersion),
		SLE_CONDVAR(CargoPacket, travelled.x, SLE_FILE_I16 | SLE_VAR_I32, SaveLoadVersion::CargoTravelled, SaveLoadVersion::MaxVersion),
		SLE_CONDVAR(CargoPacket, travelled.y, SLE_FILE_I16 | SLE_VAR_I32, SaveLoadVersion::CargoTravelled, SaveLoadVersion::MaxVersion),
	};
	return _cargopacket_desc;
}

struct CAPAChunkHandler : ChunkHandler {
	CAPAChunkHandler() : ChunkHandler('CAPA', ChunkType::Table) {}

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
			CargoPacket *cp = CargoPacket::CreateAtIndex(CargoPacketID(index));
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
