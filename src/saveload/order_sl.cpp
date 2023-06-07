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

namespace upstream_sl {

/**
 * Unpacks a order from savegames with version 4 and lower
 * @param packed packed order
 * @return unpacked order
 */
static Order UnpackVersion4Order(uint16 packed)
{
	return Order(GB(packed, 8, 8) << 16 | GB(packed, 4, 4) << 8 | GB(packed, 0, 4));
}

/**
 * Unpacks a order from savegames made with TTD(Patch)
 * @param packed packed order
 * @return unpacked order
 */
Order UnpackOldOrder(uint16 packed)
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
		     SLE_REF(Order, next,           REF_ORDER),
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
		const SaveLoadTable slt = GetOrderDescription();
		SlTableHeader(slt);

		for (Order *order : Order::Iterate()) {
			SlSetArrayIndex(order->index);
			SlObject(order, slt);
		}
	}

	void Load() const override
	{
		if (IsSavegameVersionBefore(SLV_5, 2)) {
			/* Version older than 5.2 did not have a ->next pointer. Convert them
			 * (in the old days, the orderlist was 5000 items big) */
			size_t len = SlGetFieldLength();

			if (IsSavegameVersionBefore(SLV_5)) {
				/* Pre-version 5 had another layout for orders
				 * (uint16 instead of uint32) */
				len /= sizeof(uint16);
				uint16 *orders = MallocT<uint16>(len + 1);

				SlCopy(orders, len, SLE_UINT16);

				for (size_t i = 0; i < len; ++i) {
					Order *o = new (i) Order();
					o->AssignOrder(UnpackVersion4Order(orders[i]));
				}

				free(orders);
			} else if (IsSavegameVersionBefore(SLV_5, 2)) {
				len /= sizeof(uint32);
				uint32 *orders = MallocT<uint32>(len + 1);

				SlCopy(orders, len, SLE_UINT32);

				for (size_t i = 0; i < len; ++i) {
					new (i) Order(orders[i]);
				}

				free(orders);
			}

			/* Update all the next pointer */
			for (Order *o : Order::Iterate()) {
				size_t order_index = o->index;
				/* Delete invalid orders */
				if (o->IsType(OT_NOTHING)) {
					delete o;
					continue;
				}
				/* The orders were built like this:
				 * While the order is valid, set the previous will get its next pointer set */
				Order *prev = Order::GetIfValid(order_index - 1);
				if (prev != nullptr) prev->next = o;
			}
		} else {
			const std::vector<SaveLoad> slt = SlCompatTableHeader(GetOrderDescription(), _order_sl_compat);

			int index;

			while ((index = SlIterateArray()) != -1) {
				Order *order = new (index) Order();
				SlObject(order, slt);
			}
		}
	}

	void FixPointers() const override
	{
		/* Orders from old savegames have pointers corrected in Load_ORDR */
		if (IsSavegameVersionBefore(SLV_5, 2)) return;

		for (Order *o : Order::Iterate()) {
			SlObject(o, GetOrderDescription());
		}
	}
};

SaveLoadTable GetOrderListDescription()
{
	static const SaveLoad _orderlist_desc[] = {
		SLE_REF(OrderList, first,              REF_ORDER),
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
			OrderList *list = new (index) OrderList(0);
			SlObject(list, slt);
		}

	}

	void FixPointers() const override
	{
		for (OrderList *list : OrderList::Iterate()) {
			SlObject(list, GetOrderListDescription());
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
		    SLE_SSTR(OrderBackup, name,                     SLE_STR),
		 SLE_CONDREF(OrderBackup, clone,                    REF_VEHICLE,               SLV_192, SL_MAX_VERSION),
		     SLE_VAR(OrderBackup, cur_real_order_index,     SLE_FILE_U8 | SLE_VAR_U16),
		 SLE_CONDVAR(OrderBackup, cur_implicit_order_index, SLE_FILE_U8 | SLE_VAR_U16, SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, current_order_time,       SLE_UINT32,                SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, lateness_counter,         SLE_INT32,                 SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, timetable_start,          SLE_INT32,                 SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, vehicle_flags,            SLE_FILE_U8  | SLE_VAR_U32, SLV_176, SLV_180),
		 SLE_CONDVAR(OrderBackup, vehicle_flags,            SLE_FILE_U16 | SLE_VAR_U32, SLV_180, SL_MAX_VERSION),
		     SLE_REF(OrderBackup, orders,                   REF_ORDER),
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
