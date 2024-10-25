/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_sl.cpp Code handling saving and loading of orders */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/order_sl_compat.h"

#include "../order_backup.h"
#include "../order_base.h"
#include "../settings_type.h"
#include "../network/network.h"

#include "../safeguards.h"

extern void RegisterOrderPoolItemReference(std::vector<Order> *orders, uint32_t ref);

namespace upstream_sl {

static uint32_t _order_item_ref;

/**
 * Unpacks a order from savegames with version 4 and lower
 * @param packed packed order
 * @return unpacked order
 */
static Order UnpackVersion4Order(uint16_t packed)
{
	return Order(GB(packed, 0, 4), GB(packed, 4, 4), GB(packed, 8, 8));
}

/**
 * Unpacks a order from savegames made with TTD(Patch)
 * @param packed packed order
 * @return unpacked order
 */
Order UnpackOldOrder(uint16_t packed)
{
	Order order = UnpackVersion4Order(packed);

	/*
	 * Sanity check
	 * TTD stores invalid orders as OT_NOTHING with non-zero flags/station
	 */
	if (order.IsType(OT_NOTHING) && packed != 0) order.MakeDummy();

	return order;
}

SaveLoadTable GetOrderDescription()
{
	static const SaveLoad _order_desc[] = {
		     SLE_VAR(Order, type,           SLE_UINT8),
		     SLE_VAR(Order, flags,          SLE_FILE_U8 | SLE_VAR_U16),
		     SLE_VAR(Order, dest,           SLE_UINT16),
		    SLEG_VAR("next", _order_item_ref, SLE_UINT32),
		 SLE_CONDVAR(Order, refit_cargo,    SLE_UINT8,   SLV_36, SL_MAX_VERSION),
		 SLE_CONDVAR(Order, wait_time,      SLE_FILE_U16 | SLE_VAR_U32,  SLV_67, SL_MAX_VERSION),
		 SLE_CONDVAR(Order, travel_time,    SLE_FILE_U16 | SLE_VAR_U32,  SLV_67, SL_MAX_VERSION),
		 SLE_CONDVAR(Order, max_speed,      SLE_UINT16, SLV_172, SL_MAX_VERSION),
	};

	return _order_desc;
}

struct ORDRChunkHandler : ChunkHandler {
	ORDRChunkHandler() : ChunkHandler('ORDR', CH_TABLE) {}

	void Save() const override
	{
		NOT_REACHED();
	}

	void Load() const override
	{
		if (IsSavegameVersionBefore(SLV_5, 2)) {
			NOT_REACHED();
		} else {
			const std::vector<SaveLoad> slt = SlCompatTableHeader(GetOrderDescription(), _order_sl_compat);

			int index;

			while ((index = SlIterateArray()) != -1) {
				OrderPoolItem *item = new (index) OrderPoolItem();
				SlObject(&item->order, slt);
				item->next_ref = _order_item_ref;
			}
		}
	}
};

SaveLoadTable GetOrderListDescription()
{
	static const SaveLoad _orderlist_desc[] = {
		SLEG_VAR("first",  _order_item_ref,    SLE_UINT32),
	};

	return _orderlist_desc;
}

struct ORDLChunkHandler : ChunkHandler {
	ORDLChunkHandler() : ChunkHandler('ORDL', CH_TABLE) {}

	void Save() const override
	{
		const SaveLoadTable slt = GetOrderListDescription();
		SlTableHeader(slt);

		for (OrderList *list : OrderList::Iterate()) {
			SlSetArrayIndex(list->index);
			SlObject(list, slt);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(GetOrderListDescription(), _orderlist_sl_compat);

		int index;

		while ((index = SlIterateArray()) != -1) {
			/* set num_orders to 0 so it's a valid OrderList */
			OrderList *list = new (index) OrderList();
			SlObject(list, slt);
			RegisterOrderPoolItemReference(&list->GetOrderVector(), _order_item_ref);
		}

	}
};

SaveLoadTable GetOrderBackupDescription()
{
	static const SaveLoad _order_backup_desc[] = {
		     SLE_VAR(OrderBackup, user,                     SLE_UINT32),
		     SLE_VAR(OrderBackup, tile,                     SLE_UINT32),
		     SLE_VAR(OrderBackup, group,                    SLE_UINT16),
		 SLE_CONDVAR(OrderBackup, service_interval,         SLE_FILE_U32 | SLE_VAR_U16,  SL_MIN_VERSION, SLV_192),
		 SLE_CONDVAR(OrderBackup, service_interval,         SLE_UINT16,                SLV_192, SL_MAX_VERSION),
		     SLE_STR(OrderBackup, name,                     SLE_STR, 0),
		 SLE_CONDREF(OrderBackup, clone,                    REF_VEHICLE,               SLV_192, SL_MAX_VERSION),
		     SLE_VAR(OrderBackup, cur_real_order_index,     SLE_FILE_U8 | SLE_VAR_U16),
		 SLE_CONDVAR(OrderBackup, cur_implicit_order_index, SLE_FILE_U8 | SLE_VAR_U16, SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, current_order_time,       SLE_UINT32,                SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, lateness_counter,         SLE_INT32,                 SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, timetable_start,          SLE_FILE_I32 | SLE_VAR_I64, SLV_176, SLV_TIMETABLE_START_TICKS_FIX),
		 SLE_CONDVAR(OrderBackup, timetable_start,          SLE_FILE_U64 | SLE_VAR_I64, SLV_TIMETABLE_START_TICKS_FIX, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, vehicle_flags,            SLE_FILE_U8  | SLE_VAR_U32, SLV_176, SLV_180),
		 SLE_CONDVAR(OrderBackup, vehicle_flags,            SLE_FILE_U16 | SLE_VAR_U32, SLV_180, SL_MAX_VERSION),
		    SLEG_VAR("orders",    _order_item_ref,          SLE_UINT32),
	};

	return _order_backup_desc;
}

struct BKORChunkHandler : ChunkHandler {
	BKORChunkHandler() : ChunkHandler('BKOR', CH_TABLE) {}

	void Save() const override
	{
		const SaveLoadTable slt = GetOrderBackupDescription();
		SlTableHeader(slt);

		/* We only save this when we're a network server
		 * as we want this information on our clients. For
		 * normal games this information isn't needed. */
		if (!_networking || !_network_server) return;

		for (OrderBackup *ob : OrderBackup::Iterate()) {
			SlSetArrayIndex(ob->index);
			SlObject(ob, slt);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(GetOrderBackupDescription(), _order_backup_sl_compat);

		int index;

		while ((index = SlIterateArray()) != -1) {
			/* set num_orders to 0 so it's a valid OrderList */
			OrderBackup *ob = new (index) OrderBackup();
			SlObject(ob, slt);
			if (ob->cur_real_order_index == 0xFF) ob->cur_real_order_index = INVALID_VEH_ORDER_ID;
			if (ob->cur_implicit_order_index == 0xFF) ob->cur_implicit_order_index = INVALID_VEH_ORDER_ID;
			RegisterOrderPoolItemReference(&ob->orders, _order_item_ref);
		}
	}

	void FixPointers() const override
	{
		for (OrderBackup *ob : OrderBackup::Iterate()) {
			SlObject(ob, GetOrderBackupDescription());
		}
	}
};

static const BKORChunkHandler BKOR;
static const ORDRChunkHandler ORDR;
static const ORDLChunkHandler ORDL;
static const ChunkHandlerRef order_chunk_handlers[] = {
	BKOR,
	ORDR,
	ORDL,
};

extern const ChunkHandlerTable _order_chunk_handlers(order_chunk_handlers);

}
