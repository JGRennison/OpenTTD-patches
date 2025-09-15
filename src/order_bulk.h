/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_bulk.h Bulk order operations. */

#ifndef ORDER_BULK_H
#define ORDER_BULK_H

#include "order_base.h"
#include "timetable_cmd.h"
#include "core/serialisation.hpp"

enum class BulkOrderOp {
	ClearOrders,
	Insert,
	Modify,
	Refit,
	Timetable,
	ReplaceOnFail,
	ReplaceWithFail,
	InsertFail,
	SeekTo,
	Move,
	AdjustTravelAfterReverse,
	SetRouteOverlayColour,
	ClearSchedules,
	AppendSchedule,
	SelectSchedule,
	SetDispatchEnabled,
	RenameSchedule,
	RenameScheduleTag,
	EditScheduleRoute,
	SetScheduleMaxDelay,
	SetScheduleReuseSlots,
	AddScheduleSlot,
	AddScheduleSlotWithFlags,
};

static constexpr size_t BULK_ORDER_MAX_CMD_SIZE = 2048;

class BulkOrderOpSerialiser {
	BufferSerialisationRef serialiser;

	inline void OpCode(BulkOrderOp op)
	{
		this->serialiser.Send_uint8(to_underlying(op));
	}

public:
	BulkOrderOpSerialiser(std::vector<uint8_t> &buffer) : serialiser(buffer, BULK_ORDER_MAX_CMD_SIZE * 2) {}

	void ClearOrders()
	{
		this->OpCode(BulkOrderOp::ClearOrders);
	}

	void Insert(const Order &order)
	{
		this->OpCode(BulkOrderOp::Insert);
		this->serialiser.Send_generic(const_cast<Order &>(order).GetCmdRefTuple());
	}

	void Modify(ModifyOrderFlags mof, uint16_t data, CargoType cargo_id, std::string_view text)
	{
		this->OpCode(BulkOrderOp::Modify);
		this->serialiser.Send_generic_seq(mof, data, cargo_id, text);
	}

	void Refit(CargoType cargo)
	{
		this->OpCode(BulkOrderOp::Refit);
		this->serialiser.Send_generic(cargo);
	}

	void Timetable(ModifyTimetableFlags mtf, uint32_t data, ModifyTimetableCtrlFlags ctrl_flags)
	{
		this->OpCode(BulkOrderOp::Timetable);
		this->serialiser.Send_generic_seq(mtf, data, ctrl_flags);
	}

	void ReplaceOnFail()
	{
		this->OpCode(BulkOrderOp::ReplaceOnFail);
	}

	void ReplaceWithFail()
	{
		this->OpCode(BulkOrderOp::ReplaceWithFail);
	}

	void InsertFail()
	{
		this->OpCode(BulkOrderOp::InsertFail);
	}

	void SeekTo(VehicleOrderID order_id)
	{
		this->OpCode(BulkOrderOp::SeekTo);
		this->serialiser.Send_generic(order_id);
	}

	void Move(VehicleOrderID from, VehicleOrderID to, uint16_t count)
	{
		this->OpCode(BulkOrderOp::Move);
		this->serialiser.Send_generic_seq(from, to, count);
	}

	void AdjustTravelAfterReverse(VehicleOrderID start, uint16_t count)
	{
		this->OpCode(BulkOrderOp::AdjustTravelAfterReverse);
		this->serialiser.Send_generic_seq(start, count);
	}

	void SetRouteOverlayColour(Colours colour)
	{
		this->OpCode(BulkOrderOp::SetRouteOverlayColour);
		this->serialiser.Send_generic(colour);
	}

	void ClearSchedules()
	{
		this->OpCode(BulkOrderOp::ClearSchedules);
	}

	void AppendSchedule(StateTicks start_tick, uint32_t duration)
	{
		this->OpCode(BulkOrderOp::AppendSchedule);
		this->serialiser.Send_generic_seq(start_tick, duration);
	}

	void SelectSchedule(uint schedule_id)
	{
		this->OpCode(BulkOrderOp::SelectSchedule);
		this->serialiser.Send_generic(schedule_id);
	}

	void SetDispatchEnabled(bool enabled)
	{
		this->OpCode(BulkOrderOp::SetDispatchEnabled);
		this->serialiser.Send_generic(enabled);
	}

	void RenameSchedule(std::string_view text)
	{
		this->OpCode(BulkOrderOp::RenameSchedule);
		this->serialiser.Send_generic(text);
	}

	void RenameScheduleTag(uint16_t tag_id, std::string_view text)
	{
		this->OpCode(BulkOrderOp::RenameScheduleTag);
		this->serialiser.Send_generic_seq(tag_id, text);
	}

	void EditScheduleRoute(DispatchSlotRouteID route_id, std::string_view text)
	{
		this->OpCode(BulkOrderOp::EditScheduleRoute);
		this->serialiser.Send_generic_seq(route_id, text);
	}

	void SetScheduleMaxDelay(uint32_t delay)
	{
		this->OpCode(BulkOrderOp::SetScheduleMaxDelay);
		this->serialiser.Send_generic(delay);
	}

	void SetScheduleReuseSlots(bool reuse)
	{
		this->OpCode(BulkOrderOp::SetScheduleReuseSlots);
		this->serialiser.Send_generic(reuse);
	}

	void AddScheduleSlot(uint32_t offset)
	{
		this->OpCode(BulkOrderOp::AddScheduleSlot);
		this->serialiser.Send_generic(offset);
	}

	void AddScheduleSlotWithFlags(uint32_t offset, uint16_t flags)
	{
		this->OpCode(BulkOrderOp::AddScheduleSlotWithFlags);
		this->serialiser.Send_generic_seq(offset, flags);
	}
};


#endif /* ORDER_BULK_H */
