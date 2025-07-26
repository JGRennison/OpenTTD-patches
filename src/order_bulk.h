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
	InsertFail,
	SeekTo,
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

	void InsertFail()
	{
		this->OpCode(BulkOrderOp::InsertFail);
	}

	void SeekTo(VehicleOrderID order_id)
	{
		this->OpCode(BulkOrderOp::SeekTo);
		this->serialiser.Send_generic(order_id);
	}
};


#endif /* ORDER_BULK_H */
