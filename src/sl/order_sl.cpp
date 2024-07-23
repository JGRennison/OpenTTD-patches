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

static uint32_t _jokerpp_separation_mode;
std::vector<OrderList *> _jokerpp_auto_separation;
std::vector<OrderList *> _jokerpp_non_auto_separation;

static uint16_t _old_scheduled_dispatch_start_full_date_fract;
btree::btree_map<DispatchSchedule *, uint16_t> _old_scheduled_dispatch_start_full_date_fract_map;
static std::vector<uint32_t> _old_scheduled_dispatch_slots;

/**
 * Converts this order from an old savegame's version;
 * it moves all bits to the new location.
 */
void Order::ConvertFromOldSavegame()
{
	uint8_t old_flags = this->flags;
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
static Order UnpackVersion4Order(uint16_t packed)
{
	return Order(((uint64_t) GB(packed, 8, 8)) << 24 | ((uint64_t) GB(packed, 4, 4)) << 8 | ((uint64_t) GB(packed, 0, 4)));
}

/**
 * Unpacks a order from savegames with version 5.1 and lower
 * @param packed packed order
 * @return unpacked order
 */
static Order UnpackVersion5Order(uint32_t packed)
{
	return Order(((uint64_t) GB(packed, 16, 16)) << 24 | ((uint64_t) GB(packed, 8, 8)) << 8 | ((uint64_t) GB(packed, 0, 8)));
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

NamedSaveLoadTable GetOrderExtraInfoDescription()
{
	static const NamedSaveLoad _order_extra_info_desc[] = {
		NSL("cargo_type_flags", SLE_CONDARR_X(OrderExtraInfo, cargo_type_flags, SLE_UINT8, 32,        SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CARGO_TYPE_ORDERS, 1, 2))),
		NSL("cargo_type_flags", SLE_CONDARR_X(OrderExtraInfo, cargo_type_flags, SLE_UINT8, NUM_CARGO, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CARGO_TYPE_ORDERS, 3))),
		NSL("xflags",           SLE_CONDVAR_X(OrderExtraInfo, xflags,           SLE_UINT8,            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA))),
		NSL("xdata",            SLE_CONDVAR_X(OrderExtraInfo, xdata,           SLE_UINT32,            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_EXTRA_DATA))),
		NSL("xdata2",           SLE_CONDVAR_X(OrderExtraInfo, xdata2,          SLE_UINT32,            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_EXTRA_DATA, 3))),
		NSL("dispatch_index",   SLE_CONDVAR_X(OrderExtraInfo, dispatch_index,  SLE_UINT16,            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SCHEDULED_DISPATCH, 3))),
		NSL("colour",           SLE_CONDVAR_X(OrderExtraInfo, colour,           SLE_UINT8,            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_EXTRA_DATA, 2))),
	};

	return _order_extra_info_desc;
}

struct OrderExtraDataStructHandler final : public TypedSaveLoadStructHandler<OrderExtraDataStructHandler, Order> {
	NamedSaveLoadTable GetDescription() const override
	{
		return GetOrderExtraInfoDescription();
	}

	void Save(Order *order) const override
	{
		if (!order->extra) return;

		SlObjectSaveFiltered(order->extra.get(), this->GetLoadDescription());
	}

	void Load(Order *order) const override
	{
		order->AllocExtraInfo();
		SlObjectLoadFiltered(order->extra.get(), this->GetLoadDescription());
	}
};

NamedSaveLoadTable GetOrderDescription()
{
	static const NamedSaveLoad _order_desc[] = {
		NSL("type",                SLE_VAR(Order, type,               SLE_UINT8)),
		NSL("flags",         SLE_CONDVAR_X(Order, flags,              SLE_FILE_U8 | SLE_VAR_U16,    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_FLAGS_EXTRA, 0, 0))),
		NSL("flags",         SLE_CONDVAR_X(Order, flags,              SLE_UINT16,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_FLAGS_EXTRA, 1))),
		NSL("",             SLE_CONDNULL_X(1,                                                       SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP))),
		NSL("dest",                SLE_VAR(Order, dest,               SLE_UINT16)),
		NSL("next",                SLE_REF(Order, next,               REF_ORDER)),
		NSL("refit_cargo",     SLE_CONDVAR(Order, refit_cargo,        SLE_UINT8,                    SLV_36, SL_MAX_VERSION)),
		NSL("",               SLE_CONDNULL(1,                                                       SLV_36, SLV_182)), // refit_subtype
		NSL("occupancy",     SLE_CONDVAR_X(Order, occupancy,          SLE_UINT8,                    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_OCCUPANCY))),
		NSL("wait_time",     SLE_CONDVAR_X(Order, wait_time,          SLE_FILE_U16 | SLE_VAR_U32,   SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 0, 5))),
		NSL("wait_time",     SLE_CONDVAR_X(Order, wait_time,          SLE_UINT32,                   SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 6))),
		NSL("travel_time",   SLE_CONDVAR_X(Order, travel_time,        SLE_FILE_U16 | SLE_VAR_U32,   SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 0, 5))),
		NSL("travel_time",   SLE_CONDVAR_X(Order, travel_time,        SLE_UINT32,                   SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 6))),
		NSL("max_speed",       SLE_CONDVAR(Order, max_speed,          SLE_UINT16,                   SLV_172, SL_MAX_VERSION)),
		NSL("",             SLE_CONDNULL_X(1,                                                       SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_MORE_COND_ORDERS, 1, 6))), // jump_counter

		/* Leftover from the minor savegame version stuff
		 * We will never use those free bytes, but we have to keep this line to allow loading of old savegames */
		NSL("",               SLE_CONDNULL(10,                                                      SLV_5,  SLV_36)),

		NSLT_STRUCT<OrderExtraDataStructHandler>("extra"),
	};

	return _order_desc;
}

static void Save_ORDR()
{
	SaveLoadTableData slt = SlTableHeader(GetOrderDescription());

	for (Order *order : Order::Iterate()) {
		SlSetArrayIndex(order->index);
		SlObjectSaveFiltered(order, slt);
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
			 * (uint16_t instead of uint32_t) */
			len /= sizeof(uint16_t);
			uint16_t *orders = MallocT<uint16_t>(len + 1);

			SlArray(orders, len, SLE_UINT16);

			for (size_t i = 0; i < len; ++i) {
				Order *o = new (i) Order();
				o->AssignOrder(UnpackVersion4Order(orders[i]));
			}

			free(orders);
		} else if (IsSavegameVersionBefore(SLV_5, 2)) {
			len /= sizeof(uint32_t);
			uint32_t *orders = MallocT<uint32_t>(len + 1);

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
		SaveLoadTableData slt = SlTableHeaderOrRiff(GetOrderDescription());

		int index;
		while ((index = SlIterateArray()) != -1) {
			Order *order = new (index) Order();
			SlObjectLoadFiltered(order, slt);
		}
	}
}

void Load_ORDX()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(GetOrderExtraInfoDescription());

	int index;
	while ((index = SlIterateArray()) != -1) {
		Order *order = Order::GetIfValid(index);
		assert(order != nullptr);
		order->AllocExtraInfo();
		SlObjectLoadFiltered(order->extra.get(), slt);
	}
}

static void Ptrs_ORDR()
{
	/* Orders from old savegames have pointers corrected in Load_ORDR */
	if (IsSavegameVersionBefore(SLV_5, 2)) return;

	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(GetOrderDescription());

	for (Order *o : Order::Iterate()) {
		SlObjectPtrOrNullFiltered(o, slt);
	}
}

NamedSaveLoadTable GetDispatchSlotDescription()
{
	static const NamedSaveLoad _dispatch_slot_info_desc[] = {
		NSL("offset",        SLE_VAR(DispatchSlot, offset,                                       SLE_UINT32)),
		NSL("flags",         SLE_VAR(DispatchSlot, flags,                                        SLE_UINT16)),
	};

	return _dispatch_slot_info_desc;
}

struct DispatchSlotStructHandler final : public TypedSaveLoadStructHandler<DispatchSlotStructHandler, DispatchSchedule> {
	NamedSaveLoadTable GetDescription() const override
	{
		return GetDispatchSlotDescription();
	}

	void Save(DispatchSchedule *ds) const override
	{
		SlSetStructListLength(ds->GetScheduledDispatchMutable().size());
		for (DispatchSlot &slot : ds->GetScheduledDispatchMutable()) {
			SlObjectSaveFiltered(&slot, this->GetLoadDescription());
		}
	}

	void Load(DispatchSchedule *ds) const override
	{
		ds->GetScheduledDispatchMutable().resize(SlGetStructListLength(UINT32_MAX));
		for (DispatchSlot &slot : ds->GetScheduledDispatchMutable()) {
			SlObjectLoadFiltered(&slot, this->GetLoadDescription());
		}
	}
};

using DispatchSupplementaryNamePair = std::pair<const uint32_t, std::string>;

NamedSaveLoadTable GetDispatchSupplementaryNamePairDescription()
{
	static const NamedSaveLoad _dispatch_name_pair_desc[] = {
		NSL("key",           SLTAG(SLTAG_CUSTOM_START,      SLE_VAR(DispatchSupplementaryNamePair, first,                                       SLE_UINT32))),
		NSL("value",         SLTAG(SLTAG_CUSTOM_START + 1, SLE_SSTR(DispatchSupplementaryNamePair, second,                                      SLE_STR))),
	};

	return _dispatch_name_pair_desc;
}

struct DispatchNameStructHandler final : public TypedSaveLoadStructHandler<DispatchNameStructHandler, DispatchSchedule> {
	NamedSaveLoadTable GetDescription() const override
	{
		return GetDispatchSupplementaryNamePairDescription();
	}

	void Save(DispatchSchedule *ds) const override
	{
		btree::btree_map<uint32_t, std::string> &names = ds->GetSupplementaryNameMap();
		SlSetStructListLength(names.size());
		for (DispatchSupplementaryNamePair &it : names) {
			SlObjectSaveFiltered(&it, this->GetLoadDescription());
		}
	}

	void Load(DispatchSchedule *ds) const override
	{
		SaveLoadTable slt = this->GetLoadDescription();
		if (slt.size() != 2 || slt[0].label_tag != SLTAG_CUSTOM_START || slt[1].label_tag != SLTAG_CUSTOM_START + 1) {
			SlErrorCorrupt("Dispatch names sub-chunk fields not as expected");
		}

		size_t string_count = SlGetStructListLength(UINT32_MAX);
		btree::btree_map<uint32_t, std::string> &names = ds->GetSupplementaryNameMap();
		for (size_t i = 0; i < string_count; i++) {
			uint32_t key = SlReadUint32();
			SlStdString(&(names[key]), SLE_STR);
		}
	}
};

NamedSaveLoadTable GetDispatchScheduleDescription()
{
	static const NamedSaveLoad _dispatch_scheduled_info_desc[] = {
		NSL("",              SLEG_CONDVARVEC_X(_old_scheduled_dispatch_slots,                    SLE_UINT32,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SCHEDULED_DISPATCH, 1, 6))),
		NSL("duration",      SLE_VAR(DispatchSchedule, scheduled_dispatch_duration,              SLE_UINT32)),
		NSL("",              SLE_CONDVAR_X(DispatchSchedule, scheduled_dispatch_start_tick,      SLE_FILE_I32 | SLE_VAR_I64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SCHEDULED_DISPATCH, 1, 4))),
		NSL("",              SLEG_CONDVAR_X(_old_scheduled_dispatch_start_full_date_fract,       SLE_UINT16,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SCHEDULED_DISPATCH, 1, 4))),
		NSL("start_tick",    SLE_CONDVAR_X(DispatchSchedule, scheduled_dispatch_start_tick,      SLE_INT64,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SCHEDULED_DISPATCH, 5))),
		NSL("last_dispatch", SLE_VAR(DispatchSchedule, scheduled_dispatch_last_dispatch,         SLE_INT32)),
		NSL("max_delay",     SLE_VAR(DispatchSchedule, scheduled_dispatch_max_delay,             SLE_INT32)),
		NSL("name",          SLE_CONDSSTR_X(DispatchSchedule, name,                              SLE_STR,                    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SCHEDULED_DISPATCH, 4))),
		NSL("flags",         SLE_CONDVAR_X(DispatchSchedule, scheduled_dispatch_flags,           SLE_FILE_U32 | SLE_VAR_U8,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SCHEDULED_DISPATCH, 6))),

		NSLT_STRUCTLIST<DispatchSlotStructHandler>("slots"),
		NSLT_STRUCTLIST<DispatchNameStructHandler>("names"),
	};

	return _dispatch_scheduled_info_desc;
}

struct ScheduledDispatchNonTableHelper {
	std::vector<SaveLoad> dispatch_desc;
	std::vector<SaveLoad> slot_desc;

	void Setup()
	{
		this->dispatch_desc = SlFilterNamedSaveLoadTable(GetDispatchScheduleDescription());
		this->slot_desc = SlFilterNamedSaveLoadTable(GetDispatchSlotDescription());
	}

	void LoadDispatchSchedule(DispatchSchedule &ds)
	{
		SlObjectLoadFiltered(&ds, this->dispatch_desc);
		if (SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 1, 4) && _old_scheduled_dispatch_start_full_date_fract != 0) {
			_old_scheduled_dispatch_start_full_date_fract_map[&ds] = _old_scheduled_dispatch_start_full_date_fract;
		}

		if (SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 1, 6)) {
			ds.GetScheduledDispatchMutable().reserve(_old_scheduled_dispatch_slots.size());
			for (uint32_t slot : _old_scheduled_dispatch_slots) {
				ds.GetScheduledDispatchMutable().push_back({ slot, 0 });
			}
		} else {
			ds.GetScheduledDispatchMutable().resize(SlReadUint32());
			for (DispatchSlot &slot : ds.GetScheduledDispatchMutable()) {
				SlObjectLoadFiltered(&slot, this->slot_desc);
			}
		}

		if (SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 8)) {
			uint32_t string_count = SlReadUint32();
			btree::btree_map<uint32_t, std::string> &names = ds.GetSupplementaryNameMap();
			for (uint32_t i = 0; i < string_count; i++) {
				uint32_t key = SlReadUint32();
				SlStdString(&(names[key]), SLE_STR);
			}
		}
	}
};

struct DispatchScheduleStructHandlerBase : public SaveLoadStructHandler {
	NamedSaveLoadTable GetDescription() const override
	{
		return GetDispatchScheduleDescription();
	}

	void SaveSchedules(std::vector<DispatchSchedule> &schedules) const
	{
		SlSetStructListLength(schedules.size());
		for (DispatchSchedule &ds : schedules) {
			SlObjectSaveFiltered(&ds, this->GetLoadDescription());
		}
	}

	void LoadSchedules(std::vector<DispatchSchedule> &schedules) const
	{
		schedules.resize(SlGetStructListLength(UINT32_MAX));
		for (DispatchSchedule &ds : schedules) {
			SlObjectLoadFiltered(&ds, this->GetLoadDescription());
		}
	}
};

struct OrderListDispatchScheduleStructHandler final : public DispatchScheduleStructHandlerBase {
	void Save(void *object) const override { this->SaveSchedules(static_cast<OrderList *>(object)->GetScheduledDispatchScheduleSet()); }

	void Load(void *object) const override { this->LoadSchedules(static_cast<OrderList *>(object)->GetScheduledDispatchScheduleSet()); }
};

struct OrderBackupDispatchScheduleStructHandler final : public DispatchScheduleStructHandlerBase {
	void Save(void *object) const override { this->SaveSchedules(static_cast<OrderBackup *>(object)->dispatch_schedules); }

	void Load(void *object) const override { this->LoadSchedules(static_cast<OrderBackup *>(object)->dispatch_schedules); }
};

NamedSaveLoadTable GetOrderListDescription()
{
	static const NamedSaveLoad _orderlist_desc[] = {
		NSL("first",              SLE_REF(OrderList, first,                                   REF_ORDER)),
		NSL("",            SLEG_CONDVAR_X(_jokerpp_separation_mode,                           SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP))),
		NSL("",            SLE_CONDNULL_X(21,                                                             SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP))),

		NSLT_STRUCTLIST<OrderListDispatchScheduleStructHandler>("dispatch_schedule"),
	};

	return _orderlist_desc;
}

NamedSaveLoadTable GetOrderBackupDescription()
{
	static const NamedSaveLoad _order_backup_desc[] = {
		NSL("user",                            SLE_VAR(OrderBackup, user,                      SLE_UINT32)),
		NSL("tile",                            SLE_VAR(OrderBackup, tile,                      SLE_UINT32)),
		NSL("group",                           SLE_VAR(OrderBackup, group,                     SLE_UINT16)),
		NSL("service_interval",            SLE_CONDVAR(OrderBackup, service_interval,          SLE_FILE_U32 | SLE_VAR_U16,  SL_MIN_VERSION, SLV_192)),
		NSL("service_interval",            SLE_CONDVAR(OrderBackup, service_interval,          SLE_UINT16,                  SLV_192, SL_MAX_VERSION)),
		NSL("name",                            SLE_STR(OrderBackup, name,                      SLE_STR, 0)),
		NSL("",                           SLE_CONDNULL(2,                                                                   SL_MIN_VERSION, SLV_192)), // clone (2 bytes of pointer, i.e. garbage)
		NSL("clone",                       SLE_CONDREF(OrderBackup, clone,                     REF_VEHICLE,                 SLV_192, SL_MAX_VERSION)),
		NSL("cur_real_order_index",            SLE_VAR(OrderBackup, cur_real_order_index,      SLE_VEHORDERID)),
		NSL("cur_implicit_order_index",    SLE_CONDVAR(OrderBackup, cur_implicit_order_index,  SLE_VEHORDERID,              SLV_176, SL_MAX_VERSION)),
		NSL("cur_timetable_order_index", SLE_CONDVAR_X(OrderBackup, cur_timetable_order_index, SLE_VEHORDERID,              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA))),
		NSL("current_order_time",          SLE_CONDVAR(OrderBackup, current_order_time,        SLE_UINT32,                  SLV_176, SL_MAX_VERSION)),
		NSL("lateness_counter",            SLE_CONDVAR(OrderBackup, lateness_counter,          SLE_INT32,                   SLV_176, SL_MAX_VERSION)),
		NSL("timetable_start",           SLE_CONDVAR_X(OrderBackup, timetable_start,           SLE_FILE_I32 | SLE_VAR_I64,  SLV_176, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLES_START_TICKS, 0, 2))),
		NSL("timetable_start",           SLE_CONDVAR_X(OrderBackup, timetable_start,           SLE_INT64,                   SLV_176, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLES_START_TICKS, 3))),
		NSL("",                         SLE_CONDNULL_X(2,                                                                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLES_START_TICKS, 2, 2))),
		NSL("vehicle_flags",               SLE_CONDVAR(OrderBackup, vehicle_flags,             SLE_FILE_U8  | SLE_VAR_U32,  SLV_176, SLV_180)),
		NSL("vehicle_flags",             SLE_CONDVAR_X(OrderBackup, vehicle_flags,             SLE_FILE_U16 | SLE_VAR_U32,  SLV_180, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEHICLE_FLAGS_EXTRA, 0, 0))),
		NSL("vehicle_flags",             SLE_CONDVAR_X(OrderBackup, vehicle_flags,             SLE_UINT32,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEHICLE_FLAGS_EXTRA, 1))),
		NSL("orders",                          SLE_REF(OrderBackup, orders,                    REF_ORDER)),
		NSL("",                         SLE_CONDNULL_X(18,                                                                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SCHEDULED_DISPATCH, 2, 2))),

		NSLT_STRUCTLIST<OrderBackupDispatchScheduleStructHandler>("dispatch_schedule"),
	};

	return _order_backup_desc;
}

static void Save_ORDL()
{
	SaveLoadTableData slt = SlTableHeader(GetOrderListDescription());

	for (OrderList *list : OrderList::Iterate()) {
		SlSetArrayIndex(list->index);
		SlObjectSaveFiltered(list, slt);
	}
}

static void Load_ORDL()
{
	_jokerpp_auto_separation.clear();
	_jokerpp_non_auto_separation.clear();

	_old_scheduled_dispatch_start_full_date_fract = 0;
	_old_scheduled_dispatch_start_full_date_fract_map.clear();

	const bool is_table = SlIsTableChunk();
	SaveLoadTableData slt = SlTableHeaderOrRiff(GetOrderListDescription());

	if (is_table && SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 1, 6)) SlErrorCorrupt("XSLFI_SCHEDULED_DISPATCH versions 1 - 6 not supported in table format");

	ScheduledDispatchNonTableHelper helper;
	if (!is_table) helper.Setup();

	int index;
	while ((index = SlIterateArray()) != -1) {
		/* set num_orders to 0 so it's a valid OrderList */
		OrderList *list = new (index) OrderList(0);
		SlObjectLoadFiltered(list, slt);
		if (SlXvIsFeaturePresent(XSLFI_JOKERPP)) {
			if (_jokerpp_separation_mode == 0) {
				_jokerpp_auto_separation.push_back(list);
			} else {
				_jokerpp_non_auto_separation.push_back(list);
			}
		}
		if (SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH) && !is_table) {
			uint count = SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 3) ? SlReadUint32() : 1;
			list->GetScheduledDispatchScheduleSet().resize(count);
			for (DispatchSchedule &ds : list->GetScheduledDispatchScheduleSet()) {
				helper.LoadDispatchSchedule(ds);
			}
		}
	}

	_old_scheduled_dispatch_slots.clear();
}

void Ptrs_ORDL()
{
	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(GetOrderListDescription());

	for (OrderList *list : OrderList::Iterate()) {
		SlObjectPtrOrNullFiltered(list, slt);
	}
}

void Save_BKOR()
{
	SaveLoadTableData slt = SlTableHeader(GetOrderBackupDescription());

	/* We only save this when we're a network server
	 * as we want this information on our clients. For
	 * normal games this information isn't needed. */
	if (!_networking || !_network_server) return;

	for (OrderBackup *ob : OrderBackup::Iterate()) {
		SlSetArrayIndex(ob->index);
		SlObjectSaveFiltered(ob, slt);
	}
}

void Load_BKOR()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(GetOrderBackupDescription());

	if (SlIsTableChunk()) {
		if (SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 1, 6)) SlErrorCorrupt("XSLFI_SCHEDULED_DISPATCH versions 1 - 6 not supported in table format");
		int index;
		while ((index = SlIterateArray()) != -1) {
			/* set num_orders to 0 so it's a valid OrderList */
			OrderBackup *ob = new (index) OrderBackup();
			SlObjectLoadFiltered(ob, slt);
		}
		return;
	}

	ScheduledDispatchNonTableHelper helper;
	helper.Setup();

	int index;
	while ((index = SlIterateArray()) != -1) {
		/* set num_orders to 0 so it's a valid OrderList */
		OrderBackup *ob = new (index) OrderBackup();
		SlObjectLoadFiltered(ob, slt);
		if (SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 3)) {
			uint count = SlReadUint32();
			ob->dispatch_schedules.resize(count);
			for (DispatchSchedule &ds : ob->dispatch_schedules) {
				if (SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 8)) {
					helper.LoadDispatchSchedule(ds);
				} else {
					SlObjectLoadFiltered(&ds, helper.dispatch_desc);
				}
			}
		}
	}
}

static void Ptrs_BKOR()
{
	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(GetOrderBackupDescription());

	for (OrderBackup *ob : OrderBackup::Iterate()) {
		SlObjectPtrOrNullFiltered(ob, slt);
	}
}

static const ChunkHandler order_chunk_handlers[] = {
	{ 'BKOR', Save_BKOR, Load_BKOR, Ptrs_BKOR, nullptr, CH_TABLE },
	{ 'ORDR', Save_ORDR, Load_ORDR, Ptrs_ORDR, nullptr, CH_TABLE },
	{ 'ORDL', Save_ORDL, Load_ORDL, Ptrs_ORDL, nullptr, CH_TABLE },
	{ 'ORDX', nullptr,   Load_ORDX, nullptr,   nullptr, CH_READONLY },
};

extern const ChunkHandlerTable _order_chunk_handlers(order_chunk_handlers);
