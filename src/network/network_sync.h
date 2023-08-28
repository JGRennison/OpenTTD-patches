/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_sync.h Variables and function used for network sync tracking. */

#ifndef NETWORK_SYNC_H
#define NETWORK_SYNC_H

#include "../core/ring_buffer.hpp"

/* Sync debugging */
struct NetworkSyncRecord {
	uint32 frame;
	uint32 seed_1;
	uint64 state_checksum;
};
extern ring_buffer<NetworkSyncRecord> _network_sync_records;
extern ring_buffer<uint> _network_sync_record_counts;
extern bool _record_sync_records;

enum NetworkSyncRecordEvents : uint32 {
	NSRE_BEGIN,
	NSRE_CMD,
	NSRE_AUX_TILE,
	NSRE_TILE,
	NSRE_TOWN,
	NSRE_TREE,
	NSRE_STATION,
	NSRE_INDUSTRY,
	NSRE_PRE_COMPANY_STATE,
	NSRE_VEH_PERIODIC,
	NSRE_VEH_LOAD_UNLOAD,
	NSRE_VEH_EFFECT,
	NSRE_VEH_TRAIN,
	NSRE_VEH_ROAD,
	NSRE_VEH_AIR,
	NSRE_VEH_SHIP,
	NSRE_VEH_OTHER,
	NSRE_VEH_SELL,
	NSRE_VEH_TBTR,
	NSRE_VEH_AUTOREPLACE,
	NSRE_VEH_REPAIR,
	NSRE_FRAME_DONE,
	NSRE_LAST,
};

void RecordSyncEventData(NetworkSyncRecordEvents event);
const char *GetSyncRecordEventName(NetworkSyncRecordEvents event);

inline void RecordSyncEvent(NetworkSyncRecordEvents event)
{
	if (_record_sync_records) {
		RecordSyncEventData(event);
	}
}

#endif /* NETWORK_SYNC_H */
