/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file order_sl.cpp Code handling saving and loading of orders. */

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

SaveLoadTable GetOrderDescription()
{
	static const SaveLoad _order_desc[] = {
		     SLE_VAR(Order, type,           SLE_UINT8),
		     SLE_VAR(Order, flags,          SLE_FILE_U8 | SLE_VAR_U16),
		     SLE_VAR(Order, dest,           SLE_UINT16),
		    SLEG_VAR("next", _order_item_ref, SLE_UINT32),
		 SLE_CONDVAR(Order, refit_cargo,    SLE_UINT8,   SaveLoadVersion::RefitOrders, SaveLoadVersion::MaxVersion),
		 SLE_CONDVAR(Order, wait_time,      SLE_FILE_U16 | SLE_VAR_U32,  SaveLoadVersion::Timetables, SaveLoadVersion::MaxVersion),
		 SLE_CONDVAR(Order, travel_time,    SLE_FILE_U16 | SLE_VAR_U32,  SaveLoadVersion::Timetables, SaveLoadVersion::MaxVersion),
		 SLE_CONDVAR(Order, max_speed,      SLE_UINT16, SaveLoadVersion::OrderMaxSpeed, SaveLoadVersion::MaxVersion),
	};

	return _order_desc;
}

struct ORDRChunkHandler : ChunkHandler {
	ORDRChunkHandler() : ChunkHandler('ORDR', ChunkType::ReadOnly) {}

	void Save() const override
	{
		NOT_REACHED();
	}

	void Load() const override
	{
		if (IsSavegameVersionBefore(SaveLoadVersion::BigMap, 2)) {
			NOT_REACHED();
		} else {
			const std::vector<SaveLoad> slt = SlCompatTableHeader(GetOrderDescription(), _order_sl_compat);

			int index;

			while ((index = SlIterateArray()) != -1) {
				OrderPoolItem *item = OrderPoolItem::CreateAtIndex(OrderID(index));
				SlObject(&item->order, slt);
				item->next_ref = _order_item_ref;
			}
		}
	}
};

template <typename T>
class SlOrders : public VectorSaveLoadHandler<SlOrders<T>, T, Order> {
public:
	static inline const SaveLoad description[] = {
		SLE_VAR(Order, type,        SLE_UINT8),
		SLE_VAR(Order, flags,       SLE_FILE_U8 | SLE_VAR_U16),
		SLE_VAR(Order, dest,        SLE_UINT16),
		SLE_VAR(Order, refit_cargo, SLE_UINT8),
		SLE_VAR(Order, wait_time,   SLE_FILE_U16 | SLE_VAR_U32),
		SLE_VAR(Order, travel_time, SLE_FILE_U16 | SLE_VAR_U32),
		SLE_VAR(Order, max_speed,   SLE_UINT16),
	};
	static inline const SaveLoadCompatTable compat_description = {};

	std::vector<Order> &GetVector(T *container) const override { return container->orders; }

	void LoadCheck(T *container) const override { this->Load(container); }
};

/* Instantiate SlOrders classes. */
template class SlOrders<OrderList>;
template class SlOrders<OrderBackup>;

SaveLoadTable GetOrderListDescription()
{
	static const SaveLoad _orderlist_desc[] = {
		SLEG_CONDVAR("first", _order_item_ref, SLE_UINT32, SaveLoadVersion::MinVersion, SaveLoadVersion::OrdersOwnedByOrderlist),
		SLEG_CONDSTRUCTLIST("orders", SlOrders<OrderList>, SaveLoadVersion::OrdersOwnedByOrderlist, SaveLoadVersion::MaxVersion),
	};

	return _orderlist_desc;
}

struct ORDLChunkHandler : ChunkHandler {
	ORDLChunkHandler() : ChunkHandler('ORDL', ChunkType::Table) {}

	void Save() const override
	{
		NOT_REACHED();
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(GetOrderListDescription(), _orderlist_sl_compat);
		const bool old_mode = IsSavegameVersionBefore(SaveLoadVersion::OrdersOwnedByOrderlist);

		int index;

		while ((index = SlIterateArray()) != -1) {
			/* set num_orders to 0 so it's a valid OrderList */
			OrderList *list = OrderList::CreateAtIndex(OrderListID(index));
			SlObject(list, slt);
			if (old_mode) {
				RegisterOrderPoolItemReference(&list->GetOrderVector(), _order_item_ref);
			}
		}
	}
};

SaveLoadTable GetOrderBackupDescription()
{
	static const SaveLoad _order_backup_desc[] = {
		     SLE_VAR(OrderBackup, user,                     SLE_UINT32),
		     SLE_VAR(OrderBackup, tile,                     SLE_UINT32),
		     SLE_VAR(OrderBackup, group,                    SLE_UINT16),
		 SLE_CONDVAR(OrderBackup, service_interval, SLE_FILE_U32 | SLE_VAR_U16, SaveLoadVersion::MinVersion, SaveLoadVersion::FixOrderBackup),
		 SLE_CONDVAR(OrderBackup, service_interval, SLE_UINT16, SaveLoadVersion::FixOrderBackup, SaveLoadVersion::MaxVersion),
		     SLE_STR(OrderBackup, name,                     SLE_STR, 0),
		 SLE_CONDREF(OrderBackup, clone, REF_VEHICLE, SaveLoadVersion::FixOrderBackup, SaveLoadVersion::MaxVersion),
		     SLE_VAR(OrderBackup, cur_real_order_index,     SLE_FILE_U8 | SLE_VAR_U16),
		 SLE_CONDVAR(OrderBackup, cur_implicit_order_index, SLE_FILE_U8 | SLE_VAR_U16, SaveLoadVersion::BackupOrderState, SaveLoadVersion::MaxVersion),
		 SLE_CONDVAR(OrderBackup, current_order_time, SLE_UINT32, SaveLoadVersion::BackupOrderState, SaveLoadVersion::MaxVersion),
		 SLE_CONDVAR(OrderBackup, lateness_counter, SLE_INT32, SaveLoadVersion::BackupOrderState, SaveLoadVersion::MaxVersion),
		 SLE_CONDVAR(OrderBackup, timetable_start, SLE_FILE_I32 | SLE_VAR_I64, SaveLoadVersion::BackupOrderState, SaveLoadVersion::TimetableStartTicksFix),
		 SLE_CONDVAR(OrderBackup, timetable_start, SLE_FILE_U64 | SLE_VAR_I64, SaveLoadVersion::TimetableStartTicksFix, SaveLoadVersion::MaxVersion),
		 SLE_CONDVAR(OrderBackup, vehicle_flags, SLE_FILE_U8 | SLE_VAR_U32, SaveLoadVersion::BackupOrderState, SaveLoadVersion::ServiceIntervalPercent),
		 SLE_CONDVAR(OrderBackup, vehicle_flags, SLE_FILE_U16 | SLE_VAR_U32, SaveLoadVersion::ServiceIntervalPercent, SaveLoadVersion::MaxVersion),
		 SLEG_CONDVAR("orders", _order_item_ref,  SLE_UINT32, SaveLoadVersion::MinVersion, SaveLoadVersion::OrdersOwnedByOrderlist),
		SLEG_CONDSTRUCTLIST("orders", SlOrders<OrderBackup>, SaveLoadVersion::OrdersOwnedByOrderlist, SaveLoadVersion::MaxVersion),
	};

	return _order_backup_desc;
}

struct BKORChunkHandler : ChunkHandler {
	BKORChunkHandler() : ChunkHandler('BKOR', ChunkType::Table) {}

	void Save() const override
	{
		NOT_REACHED();
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(GetOrderBackupDescription(), _order_backup_sl_compat);
		const bool old_mode = IsSavegameVersionBefore(SaveLoadVersion::OrdersOwnedByOrderlist);

		int index;

		while ((index = SlIterateArray()) != -1) {
			/* set num_orders to 0 so it's a valid OrderList */
			OrderBackup *ob = OrderBackup::CreateAtIndex(OrderBackupID(index));
			SlObject(ob, slt);
			if (ob->cur_real_order_index == 0xFF) ob->cur_real_order_index = INVALID_VEH_ORDER_ID;
			if (ob->cur_implicit_order_index == 0xFF) ob->cur_implicit_order_index = INVALID_VEH_ORDER_ID;
			if (old_mode) {
				RegisterOrderPoolItemReference(&ob->orders, _order_item_ref);
			}
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
