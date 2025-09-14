/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file base_consist.h Properties for front vehicles/consists. */

#ifndef BASE_CONSIST_H
#define BASE_CONSIST_H

#include "core/enum_type.hpp"
#include "order_type.h"
#include "date_type.h"
#include "timetable.h"
#include "core/tinystring_type.hpp"
#include "3rdparty/cpp-btree/btree_map.h"

struct LastDispatchRecord {
	enum RecordFlags {
		RF_FIRST_SLOT = 0,  ///< Dispatch slot was first
		RF_LAST_SLOT,       ///< Dispatch slot was last
	};

	StateTicks dispatched;
	uint32_t offset;
	uint16_t slot_flags;
	DispatchSlotRouteID route_id = 0;
	uint8_t record_flags;
};

/** Bit numbers in #Vehicle::vehicle_flags. */
enum class VehicleFlag : uint8_t {
	LoadingFinished             = 0, ///< Vehicle has finished loading.
	CargoUnloading              = 1, ///< Vehicle is unloading cargo.
	BuiltAsPrototype            = 2, ///< Vehicle is a prototype (accepted as exclusive preview).
	TimetableStarted            = 3, ///< Whether the vehicle has started running on the timetable yet.
	AutofillTimetable           = 4, ///< Whether the vehicle should fill in the timetable automatically.
	AutofillPreserveWaitTime    = 5, ///< Whether non-destructive auto-fill should preserve waiting times
	StopLoading                 = 6, ///< Don't load anymore during the next load cycle.
	PathfinderLost              = 7, ///< Vehicle's pathfinder is lost.
	ServiceIntervalIsCustom     = 8, ///< Service interval is custom.
	ServiceIntervalIsPercent    = 9, ///< Service interval is percent.
	/* gap, above are common with upstream */
	SeparationActive            = 11, ///< Whether timetable auto-separation is currently active.
	ScheduledDispatch           = 12, ///< Whether the vehicle should follow a timetabled dispatching schedule.
	LastLoadStationSeparate     = 13, ///< Each vehicle of this chain has its last_loading_station and last_loading_tick fields set separately.
	TimetableSeparation         = 14, ///< Whether timetable auto-separation is enabled.
	AutomateTimetable           = 15, ///< Whether the vehicle should manage the timetable automatically.
	HaveSlot                    = 16, ///< Vehicle has 1 or more slots.
	ConditionalOrderWait        = 17, ///< Vehicle is waiting due to conditional order loop.
	ReplacementPending          = 18, ///< Autoreplace or template replacement is pending, vehicle should visit the depot.
};
using VehicleFlags = EnumBitSet<VehicleFlag, uint32_t>;

/** Various front vehicle properties that are preserved when autoreplacing, using order-backup or switching front engines within a consist. */
struct BaseConsist {
	TinyString name{};                            ///< Name of vehicle

	btree::btree_map<uint16_t, LastDispatchRecord> dispatch_records{}; ///< Records of last scheduled dispatches

	/* Used for timetabling. */
	uint32_t current_order_time = 0;              ///< How many ticks have passed since this order started.
	int32_t lateness_counter = 0;                 ///< How many ticks late (or early if negative) this vehicle is.
	StateTicks timetable_start{};                 ///< When the vehicle is supposed to start the timetable.

	uint16_t service_interval = 0;                ///< The interval for (automatic) servicing; either in days or %.

	VehicleOrderID cur_real_order_index = 0;      ///< The index to the current real (non-implicit) order
	VehicleOrderID cur_implicit_order_index = 0;  ///< The index to the current implicit order
	VehicleOrderID cur_timetable_order_index = 0; ///< The index to the current real (non-implicit) order used for timetable updates

	VehicleFlags vehicle_flags{};                 ///< Used for gradual loading and other miscellaneous things (@see VehicleFlags enum)

	virtual ~BaseConsist() = default;

	void CopyConsistPropertiesFrom(const BaseConsist *src);
};

#endif /* BASE_CONSIST_H */
