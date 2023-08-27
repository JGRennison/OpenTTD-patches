/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_sl.cpp Code handling saving and loading of orders */

#include "../stdafx.h"
#include "../order_backup.h"
#include "../settings_type.h"
#include "../network/network.h"

#include "saveload_internal.h"

#include "../safeguards.h"

static uint32 _jokerpp_separation_mode;
std::vector<OrderList *> _jokerpp_auto_separation;
std::vector<OrderList *> _jokerpp_non_auto_separation;

/**
 * Converts this order from an old savegame's version;
 * it moves all bits to the new location.
 */
void Order::ConvertFromOldSavegame()
{
	uint8 old_flags = this->flags;
	this->flags = 0;

	/* First handle non-stop - use value from savegame if possible, else use value from config file */
	if (_settings_client.gui.sg_new_nonstop || (IsSavegameVersionBefore(SLV_22) && _savegame_type != SGT_TTO && _savegame_type != SGT_TTD && (_settings_client.gui.new_nonstop || _settings_game.order.nonstop_only))) {
		/* OFB_NON_STOP */
		this->SetNonStopType((old_flags & 8) ? ONSF_NO_STOP_AT_ANY_STATION : ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
	} else {
		this->SetNonStopType((old_flags & 8) ? ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS : ONSF_STOP_EVERYWHERE);
	}

	switch (this->GetType()) {
		/* Only a few types need the other savegame conversions. */
		case OT_GOTO_DEPOT: case OT_GOTO_STATION: case OT_LOADING: break;
		default: return;
	}

	if (this->GetType() != OT_GOTO_DEPOT) {
		/* Then the load flags */
		if ((old_flags & 2) != 0) { // OFB_UNLOAD
			this->SetLoadType(OLFB_NO_LOAD);
		} else if ((old_flags & 4) == 0) { // !OFB_FULL_LOAD
			this->SetLoadType(OLF_LOAD_IF_POSSIBLE);
		} else {
			/* old OTTD versions stored full_load_any in config file - assume it was enabled when loading */
			this->SetLoadType(_settings_client.gui.sg_full_load_any || IsSavegameVersionBefore(SLV_22) ? OLF_FULL_LOAD_ANY : OLFB_FULL_LOAD);
		}

		if (this->IsType(OT_GOTO_STATION)) this->SetStopLocation(OSL_PLATFORM_FAR_END);

		/* Finally fix the unload flags */
		if ((old_flags & 1) != 0) { // OFB_TRANSFER
			this->SetUnloadType(OUFB_TRANSFER);
		} else if ((old_flags & 2) != 0) { // OFB_UNLOAD
			this->SetUnloadType(OUFB_UNLOAD);
		} else {
			this->SetUnloadType(OUF_UNLOAD_IF_POSSIBLE);
		}
	} else {
		/* Then the depot action flags */
		this->SetDepotActionType(((old_flags & 6) == 4) ? ODATFB_HALT : ODATF_SERVICE_ONLY);

		/* Finally fix the depot type flags */
		uint t = ((old_flags & 6) == 6) ? ODTFB_SERVICE : ODTF_MANUAL;
		if ((old_flags & 2) != 0) t |= ODTFB_PART_OF_ORDERS;
		this->SetDepotOrderType((OrderDepotTypeFlags)t);
	}
}

/**
 * Unpacks a order from savegames with version 4 and lower
 * @param packed packed order
 * @return unpacked order
 */
static Order UnpackVersion4Order(uint16 packed)
{
	return Order(((uint64) GB(packed, 8, 8)) << 24 | ((uint64) GB(packed, 4, 4)) << 8 | ((uint64) GB(packed, 0, 4)));
}

/**
 * Unpacks a order from savegames with version 5.1 and lower
 * @param packed packed order
 * @return unpacked order
 */
static Order UnpackVersion5Order(uint32 packed)
{
	return Order(((uint64) GB(packed, 16, 16)) << 24 | ((uint64) GB(packed, 8, 8)) << 8 | ((uint64) GB(packed, 0, 8)));
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
		SLE_CONDVAR_X(Order, flags,              SLE_FILE_U8 | SLE_VAR_U16,    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_FLAGS_EXTRA, 0, 0)),
		SLE_CONDVAR_X(Order, flags,              SLE_UINT16,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_FLAGS_EXTRA, 1)),
		SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
		     SLE_VAR(Order, dest,           SLE_UINT16),
		     SLE_REF(Order, next,           REF_ORDER),
		 SLE_CONDVAR(Order, refit_cargo,    SLE_UINT8,   SLV_36, SL_MAX_VERSION),
		SLE_CONDNULL(1,                                  SLV_36, SLV_182), // refit_subtype
		SLE_CONDVAR_X(Order, occupancy,     SLE_UINT8,           SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_OCCUPANCY)),
		SLE_CONDVAR_X(Order, wait_time,     SLE_FILE_U16 | SLE_VAR_U32,  SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 0, 5)),
		SLE_CONDVAR_X(Order, wait_time,     SLE_UINT32,                  SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 6)),
		SLE_CONDVAR_X(Order, travel_time,   SLE_FILE_U16 | SLE_VAR_U32,  SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 0, 5)),
		SLE_CONDVAR_X(Order, travel_time,   SLE_UINT32,                  SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 6)),
		 SLE_CONDVAR(Order, max_speed,      SLE_UINT16, SLV_172, SL_MAX_VERSION),
		SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_MORE_COND_ORDERS, 1, 6)), // jump_counter

		/* Leftover from the minor savegame version stuff
		 * We will never use those free bytes, but we have to keep this line to allow loading of old savegames */
		SLE_CONDNULL(10,                                  SLV_5,  SLV_36),
	};

	return _order_desc;
}

static std::vector<SaveLoad> _filtered_desc;

static void Save_ORDR()
{
	_filtered_desc = SlFilterObject(GetOrderDescription());
	for (Order *order : Order::Iterate()) {
		SlSetArrayIndex(order->index);
		SlObjectSaveFiltered(order, _filtered_desc);
	}
}

static void Load_ORDR()
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

			SlArray(orders, len, SLE_UINT16);

			for (size_t i = 0; i < len; ++i) {
				Order *o = new (i) Order();
				o->AssignOrder(UnpackVersion4Order(orders[i]));
			}

			free(orders);
		} else if (IsSavegameVersionBefore(SLV_5, 2)) {
			len /= sizeof(uint32);
			uint32 *orders = MallocT<uint32>(len + 1);

			SlArray(orders, len, SLE_UINT32);

			for (size_t i = 0; i < len; ++i) {
				Order *o = new (i) Order();
				o->AssignOrder(UnpackVersion5Order(orders[i]));
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
		_filtered_desc = SlFilterObject(GetOrderDescription());
		int index;

		while ((index = SlIterateArray()) != -1) {
			Order *order = new (index) Order();
			SlObjectLoadFiltered(order, _filtered_desc);
		}
	}
}

const SaveLoadTable GetOrderExtraInfoDescription()
{
	static const SaveLoad _order_extra_info_desc[] = {
		SLE_CONDARR_X(OrderExtraInfo, cargo_type_flags, SLE_UINT8, 32,        SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CARGO_TYPE_ORDERS, 1, 2)),
		SLE_CONDARR_X(OrderExtraInfo, cargo_type_flags, SLE_UINT8, NUM_CARGO, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CARGO_TYPE_ORDERS, 3)),
		SLE_CONDVAR_X(OrderExtraInfo, xflags,           SLE_UINT8,            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA)),
		SLE_CONDVAR_X(OrderExtraInfo, xdata,           SLE_UINT32,            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_EXTRA_DATA)),
		SLE_CONDVAR_X(OrderExtraInfo, xdata2,          SLE_UINT32,            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_EXTRA_DATA, 3)),
		SLE_CONDVAR_X(OrderExtraInfo, dispatch_index,  SLE_UINT16,            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SCHEDULED_DISPATCH, 3)),
		SLE_CONDVAR_X(OrderExtraInfo, colour,           SLE_UINT8,            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_EXTRA_DATA, 2)),
	};

	return _order_extra_info_desc;
}

void Save_ORDX()
{
	_filtered_desc = SlFilterObject(GetOrderExtraInfoDescription());
	for (Order *order : Order::Iterate()) {
		if (order->extra) {
			SlSetArrayIndex(order->index);
			SlObjectSaveFiltered(order->extra.get(), _filtered_desc);
		}
	}
}

void Load_ORDX()
{
	_filtered_desc = SlFilterObject(GetOrderExtraInfoDescription());
	int index;
	while ((index = SlIterateArray()) != -1) {
		Order *order = Order::GetIfValid(index);
		assert(order != nullptr);
		order->AllocExtraInfo();
		SlObjectLoadFiltered(order->extra.get(), _filtered_desc);
	}
}

static void Ptrs_ORDR()
{
	/* Orders from old savegames have pointers corrected in Load_ORDR */
	if (IsSavegameVersionBefore(SLV_5, 2)) return;

	for (Order *o : Order::Iterate()) {
		SlObject(o, GetOrderDescription());
	}
}

SaveLoadTable GetDispatchScheduleDescription()
{
	static const SaveLoad _order_extra_info_desc[] = {
		SLE_VARVEC(DispatchSchedule, scheduled_dispatch,                    SLE_UINT32),
		SLE_VAR(DispatchSchedule, scheduled_dispatch_duration,              SLE_UINT32),
		SLE_VAR(DispatchSchedule, scheduled_dispatch_start_date,            SLE_INT32),
		SLE_VAR(DispatchSchedule, scheduled_dispatch_start_full_date_fract, SLE_UINT16),
		SLE_VAR(DispatchSchedule, scheduled_dispatch_last_dispatch,         SLE_INT32),
		SLE_VAR(DispatchSchedule, scheduled_dispatch_max_delay,             SLE_INT32),
		SLE_CONDSSTR_X(DispatchSchedule, name, 0, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SCHEDULED_DISPATCH, 4)),
	};

	return _order_extra_info_desc;
}

SaveLoadTable GetOrderListDescription()
{
	static const SaveLoad _orderlist_desc[] = {
		      SLE_REF(OrderList, first,                                    REF_ORDER),
		SLEG_CONDVAR_X(_jokerpp_separation_mode,                           SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
		SLE_CONDNULL_X(21,                                                             SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	};

	return _orderlist_desc;
}

static void Save_ORDL()
{
	for (OrderList *list : OrderList::Iterate()) {
		SlSetArrayIndex(list->index);
		SlAutolength([](void *data) {
			OrderList *list = static_cast<OrderList *>(data);
			SlObject(list, GetOrderListDescription());
			SlWriteUint32(list->GetScheduledDispatchScheduleCount());
			for (DispatchSchedule &ds : list->GetScheduledDispatchScheduleSet()) {
				SlObject(&ds, GetDispatchScheduleDescription());
			}
		}, list);
	}
}

static void Load_ORDL()
{
	_jokerpp_auto_separation.clear();
	_jokerpp_non_auto_separation.clear();
	int index;

	while ((index = SlIterateArray()) != -1) {
		/* set num_orders to 0 so it's a valid OrderList */
		OrderList *list = new (index) OrderList(0);
		SlObject(list, GetOrderListDescription());
		if (SlXvIsFeaturePresent(XSLFI_JOKERPP)) {
			if (_jokerpp_separation_mode == 0) {
				_jokerpp_auto_separation.push_back(list);
			} else {
				_jokerpp_non_auto_separation.push_back(list);
			}
		}
		if (SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH)) {
			uint count = SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 3) ? SlReadUint32() : 1;
			list->GetScheduledDispatchScheduleSet().resize(count);
			for (DispatchSchedule &ds : list->GetScheduledDispatchScheduleSet()) {
				SlObject(&ds, GetDispatchScheduleDescription());
			}
		}
	}
}

void Ptrs_ORDL()
{
	for (OrderList *list : OrderList::Iterate()) {
		SlObject(list, GetOrderListDescription());
		list->ReindexOrderList();
	}
}

SaveLoadTable GetOrderBackupDescription()
{
	static const SaveLoad _order_backup_desc[] = {
		     SLE_VAR(OrderBackup, user,                     SLE_UINT32),
		     SLE_VAR(OrderBackup, tile,                     SLE_UINT32),
		     SLE_VAR(OrderBackup, group,                    SLE_UINT16),
		 SLE_CONDVAR(OrderBackup, service_interval,         SLE_FILE_U32 | SLE_VAR_U16,  SL_MIN_VERSION, SLV_192),
		 SLE_CONDVAR(OrderBackup, service_interval,         SLE_UINT16,                SLV_192, SL_MAX_VERSION),
		     SLE_STR(OrderBackup, name,                     SLE_STR, 0),
		SLE_CONDNULL(2,                                                                  SL_MIN_VERSION, SLV_192), // clone (2 bytes of pointer, i.e. garbage)
		 SLE_CONDREF(OrderBackup, clone,                    REF_VEHICLE,               SLV_192, SL_MAX_VERSION),
		     SLE_VAR(OrderBackup, cur_real_order_index,     SLE_VEHORDERID),
		 SLE_CONDVAR(OrderBackup, cur_implicit_order_index, SLE_VEHORDERID,            SLV_176, SL_MAX_VERSION),
		SLE_CONDVAR_X(OrderBackup, cur_timetable_order_index, SLE_VEHORDERID,   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA)),
		 SLE_CONDVAR(OrderBackup, current_order_time,       SLE_UINT32,                SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, lateness_counter,         SLE_INT32,                 SLV_176, SL_MAX_VERSION),
		 SLE_CONDVAR(OrderBackup, timetable_start,          SLE_INT32,                 SLV_176, SL_MAX_VERSION),
		SLE_CONDVAR_X(OrderBackup,timetable_start_subticks, SLE_UINT16,         SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLES_START_TICKS, 2)),
		 SLE_CONDVAR(OrderBackup, vehicle_flags,            SLE_FILE_U8  | SLE_VAR_U32,  SLV_176, SLV_180),
		SLE_CONDVAR_X(OrderBackup, vehicle_flags,           SLE_FILE_U16 | SLE_VAR_U32,          SLV_180, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEHICLE_FLAGS_EXTRA, 0, 0)),
		SLE_CONDVAR_X(OrderBackup, vehicle_flags,           SLE_UINT32,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEHICLE_FLAGS_EXTRA, 1)),
		     SLE_REF(OrderBackup, orders,                   REF_ORDER),
		SLE_CONDNULL_X(18,                                                      SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SCHEDULED_DISPATCH, 2, 2)),
	};

	return _order_backup_desc;
}

void Save_BKOR()
{
	/* We only save this when we're a network server
	 * as we want this information on our clients. For
	 * normal games this information isn't needed. */
	if (!_networking || !_network_server) return;

	for (OrderBackup *ob : OrderBackup::Iterate()) {
		SlSetArrayIndex(ob->index);
		SlAutolength([](void *data) {
			OrderBackup *ob = static_cast<OrderBackup *>(data);
			SlObject(ob, GetOrderBackupDescription());
			SlWriteUint32((uint)ob->dispatch_schedules.size());
			for (DispatchSchedule &ds : ob->dispatch_schedules) {
				SlObject(&ds, GetDispatchScheduleDescription());
			}
		}, ob);
	}
}

void Load_BKOR()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		/* set num_orders to 0 so it's a valid OrderList */
		OrderBackup *ob = new (index) OrderBackup();
		SlObject(ob, GetOrderBackupDescription());
		if (SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 3)) {
			uint count = SlReadUint32();
			ob->dispatch_schedules.resize(count);
			for (DispatchSchedule &ds : ob->dispatch_schedules) {
				SlObject(&ds, GetDispatchScheduleDescription());
			}
		}
	}
}

static void Ptrs_BKOR()
{
	for (OrderBackup *ob : OrderBackup::Iterate()) {
		SlObject(ob, GetOrderBackupDescription());
	}
}

static const ChunkHandler order_chunk_handlers[] = {
	{ 'BKOR', Save_BKOR, Load_BKOR, Ptrs_BKOR, nullptr, CH_ARRAY },
	{ 'ORDR', Save_ORDR, Load_ORDR, Ptrs_ORDR, nullptr, CH_ARRAY },
	{ 'ORDL', Save_ORDL, Load_ORDL, Ptrs_ORDL, nullptr, CH_ARRAY },
	{ 'ORDX', Save_ORDX, Load_ORDX, nullptr,   nullptr, CH_SPARSE_ARRAY },
};

extern const ChunkHandlerTable _order_chunk_handlers(order_chunk_handlers);
