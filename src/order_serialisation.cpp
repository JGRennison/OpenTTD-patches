/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_serialisation.cpp Handling of order serialisation and deserialisation to/from JSON. */

#include "stdafx.h"
#include "command_func.h"
#include "debug.h"
#include "error.h"
#include "group.h"
#include "order_base.h"
#include "order_bulk.h"
#include "order_cmd.h"
#include "order_enums_to_json.h"
#include "order_serialisation.h"
#include "rev.h"
#include "schdispatch.h"
#include "station_base.h"
#include "string_func_extra.h"
#include "strings_func.h"
#include "timetable_cmd.h"
#include "vehicle_base.h"
#include "core/format.hpp"
#include "core/serialisation.hpp"
#include "3rdparty/fmt/std.h"
#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/robin_hood/robin_hood.h"

#include "table/strings.h"

#include <type_traits>

#include "safeguards.h"

static constexpr uint8_t ORDERLIST_JSON_OUTPUT_VERSION = 1;

struct OrderSerialisationFieldNames {
	static constexpr char VERSION[]              = "version";              ///< int
	static constexpr char SOURCE[]               = "source";               ///< string      OTTD version that generated the list
	static constexpr char VEHICLE_TYPE[]         = "vehicle-type";         ///< enum
	static constexpr char VEHICLE_GROUP_NAME[]   = "vehicle-group-name";   ///< string      Export-only, name of group of first vehicle in the orderlist
	static constexpr char ROUTE_OVERLAY_COLOUR[] = "route-overlay-colour"; ///< enum

	struct GameProperties {
		static constexpr char OBJKEY[]                = "game-properties";

		static constexpr char DEFAULT_STOP_LOCATION[] = "default-stop-location"; ///< enum
		static constexpr char NEW_NONSTOP[]           = "new-nonstop";           ///< bool
		static constexpr char TICKS_PER_MINUTE[]      = "ticks-per-minute";      ///< int     Incompatible with 'ticks_per_day'
		static constexpr char TICKS_PER_DAY[]         = "ticks-per-day";         ///< int     Incompatible with 'ticks_per_minute'
	};

	struct Schedules {
		static constexpr char OBJKEY[] = "schedules";

		/** <array<int|object>, when item is an int the value rapresents the offset, when it's an object the offset can be found in the apropriate field */
		struct Slots {
			static constexpr char OBJKEY[]      = "slots";

			static constexpr char OFFSET[]      = "offset";      ///< int     Required
			static constexpr char TAGS[]        = "tags";        ///< array<int>
			static constexpr char RE_USE_SLOT[] = "re-use-slot"; ///< bool
		};

		static constexpr char DURATION[]            = "duration";            ///< int     Required
		static constexpr char NAME[]                = "name";                ///< string
		static constexpr char MAX_DELAY[]           = "max-delay";           ///< int
		static constexpr char RE_USE_ALL_SLOTS[]    = "re-use-all-slots";    ///< bool
		static constexpr char RENAMED_TAGS[]        = "renamed-tags";        ///< string
		static constexpr char RELATIVE_START_TIME[] = "relative-start-time"; ///< int Incompatible with "absolute-start-time"
		static constexpr char ABSOLUTE_START_TIME[] = "absolute-start-time"; ///< int Incompatible with "relative-start-time"
	};

	struct Orders {
		static constexpr char OBJKEY[]                      = "orders";

		static constexpr char TYPE[]                        = "type";                        ///< enum    Required
		static constexpr char DESTINATION_ID[]              = "destination-id";              ///< int     Unique in-game destination-id
		static constexpr char DESTINATION_NAME[]            = "destination-name";            ///< string  Export-only
		static constexpr char DESTINATION_LOCATION[]        = "destination-location";        ///< object  XY tile coordinate
		static constexpr char DEPOT_ID[]                    = "depot-id";                    ///< int|string  Unique in-game destination-id or "nearest"
		static constexpr char DEPOT_ACTION[]                = "depot-action";                ///< enum
		static constexpr char WAYPOINT_REVERSE[]            = "waypoint-reverse";            ///< bool
		static constexpr char COLOUR[]                      = "colour";                      ///< enum
		static constexpr char TRAVEL_TIME[]                 = "travel-time";                 ///< int
		static constexpr char MAX_SPEED[]                   = "max-speed";                   ///< int
		static constexpr char WAIT_TIME[]                   = "wait-time";                   ///< int
		static constexpr char STOPPING_PATTERN[]            = "stopping-pattern";            ///< enum
		static constexpr char STOP_LOCATION[]               = "stop-location";               ///< enum
		static constexpr char STOP_DIRECTION[]              = "stop-direction";              ///< enum
		static constexpr char WAYPOINT_ACTION[]             = "waypoint-action";
		static constexpr char LOAD[]                        = "load";                        ///< enum
		static constexpr char UNLOAD[]                      = "unload";                      ///< enum
		static constexpr char LOAD_BY_CARGO_TYPE[]          = "load-by-cargo-type";          ///< object  Contains "load" and "unload" settings for specific cargo-ids
		static constexpr char TIMETABLE_LEAVE_TYPE[]        = "timeable-leave-type";         ///< enum
		static constexpr char COUNTER_ID[]                  = "counter-id";                  ///< int
		static constexpr char SLOT_ID[]                     = "slot-id";                     ///< int
		static constexpr char SLOT_GROUP_ID[]               = "slot-group-id";               ///< int
		static constexpr char LABEL_TEXT[]                  = "label-text";                  ///< string
		static constexpr char LABEL_SUBTYPE[]               = "label-subtype";               ///< enum
		static constexpr char COUNTER_OPERATION[]           = "counter-operation";           ///< int
		static constexpr char COUNTER_VALUE[]               = "counter-value";               ///< int     Value to be applied to "counter-operation"
		static constexpr char SLOT_ACTION[]                 = "slot-action";                 ///< int
		static constexpr char JUMP_TAKEN_TRAVEL_TIME[]      = "jump-taken-travel-time";      ///< int
		static constexpr char CONDITION_VARIABLE[]          = "condition-variable";          ///< enum    Required when "order-type" is OT_CONDITIONAL
		static constexpr char CONDITION_COMPARATOR[]        = "condition-comparator";        ///< enum
		static constexpr char JUMP_TO[]                     = "jump-to";                     ///< string  Jump-label to jump to
		static constexpr char JUMP_FROM[]                   = "jump-from";                   ///< string  Jump-label to jump from
		static constexpr char CONDITION_STATION[]           = "condition-station";           ///< int     Destination-id for a station used as a data source for a conditional order
		static constexpr char CONDITION_DISPATCH_SCHEDULE[] = "condition-dispatch-schedule"; ///< int
		static constexpr char CONDITION_SLOT_SOURCE[]       = "condition-slot-source";       ///< enum    Source of the slot that needs to be checked
		static constexpr char CONDITION_CHECK_SLOT[]        = "condition-check-slot";        ///< string  Incompatible with "condition-check-tag"     "first" or "last"
		static constexpr char CONDITION_CHECK_TAG[]         = "condition-check-tag";         ///< int     Incompatible with "condition-check-slot"    scheduled dispatch tag number [1-4]
		static constexpr char CONDITION_VALUE1[]            = "condition-value1";            ///< int     Raw data
		static constexpr char CONDITION_VALUE2[]            = "condition-value2";            ///< int     Raw data
		static constexpr char CONDITION_VALUE3[]            = "condition-value3";            ///< int     Raw data
		static constexpr char CONDITION_VALUE4[]            = "condition-value4";            ///< int     Raw data
		static constexpr char REFIT_CARGO[]                 = "refit-cargo";                 ///< int
		static constexpr char SCHEDULE_INDEX[]              = "schedule-index";              ///< int
	};
};

static nlohmann::ordered_json OrderToJSON(const Order &o, VehicleType vt)
{
	using OFName = OrderSerialisationFieldNames::Orders;

	nlohmann::ordered_json json;

	json[OFName::TYPE] = o.GetType();

	if (o.IsType(OT_GOTO_WAYPOINT) || o.IsType(OT_GOTO_STATION) || (o.IsType(OT_LABEL) && IsDestinationOrderLabelSubType(o.GetLabelSubType()))) {
		json[OFName::DESTINATION_ID] = o.GetDestination().ToStationID().base();

		const BaseStation *station = BaseStation::GetIfValid(o.GetDestination().ToStationID());
		if (station != nullptr) {
			json[OFName::DESTINATION_NAME] = station->GetCachedName();
			json[OFName::DESTINATION_LOCATION]["X"] = TileX(station->xy);
			json[OFName::DESTINATION_LOCATION]["Y"] = TileY(station->xy);
		}
	} else if (o.IsType(OT_GOTO_DEPOT)) {
		if (o.GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
			json[OFName::DEPOT_ID] = "nearest";
		} else {
			json[OFName::DEPOT_ID] = o.GetDestination().ToDepotID().base();
		}

		if (o.GetDepotActionType() & ODATFB_SELL) {
			json[OFName::DEPOT_ACTION] = DA_SELL;
		} else if (o.GetDepotActionType() & ODATFB_UNBUNCH) {
			json[OFName::DEPOT_ACTION] = DA_UNBUNCH;
		} else if (o.GetDepotActionType() & ODATFB_HALT) {
			json[OFName::DEPOT_ACTION] = DA_STOP;
		} else if (o.GetDepotActionType() & ODATF_SERVICE_ONLY) {
			json[OFName::DEPOT_ACTION] = DA_SERVICE;
		}
	}

	if (o.GetColour() != INVALID_COLOUR) {
		json[OFName::COLOUR] = o.GetColour();
	}

	if (o.IsGotoOrder() || o.GetType() == OT_CONDITIONAL) {
		if (o.IsTravelTimetabled()) {
			json[OFName::TRAVEL_TIME] = o.GetTravelTime();
		}

		if (o.GetMaxSpeed() != UINT16_MAX) {
			json[OFName::MAX_SPEED] = o.GetMaxSpeed();
		}
	}

	if (o.IsGotoOrder()) {
		if (o.IsWaitTimetabled()) {
			json[OFName::WAIT_TIME] = o.GetWaitTime();
		}

		if (vt == VEH_ROAD || vt == VEH_TRAIN) {
			OrderNonStopFlags default_non_stop_flags;
			bool is_default_nonstop = _settings_client.gui.new_nonstop || _settings_game.order.nonstop_only;
			if (o.IsType(OT_GOTO_WAYPOINT)) {
				default_non_stop_flags = is_default_nonstop ? ONSF_NO_STOP_AT_ANY_STATION : ONSF_NO_STOP_AT_DESTINATION_STATION;
			} else {
				default_non_stop_flags = is_default_nonstop ? ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS : ONSF_STOP_EVERYWHERE;
			}

			if (o.GetNonStopType() != default_non_stop_flags) {
				json[OFName::STOPPING_PATTERN] = o.GetNonStopType();
			}
		}
	}

	if (o.IsType(OT_GOTO_STATION)) {
		if (o.GetLoadType() != OLFB_CARGO_TYPE_LOAD && o.GetLoadType() != OLF_LOAD_IF_POSSIBLE) {
			json[OFName::LOAD] = o.GetLoadType();
		}

		if (o.GetUnloadType() != OUFB_CARGO_TYPE_UNLOAD && o.GetUnloadType() != OUF_UNLOAD_IF_POSSIBLE) {
			json[OFName::UNLOAD] = o.GetUnloadType();
		}

		for (CargoType i = 0; i < NUM_CARGO; i++) {
			if (o.GetLoadType() == OLFB_CARGO_TYPE_LOAD && o.GetCargoLoadType(i) != OLF_LOAD_IF_POSSIBLE) {
				json[OFName::LOAD_BY_CARGO_TYPE][std::to_string(i)][OFName::LOAD] = o.GetCargoLoadType(i);
			}

			if (o.GetUnloadType() == OUFB_CARGO_TYPE_UNLOAD && o.GetCargoUnloadType(i) != OUF_UNLOAD_IF_POSSIBLE) {
				json[OFName::LOAD_BY_CARGO_TYPE][std::to_string(i)][OFName::UNLOAD] = o.GetCargoUnloadType(i);
			}
		}

		if (vt == VEH_TRAIN && o.GetStopLocation() != _settings_client.gui.stop_location) {
			json[OFName::STOP_LOCATION] = o.GetStopLocation();
		} else if (vt == VEH_ROAD && o.GetRoadVehTravelDirection() != INVALID_DIAGDIR) {
			json[OFName::STOP_DIRECTION] = o.GetRoadVehTravelDirection();
		}

		if (o.GetLeaveType() != OLT_NORMAL) {
			json[OFName::TIMETABLE_LEAVE_TYPE] = o.GetLeaveType();
		}
	}

	if (o.IsType(OT_GOTO_WAYPOINT)) {
		if (o.GetWaypointFlags().Test(OrderWaypointFlag::Reverse)) json["waypoint-reverse"] = true;
	}

	if (o.IsSlotCounterOrder()) {
		DestinationID::BaseType id = o.GetDestination().ToSlotID().base();
		switch (o.GetType()) {
			case OT_COUNTER: json[OFName::COUNTER_ID] = id; break;
			case OT_SLOT: json[OFName::SLOT_ID] = id; break;
			case OT_SLOT_GROUP: json[OFName::SLOT_GROUP_ID] = id; break;
			default: break;
		}
	}

	if (o.IsType(OT_LABEL)) {
		if (o.GetLabelSubType() == OLST_TEXT) {
			json[OFName::LABEL_TEXT] = o.GetLabelText();
		} else {
			json[OFName::LABEL_SUBTYPE] = o.GetLabelSubType();
		}
	}
	if (o.IsType(OT_COUNTER)) {
		json[OFName::COUNTER_OPERATION] = o.GetCounterOperation();
		json[OFName::COUNTER_VALUE] = o.GetXData();
	}

	if (o.IsType(OT_SLOT)) {
		json[OFName::SLOT_ACTION] = o.GetSlotSubType();
	}

	if (o.IsType(OT_CONDITIONAL)) {
		if (o.IsWaitTimetabled()) {
			json[OFName::JUMP_TAKEN_TRAVEL_TIME] = o.GetWaitTime();
		}

		json[OFName::CONDITION_VARIABLE] = o.GetConditionVariable();

		if (o.GetConditionVariable() != OCV_UNCONDITIONALLY) {
			json[OFName::CONDITION_COMPARATOR] = o.GetConditionComparator();
		}

		json[OFName::JUMP_TO] = o.GetConditionSkipToOrder(); // NB: this gets overwritten later by the labeling system.

		if (ConditionVariableHasStationID(o.GetConditionVariable())) {
			json[OFName::CONDITION_STATION] = o.GetConditionStationID().base();
		}

		switch (o.GetConditionVariable()) {
			case OCV_UNCONDITIONALLY:
				break;

			case OCV_DISPATCH_SLOT: {
				json[OFName::CONDITION_DISPATCH_SCHEDULE] = o.GetConditionDispatchScheduleID();

				const uint16_t value = o.GetConditionValue();

				json[OFName::CONDITION_SLOT_SOURCE] = (OrderDispatchConditionSources)GB(o.GetConditionValue(), ODCB_SRC_START, ODCB_SRC_COUNT);

				switch ((OrderDispatchConditionModes)GB(value, ODCB_MODE_START, ODCB_MODE_COUNT)) {
					case ODCM_FIRST_LAST:
						json[OFName::CONDITION_CHECK_SLOT] = HasBit(value, ODFLCB_LAST_SLOT) ? "first" : "last";
						break;

					case OCDM_TAG:
						json[OFName::CONDITION_CHECK_TAG] = GB(value, ODFLCB_TAG_START, ODFLCB_TAG_COUNT) + 1;
						break;
				}

				break;
			}

			case OCV_SLOT_OCCUPANCY:
			case OCV_CARGO_LOAD_PERCENTAGE:
			case OCV_TIME_DATE:
			case OCV_TIMETABLE:
			case OCV_VEH_IN_SLOT_GROUP:
			case OCV_VEH_IN_SLOT:
				json[OFName::CONDITION_VALUE1] = o.GetXData();
				break;

			case OCV_COUNTER_VALUE:
			case OCV_CARGO_WAITING_AMOUNT:
			case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
				json[OFName::CONDITION_VALUE1] = o.GetXDataLow();
				break;

			default:
				json[OFName::CONDITION_VALUE1] = o.GetConditionValue();
				break;
		}

		switch (o.GetConditionVariable()) {
			case OCV_COUNTER_VALUE:
				json[OFName::CONDITION_VALUE2] = o.GetXDataHigh();
				break;

			case OCV_CARGO_LOAD_PERCENTAGE:
			case OCV_CARGO_WAITING_AMOUNT:
			case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
			case OCV_TIME_DATE:
			case OCV_TIMETABLE:
				json[OFName::CONDITION_VALUE2] = o.GetConditionValue();
				break;

			default:
				break;
		}

		if (ConditionVariableTestsCargoWaitingAmount(o.GetConditionVariable()) && o.HasConditionViaStation()) {
			json[OFName::CONDITION_VALUE3] = o.GetConditionViaStationID().base();
		}

		if (o.GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT_PERCENTAGE) {
			json[OFName::CONDITION_VALUE4] = GB(o.GetXData2(), 16, 1);
		}
	}

	if (o.IsRefit()) {
		json[OFName::REFIT_CARGO] = o.GetRefitCargo();
	}

	if (o.IsScheduledDispatchOrder(false)) {
		json[OFName::SCHEDULE_INDEX] = o.GetDispatchScheduleIndex();
	}

	return json;
}

static nlohmann::ordered_json DispatchScheduleToJSON(const DispatchSchedule &sd)
{
	using SFName = OrderSerialisationFieldNames::Schedules;

	nlohmann::ordered_json json;

	for (uint i = 0; i < DispatchSchedule::DEPARTURE_TAG_COUNT; i++) {
		std::string_view rename = sd.GetSupplementaryName(SDSNT_DEPARTURE_TAG, i);
		if (!rename.empty()) {
			json[SFName::RENAMED_TAGS][std::to_string(i + 1)] = rename;
		}
	}

	/* Normalise the start tick where possible */
	const StateTicks start_tick = sd.GetScheduledDispatchStartTick();
	if (_settings_time.time_in_minutes) {
		const uint32_t ticks_per_day = _settings_time.ticks_per_minute * 60 * 24;
		const uint32_t duration = sd.GetScheduledDispatchDuration();

		uint32_t start_offset = 0;
		if (duration <= ticks_per_day && (ticks_per_day % duration) == 0) {
			/* Schedule fits an integer number of times into a timetable day */
			const StateTicks base = _settings_time.FromTickMinutes(_settings_time.NowInTickMinutes().ToSameDayClockTime(0, 0));
			start_offset = WrapTickToScheduledDispatchRange(base, duration, start_tick);
		} else if (duration > ticks_per_day && (duration % ticks_per_day) == 0) {
			/* Schedule is an integer number of timetable days */
			start_offset = WrapTickToScheduledDispatchRange(StateTicks{0}, duration, start_tick);
		} else {
			/* Cannot normalize, use absolute start time */
			json[SFName::ABSOLUTE_START_TIME] = start_tick.base();
		}

		if (start_offset != 0) {
			json[SFName::RELATIVE_START_TIME] = start_offset;
		}
	} else {
		json[SFName::ABSOLUTE_START_TIME] = start_tick.base();
	}

	nlohmann::ordered_json &slots_array = json[SFName::Slots::OBJKEY];
	nlohmann::ordered_json slot_object{};
	for (const auto &sd_slot : sd.GetScheduledDispatch()) {
		if (HasBit(sd_slot.flags, DispatchSlot::SDSF_REUSE_SLOT)) {
			slot_object[SFName::Slots::RE_USE_SLOT] = true;
		}

		for (uint i = 0; i <= (DispatchSlot::SDSF_LAST_TAG - DispatchSlot::SDSF_FIRST_TAG); i++) {
			if (HasBit(sd_slot.flags, DispatchSlot::SDSF_FIRST_TAG + i)) {
				slot_object[SFName::Slots::TAGS].push_back(i + 1);
			}
		}

		if (slot_object.is_object()) {
			slot_object[SFName::Slots::OFFSET] = sd_slot.offset;
			slots_array.push_back(std::move(slot_object));
			slot_object = {};
		} else {
			slots_array.push_back(sd_slot.offset);
		}
	}

	if (!sd.ScheduleName().empty()) {
		json[SFName::NAME] = sd.ScheduleName();
	}

	json[SFName::DURATION] = sd.GetScheduledDispatchDuration();

	if (sd.GetScheduledDispatchDelay() != 0) {
		json[SFName::MAX_DELAY] = sd.GetScheduledDispatchDelay();
	}

	if (sd.GetScheduledDispatchReuseSlots()) {
		json[SFName::RE_USE_ALL_SLOTS] = true;
	}

	return json;
}

std::string OrderListToJSONString(const OrderList *ol)
{
	using FName = OrderSerialisationFieldNames;

	nlohmann::ordered_json json;

	json[FName::VERSION] = ORDERLIST_JSON_OUTPUT_VERSION;
	json[FName::SOURCE] = std::string(_openttd_revision);

	if (ol == nullptr) { // order list not initialised, return an empty result
		json["error"] = "Orderlist was not initialised";
		return json;
	};

	const Vehicle *veh = ol->GetFirstSharedVehicle();
	VehicleType vt = veh->type;
	const Group *group = Group::GetIfValid(veh->group_id);

	json[FName::VEHICLE_TYPE] = vt;
	if (group != nullptr && !group->name.empty()) {
		json[FName::VEHICLE_GROUP_NAME] = group->name;
	}

	if (ol->GetRouteOverlayColour() != COLOUR_WHITE) {
		json[FName::ROUTE_OVERLAY_COLOUR] = ol->GetRouteOverlayColour();
	}

	auto &game_properties = json[FName::GameProperties::OBJKEY];

	game_properties[FName::GameProperties::DEFAULT_STOP_LOCATION] = (OrderStopLocation)_settings_client.gui.stop_location;
	game_properties[FName::GameProperties::NEW_NONSTOP] = _settings_client.gui.new_nonstop;

	if (_settings_time.time_in_minutes) {
		game_properties[FName::GameProperties::TICKS_PER_MINUTE] = _settings_time.ticks_per_minute;
	} else {
		game_properties[FName::GameProperties::TICKS_PER_DAY] = TicksPerCalendarDay();
	}

	const auto &sd_data = ol->GetScheduledDispatchScheduleSet();

	if (sd_data.size() != 0) {
		auto schedules = nlohmann::ordered_json::array();
		for (const auto &sd : sd_data) {
			schedules.push_back(DispatchScheduleToJSON(sd));
		}
		json[FName::Schedules::OBJKEY] = std::move(schedules);
	}

	auto orders = nlohmann::ordered_json::array();
	for (const Order *o : ol->Orders()) {
		orders.push_back(OrderToJSON(*o, vt));
	}

	/* Tagging system for jumps */
	std::string tag = fmt::format("{:04X}-", InteractiveRandomRange(0xFFFF));

	for (auto &val : orders) {
		if (auto jt = val.find(FName::Orders::JUMP_TO); jt != val.end()) {
			auto &target = orders[(VehicleOrderID)*jt];
			std::string label;
			if (auto jf = target.find(FName::Orders::JUMP_FROM); jf != target.end()) {
				label = (std::string)*jf;
			} else {
				label = (std::string)tag + std::to_string((VehicleOrderID)*jt);
				target[FName::Orders::JUMP_FROM] = label;
			}

			*jt = std::move(label);
		}
	}

	json[FName::Orders::OBJKEY] = std::move(orders);

	return json.dump(4);
}

Colours OrderErrorTypeToColour(JsonOrderImportErrorType error_type)
{
	switch (error_type) {
		case JOIET_CRITICAL: return COLOUR_RED;
		case JOIET_MAJOR: return COLOUR_ORANGE;
		case JOIET_MINOR: return COLOUR_CREAM;
		default: NOT_REACHED();
	}
}

struct JSONImportSettings {
	OrderStopLocation stop_location;
	bool new_nonstop;

	JSONImportSettings() : stop_location((OrderStopLocation)_settings_client.gui.stop_location), new_nonstop(_settings_client.gui.new_nonstop) {}
};

struct JSONBulkOrderCommandBuffer {
	TileIndex tile;
	BulkOrderCmdData cmd_data;
	BulkOrderOpSerialiser op_serialiser;
	std::vector<uint8_t> next_buffer;
	size_t cut_pos = 0;
	uint dispatch_schedule_select = UINT32_MAX;

	JSONBulkOrderCommandBuffer(const Vehicle *v) : tile(v->tile), op_serialiser(this->cmd_data.cmds)
	{
		cmd_data.veh = v->index;
	}

	JSONBulkOrderCommandBuffer(const JSONBulkOrderCommandBuffer &) = delete;
	JSONBulkOrderCommandBuffer(JSONBulkOrderCommandBuffer &&) = delete;
	JSONBulkOrderCommandBuffer &operator=(const JSONBulkOrderCommandBuffer &) = delete;
	JSONBulkOrderCommandBuffer &operator=(JSONBulkOrderCommandBuffer &&) = delete;

private:
	void SendCmd()
	{
		if (!this->cmd_data.cmds.empty()) {
			EnqueueDoCommandP<CMD_BULK_ORDER>(this->tile, this->cmd_data, (StringID)0);
			this->cmd_data.cmds.clear();
		}
	}

	void CheckMaxSize()
	{
		if (this->cmd_data.cmds.size() >= BULK_ORDER_MAX_CMD_SIZE) {
			this->next_buffer.clear();
			if (this->dispatch_schedule_select != UINT32_MAX) {
				BulkOrderOpSerialiser next_serialiser(this->next_buffer);
				next_serialiser.SelectSchedule(this->dispatch_schedule_select);
			}
			this->next_buffer.insert(this->next_buffer.end(), this->cmd_data.cmds.begin() + this->cut_pos, this->cmd_data.cmds.end());
			this->cmd_data.cmds.resize(this->cut_pos);
			this->SendCmd();
			this->cmd_data.cmds.swap(this->next_buffer);
			this->next_buffer.clear();
		}
		this->cut_pos = this->cmd_data.cmds.size();
	}

public:
	inline void StartOrder()
	{
		this->CheckMaxSize();
	}

	inline void PostDispatchCmd()
	{
		this->CheckMaxSize();
	}

	inline void SetDispatchScheduleId(uint32_t schedule_id)
	{
		this->CheckMaxSize();
		this->dispatch_schedule_select = schedule_id;
	}

	inline void DispatchSchedulesDone()
	{
		this->CheckMaxSize();
		this->dispatch_schedule_select = UINT32_MAX;
	}

	void Flush()
	{
		this->CheckMaxSize();
		this->SendCmd();
	}
};

enum class JSONToVehicleMode {
	Global,
	Order,
	Dispatch,
};

template <JSONToVehicleMode Tmode> struct JSONToVehicleModeTraits;

template <> struct JSONToVehicleModeTraits<JSONToVehicleMode::Global> {
	using LoggingIDType = std::monostate;
};

template <> struct JSONToVehicleModeTraits<JSONToVehicleMode::Order> {
	using LoggingIDType = VehicleOrderID;
};

template <> struct JSONToVehicleModeTraits<JSONToVehicleMode::Dispatch> {
	using LoggingIDType = uint;
};

template <JSONToVehicleMode TMode = JSONToVehicleMode::Global>
class JSONToVehicleCommandParser {
public:
	using LoggingID = typename JSONToVehicleModeTraits<TMode>::LoggingIDType;

	const JSONImportSettings &import_settings;
	JSONBulkOrderCommandBuffer &cmd_buffer;

private:
	const Vehicle *veh;
	const nlohmann::json &json;
	const LoggingID logging_index;

	OrderImportErrors &errors;

	template <typename T, typename F>
	bool ParserFuncWrapper(std::string_view field, std::optional<T> default_val, JsonOrderImportErrorType error_type, F exec)
	{
		static_assert(std::is_same_v<T, std::string> || std::is_convertible_v<T, int> || std::is_base_of_v<PoolIDBase, T>, "data is either a string or it's convertible to int");

		T val;
		bool default_used = false;
		if (!this->TryGetField<T>(field, val, error_type)) {
			if (default_val) {
				default_used = true;
				val = *default_val;
			} else {
				return false;
			}
		}

		bool success = exec(val);

		/*
		 * NB: If a default value is used and 'exec' fails, this is intentional.
		 * The default is provided as a fallback, but it is not guaranteed to be valid in the current context.
		 * Validation is delegated entirely to the 'exec' function, and therefore the command system.
		 * If the command system determines the value is invalid,
		 * it simply skips applying it — no error is logged in this case.
		 */
		if (default_used) return true;

		if (!success) {
			this->LogError(fmt::format("Value for '{}' is invalid", field), error_type);
		}

		return success;
	}

public:
	JSONToVehicleCommandParser(const Vehicle *veh, const nlohmann::json &json, JSONBulkOrderCommandBuffer &cmd_buffer, OrderImportErrors &errors, const JSONImportSettings &import_settings)
			: import_settings(import_settings), cmd_buffer(cmd_buffer), veh(veh), json(json), logging_index({}), errors(errors)
	{
		static_assert(TMode == JSONToVehicleMode::Global);
	}

	JSONToVehicleCommandParser(const Vehicle *veh, const nlohmann::json &json, JSONBulkOrderCommandBuffer &cmd_buffer, OrderImportErrors &errors, const JSONImportSettings &import_settings, LoggingID logging_index)
			: import_settings(import_settings), cmd_buffer(cmd_buffer), veh(veh), json(json), logging_index(logging_index), errors(errors) {}

	const Vehicle *GetVehicle() const { return this->veh; }
	const nlohmann::json &GetJson() const { return this->json; }

	void LogGlobalError(std::string error, JsonOrderImportErrorType error_type)
	{
		if (error_type == JOIET_OK) return;

		Debug(misc, 1, "Order import error: {}, type: {}, global", error, error_type);
		this->errors.global.push_back({ error, error_type });
	}

	void LogError(std::string error, JsonOrderImportErrorType error_type)
	{
		if (error_type == JOIET_OK) return;

		if constexpr (TMode == JSONToVehicleMode::Global) {
			this->LogGlobalError(error, error_type);
		} else if constexpr (TMode == JSONToVehicleMode::Order) {
			Debug(misc, 1, "Order import error: {}, type: {}, order: {}", error, error_type, this->logging_index);
			this->errors.order[this->logging_index].push_back({ std::move(error), error_type });
		} else if constexpr (TMode == JSONToVehicleMode::Dispatch) {
			Debug(misc, 1, "Order import error: {}, type: {}, dispatch_slot: {}", error, error_type, this->logging_index);
			this->errors.schedule[this->logging_index].push_back({ std::move(error), error_type });
		}
	}

	template <typename T>
	std::optional<T> TryGetFromValue(std::string_view label, const nlohmann::json &value, JsonOrderImportErrorType fail_type)
	{
		try {
			if constexpr (std::is_same_v<T, std::string_view>) {
				const std::string &ref = value.template get_ref<const std::string &>();
				return ref;
			} else {
				T temp = (T)value;

				/* Special case for enums, here we can also check if the value is valid. */
				if constexpr (std::is_enum<T>::value) {
					const char *result = nullptr;
					to_json(result, temp);
					if (result == nullptr) {
						this->LogError(fmt::format("Value of '{}' is invalid", label), fail_type);
						return std::nullopt;
					}
				}

				return std::move(temp);
			}
		} catch (...) {
			this->LogError(fmt::format("Data type of '{}' is invalid", label), fail_type);
			return std::nullopt;
		}
	}

	template <typename T>
	std::optional<T> TryGetField(std::string_view key, JsonOrderImportErrorType fail_type)
	{
		auto iter = this->json.find(key);
		if (iter != this->json.end()) {
			return this->TryGetFromValue<T>(key, *iter, fail_type);
		} else if (fail_type == JOIET_CRITICAL) {
			this->LogError(fmt::format("Required '{}' is missing", key), fail_type);
		}
		return std::nullopt;
	}

	template <typename T>
	bool TryGetField(std::string_view key, T &value, JsonOrderImportErrorType fail_type)
	{
		if (auto result = this->TryGetField<T>(key, fail_type)) {
			value = *result;
			return true;
		}
		return false;
	}

	template <typename T = uint16_t>
	bool TryApplyTimetableCommand(std::string_view field, ModifyTimetableFlags mtf,
			JsonOrderImportErrorType error_type, VehicleOrderID oid = INVALID_VEH_ORDER_ID)
	requires (TMode == JSONToVehicleMode::Order)
	{
		static_assert(std::is_convertible_v<T, int>, "Timetable operations only take numerical values");

		return this->ParserFuncWrapper<T>(field, std::nullopt, error_type,
			[&](T val) {
				if (oid != INVALID_VEH_ORDER_ID) this->cmd_buffer.op_serialiser.SeekTo(oid);
				this->cmd_buffer.op_serialiser.Timetable(mtf, val, MTCF_NONE);
				return true;
			}
		);
	}

	void ModifyOrder(ModifyOrderFlags mof, uint16_t val, CargoType cargo = INVALID_CARGO, std::string text = {}, VehicleOrderID oid = INVALID_VEH_ORDER_ID)
	requires (TMode == JSONToVehicleMode::Order)
	{
		if (oid != INVALID_VEH_ORDER_ID) this->cmd_buffer.op_serialiser.SeekTo(oid);
		this->cmd_buffer.op_serialiser.Modify(mof, val, cargo, std::move(text));
	}

	template <typename T>
	bool TryApplyModifyOrder(std::string_view field, ModifyOrderFlags mof, JsonOrderImportErrorType error_type, std::optional<T> default_val = std::nullopt, CargoType cargo = INVALID_CARGO, VehicleOrderID oid = INVALID_VEH_ORDER_ID)
	requires (TMode == JSONToVehicleMode::Order)
	{
		return this->ParserFuncWrapper<T>(field, default_val, error_type,
			[&](T val) {
				if constexpr (std::is_same_v<std::string, T>) {
					this->ModifyOrder(mof, 0, cargo, val, oid);
				} else if constexpr (std::is_base_of_v<PoolIDBase, T>) {
					this->ModifyOrder(mof, val.base(), cargo, {}, oid);
				} else {
					this->ModifyOrder(mof, val, cargo, {}, oid);
				}
				if (error_type == JOIET_CRITICAL) this->cmd_buffer.op_serialiser.ReplaceOnFail();
				return true;
			});
	}

	JSONToVehicleCommandParser WithNewJson(const nlohmann::json &new_json)
	{
		return JSONToVehicleCommandParser(this->veh, new_json, this->cmd_buffer, this->errors, this->import_settings, this->logging_index);
	}

	template <JSONToVehicleMode TNewMode>
	JSONToVehicleCommandParser<TNewMode> WithNewTarget(const nlohmann::json &new_json, typename JSONToVehicleModeTraits<TNewMode>::LoggingIDType logging_id)
	{
		return JSONToVehicleCommandParser<TNewMode>(this->veh, new_json, this->cmd_buffer, this->errors, this->import_settings, logging_id);
	}

	JSONToVehicleCommandParser operator[](auto val)
	{
		return this->WithNewJson(this->json[val]);
	}
};

static void ImportJsonOrder(JSONToVehicleCommandParser<JSONToVehicleMode::Order> json_importer)
{
	using OFName = OrderSerialisationFieldNames::Orders;

	const Vehicle *veh = json_importer.GetVehicle();
	const nlohmann::json &json = json_importer.GetJson();

	OrderType type;

	if (!json_importer.TryGetField(OFName::TYPE, type, JOIET_CRITICAL)) {
		json_importer.cmd_buffer.op_serialiser.InsertFail();
		return;
	}

	DestinationID destination = StationID::Invalid();
	OrderLabelSubType labelSubtype = OLST_TEXT;

	/* Get basic order data required to build order. */
	switch (type) {
		case OT_LABEL:
			json_importer.TryGetField(OFName::LABEL_SUBTYPE, labelSubtype, JOIET_MAJOR);
			if (labelSubtype == OLST_DEPARTURES_REMOVE_VIA || labelSubtype == OLST_DEPARTURES_VIA) {
				if (json_importer.TryGetField(OFName::DESTINATION_ID, destination.edit_base(), JOIET_MAJOR)) {
					destination = StationID(destination.edit_base());
				}
			}
			break;

		case OT_GOTO_STATION:
		case OT_GOTO_WAYPOINT:
		case OT_IMPLICIT:
			if (json_importer.TryGetField(OFName::DESTINATION_ID, destination.edit_base(), JOIET_MAJOR)) {
				destination = StationID(destination.edit_base());
			}
			break;

		case OT_GOTO_DEPOT:
			if (json_importer.TryGetField(OFName::DEPOT_ID, destination.edit_base(), JOIET_OK)) {
				destination = DepotID(destination.edit_base());
			} else {
				destination = DepotID::Invalid();
				if (auto it = json.find(OFName::DEPOT_ID); it != json.end()) {
					if (!it->is_string() || !(*it == "nearest")) {
						json_importer.LogError(fmt::format("Value of '{}' is invalid", OFName::DEPOT_ID) , JOIET_MAJOR);
					}
				}
			}
			break;

		default:
			break;
	}

	/* Now let's build the order. */
	Order new_order;
	switch (type) {
		case OT_GOTO_STATION:
			new_order.MakeGoToStation(StationID(destination.edit_base()));
			if (veh->type != VEH_TRAIN) {
				new_order.SetStopLocation(OSL_PLATFORM_FAR_END);
			}
			break;

		case OT_GOTO_WAYPOINT:
			new_order.MakeGoToWaypoint(StationID(destination.edit_base()));
			break;

		case OT_GOTO_DEPOT:
			new_order.MakeGoToDepot(destination, ODTFB_PART_OF_ORDERS);
			if (destination == DepotID::Invalid()) {
				new_order.SetDepotActionType(ODATFB_NEAREST_DEPOT);
			}
			break;

		case OT_IMPLICIT:
			new_order.MakeImplicit(StationID(destination.edit_base()));
			break;

		case OT_LABEL:
			new_order.MakeLabel(labelSubtype);
			if (new_order.GetLabelSubType() != OLST_TEXT) {
				new_order.SetDestination(destination);
			}
			break;

		case OT_CONDITIONAL:
			new_order.MakeConditional(0);
			break;

		case OT_SLOT:
			OrderSlotSubType osst;
			if (!json_importer.TryGetField(OFName::SLOT_ACTION, osst, JOIET_CRITICAL)) {
				return;
			}
			switch (osst) {
				case OSST_TRY_ACQUIRE: new_order.MakeTryAcquireSlot(); break;
				case OSST_RELEASE: new_order.MakeReleaseSlot(); break;
				default: break;
			}
			break;

		case OT_SLOT_GROUP:
			new_order.MakeReleaseSlotGroup();
			break;

		case OT_COUNTER:
			new_order.MakeChangeCounter();
			break;

		default:
			break;
	}

	if (!veh->IsGroundVehicle()) {
		new_order.SetNonStopType(ONSF_STOP_EVERYWHERE);
	} else if (_settings_game.order.nonstop_only) {
		new_order.SetNonStopType(ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
	}

	/* Create the order */
	json_importer.cmd_buffer.op_serialiser.Insert(new_order);
	json_importer.cmd_buffer.op_serialiser.ReplaceOnFail();

	json_importer.TryApplyModifyOrder<Colours>(OFName::COLOUR, MOF_COLOUR, JOIET_MINOR);

	json_importer.TryApplyTimetableCommand(OFName::MAX_SPEED, MTF_TRAVEL_SPEED, JOIET_MINOR);
	json_importer.TryApplyTimetableCommand(OFName::WAIT_TIME, MTF_WAIT_TIME, JOIET_MINOR);
	json_importer.TryApplyTimetableCommand(OFName::TRAVEL_TIME, MTF_TRAVEL_TIME, JOIET_MINOR);
	json_importer.TryApplyTimetableCommand<OrderLeaveType>(OFName::TIMETABLE_LEAVE_TYPE, MTF_SET_LEAVE_TYPE, JOIET_MINOR);
	json_importer.TryApplyTimetableCommand(OFName::JUMP_TAKEN_TRAVEL_TIME, MTF_WAIT_TIME, JOIET_MINOR);

	json_importer.TryApplyModifyOrder<OrderStopLocation>(OFName::STOP_LOCATION, MOF_STOP_LOCATION, JOIET_MINOR, json_importer.import_settings.stop_location);
	json_importer.TryApplyModifyOrder<DiagDirection>(OFName::STOP_DIRECTION, MOF_RV_TRAVEL_DIR, JOIET_MINOR);

	OrderWaypointFlags waypoint_flags{};
	if (json_importer.TryGetField<bool>(OFName::WAYPOINT_REVERSE, JOIET_MAJOR).value_or(false)) waypoint_flags.Set(OrderWaypointFlag::Reverse);
	if (waypoint_flags.Any()) json_importer.ModifyOrder(MOF_WAYPOINT_FLAGS, waypoint_flags.base());

	if (type == OT_GOTO_DEPOT) {
		json_importer.TryApplyModifyOrder<OrderDepotAction>(OFName::DEPOT_ACTION, MOF_DEPOT_ACTION, JOIET_MAJOR, DA_ALWAYS_GO);
	}

	json_importer.TryApplyModifyOrder<std::string>(OFName::LABEL_TEXT, MOF_LABEL_TEXT, JOIET_MINOR);

	bool is_default_non_stop = json_importer.import_settings.new_nonstop || _settings_game.order.nonstop_only;
	OrderNonStopFlags default_non_stop;
	if (new_order.IsType(OT_GOTO_WAYPOINT)) {
		default_non_stop = is_default_non_stop ? ONSF_NO_STOP_AT_ANY_STATION : ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS;
	} else {
		default_non_stop = is_default_non_stop ? ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS : ONSF_STOP_EVERYWHERE;
	}
	json_importer.TryApplyModifyOrder<OrderNonStopFlags>(OFName::STOPPING_PATTERN, MOF_NON_STOP, JOIET_MAJOR, default_non_stop);

	if (type == OT_CONDITIONAL) {
		/* If we are parsing a conditional order, "condition-variable" is required. */
		auto cvresult = json_importer.TryGetField<OrderConditionVariable>(OFName::CONDITION_VARIABLE, JOIET_CRITICAL);
		if (!cvresult.has_value()) {
			json_importer.cmd_buffer.op_serialiser.ReplaceWithFail();
			return;
		}

		const OrderConditionVariable condvar = *cvresult;
		json_importer.ModifyOrder(MOF_COND_VARIABLE, condvar);
		json_importer.cmd_buffer.op_serialiser.ReplaceOnFail();

		json_importer.TryApplyModifyOrder<OrderConditionComparator>(OFName::CONDITION_COMPARATOR, MOF_COND_COMPARATOR, JOIET_MAJOR);
		json_importer.TryApplyModifyOrder<StationID>(OFName::CONDITION_STATION, MOF_COND_STATION_ID, JOIET_MAJOR);
		json_importer.TryApplyModifyOrder<uint16_t>(OFName::CONDITION_VALUE1, MOF_COND_VALUE, JOIET_MAJOR);
		json_importer.TryApplyModifyOrder<uint16_t>(OFName::CONDITION_VALUE2, MOF_COND_VALUE_2, JOIET_MAJOR);
		json_importer.TryApplyModifyOrder<uint16_t>(OFName::CONDITION_VALUE3, MOF_COND_VALUE_3, JOIET_MAJOR);
		json_importer.TryApplyModifyOrder<uint16_t>(OFName::CONDITION_VALUE4, MOF_COND_VALUE_4, JOIET_MAJOR);

		/* Non trivial cases for conditionals. */
		if (condvar == OCV_DISPATCH_SLOT) {
			uint16_t val = 0;

			auto odscs = json_importer.TryGetField<OrderDispatchConditionSources>(OFName::CONDITION_SLOT_SOURCE, JOIET_MAJOR);
			if (odscs.has_value()) {
				SB(val, ODCB_SRC_START, ODCB_SRC_COUNT, *odscs);
			}

			auto cond_dispatch_slot = json_importer.TryGetField<std::string_view>(OFName::CONDITION_CHECK_SLOT, JOIET_MAJOR);
			auto cond_dispatch_tag = json_importer.TryGetField<uint>(OFName::CONDITION_CHECK_TAG, JOIET_MAJOR);
			if (cond_dispatch_slot.has_value() == cond_dispatch_tag.has_value()) {
				json_importer.LogError(fmt::format("Either '{}' or '{}' must be defined", OFName::CONDITION_CHECK_SLOT, OFName::CONDITION_CHECK_TAG), JOIET_MAJOR);
			} else if (cond_dispatch_slot.has_value()) {
				if (cond_dispatch_slot.value() == "last") {
					SetBit(val, ODFLCB_LAST_SLOT);
				} else if (cond_dispatch_slot.value() == "first") {
					/* No bit needs to be set. */
				} else {
					json_importer.LogError(fmt::format("Invalid value in '{}'", OFName::CONDITION_CHECK_SLOT), JOIET_MAJOR);
				}
			} else if (cond_dispatch_tag.has_value()) {
				SB(val, ODCB_MODE_START, ODCB_MODE_COUNT, OCDM_TAG);
				SB(val, ODFLCB_TAG_START, ODFLCB_TAG_COUNT, cond_dispatch_tag.value() - 1);
			}

			json_importer.ModifyOrder(MOF_COND_VALUE, val);

			json_importer.TryApplyModifyOrder<uint16_t>(OFName::CONDITION_DISPATCH_SCHEDULE, MOF_COND_VALUE_2, JOIET_MAJOR);
		}
	}

	json_importer.TryApplyModifyOrder<uint16_t>(OFName::COUNTER_ID, MOF_COUNTER_ID, JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<uint16_t>(OFName::SLOT_ID, MOF_SLOT, JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<uint16_t>(OFName::SLOT_GROUP_ID, MOF_SLOT_GROUP, JOIET_MAJOR);

	json_importer.TryApplyModifyOrder<uint8_t>(OFName::COUNTER_OPERATION, MOF_COUNTER_OP, JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<uint16_t>(OFName::COUNTER_VALUE, MOF_COUNTER_VALUE, JOIET_MAJOR);

	json_importer.TryApplyModifyOrder<OrderLoadFlags>(OFName::LOAD, MOF_LOAD, JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<OrderUnloadFlags>(OFName::UNLOAD, MOF_UNLOAD, JOIET_MAJOR);

	if (auto it = json.find(OFName::LOAD_BY_CARGO_TYPE); it != json.end()) {
		if (it->is_object()) {
			for (const auto &[key, val] : it->items()) {
				auto cargo_res = IntFromChars<CargoType>((std::string_view)key);
				if (!cargo_res.has_value() || *cargo_res >= NUM_CARGO) {
					json_importer.LogError(fmt::format("in '{}','{}' is not a valid cargo_id", OFName::LOAD_BY_CARGO_TYPE, key), JOIET_MAJOR);
					continue;
				}
				CargoType cargo_id = *cargo_res;

				if (!val.is_object()) {
					json_importer.LogError(fmt::format("loading options in '{}'[{}] are not valid", OFName::LOAD_BY_CARGO_TYPE, key), JOIET_MAJOR);
					continue;
				};

				if (val.contains(OFName::LOAD)) {
					json_importer[OFName::LOAD_BY_CARGO_TYPE][key].TryApplyModifyOrder<OrderLoadFlags>(OFName::LOAD, MOF_CARGO_TYPE_LOAD, JOIET_MAJOR, std::nullopt, cargo_id);
				}

				if (val.contains(OFName::UNLOAD)) {
					json_importer[OFName::LOAD_BY_CARGO_TYPE][key].TryApplyModifyOrder<OrderUnloadFlags>(OFName::UNLOAD, MOF_CARGO_TYPE_UNLOAD, JOIET_MAJOR, std::nullopt, cargo_id);
				}
			}
		} else {
			json_importer.LogError(fmt::format("'{}' must be an object", OFName::LOAD_BY_CARGO_TYPE), JOIET_MAJOR);
		}
	}

	/* Refit works in a weird way, so it gets treated weirdly. */
	if (auto it = json.find(OFName::REFIT_CARGO); it != json.end()) {
		if (it->is_string()) {
			if (*it == "auto") {
				json_importer.cmd_buffer.op_serialiser.Refit(CARGO_AUTO_REFIT);
			} else {
				json_importer.LogError(fmt::format("Value of '{}' is invalid", OFName::REFIT_CARGO), JOIET_MAJOR);
			}
		} else {
			CargoType cargo_id;
			if (json_importer.TryGetField(OFName::REFIT_CARGO, cargo_id, JOIET_MAJOR)) {
				json_importer.cmd_buffer.op_serialiser.Refit(cargo_id);
			} else {
				json_importer.LogError(fmt::format("Value of '{}' is invalid", OFName::REFIT_CARGO), JOIET_MAJOR);
			}
		}
	}
}

/**
* Returns true if the given integer is a valid serialised (i.e. 1-indexed) tag number
*/
static bool IsValidSerialisedTagNumber(int tag_num)
{
	return tag_num >= 1 && tag_num <= 4;
}

/**
* Returns the tag index for a given serialised tag string, or -1 if it fails to parse the string
*/
static int TagStringToIndex(std::string_view tag)
{
	/* Format : ^[1-4]$ */
	auto res = IntFromChars<int>(tag);
	if (res.has_value() && IsValidSerialisedTagNumber(*res)) return *res - 1;
	return -1;
}

static void ImportJsonDispatchSchedule(JSONToVehicleCommandParser<JSONToVehicleMode::Dispatch> json_importer)
{
	using SFName = OrderSerialisationFieldNames::Schedules;

	const nlohmann::json &json = json_importer.GetJson();

	const StateTicks day_start = _settings_time.FromTickMinutes(_settings_time.NowInTickMinutes().ToSameDayClockTime(0, 0));

	auto create_error_schedule = [&]() {
		/* Create an empty error schedule to avoid disrupting schedule indices. */
		json_importer.cmd_buffer.op_serialiser.AppendSchedule(day_start, 24 * 60 * _settings_time.ticks_per_minute);
		json_importer.cmd_buffer.op_serialiser.RenameSchedule("[Parse Error]");
	};

	if (json.is_null()) {
		create_error_schedule();
		return;
	}

	uint32_t duration = 0;
	if (!json_importer.TryGetField(SFName::DURATION, duration, JOIET_CRITICAL) || duration == 0) {
		create_error_schedule();
		return;
	}

	auto relative_start_time = json_importer.TryGetField<int64_t>(SFName::RELATIVE_START_TIME, JOIET_MAJOR);
	auto absolute_start_time = json_importer.TryGetField<int64_t>(SFName::ABSOLUTE_START_TIME, JOIET_MAJOR);

	StateTicks start_tick = day_start;
	if (relative_start_time.has_value() && absolute_start_time.has_value()) {
		json_importer.LogError(
			fmt::format(
				"'{}' and '{}' are incompatible",
				SFName::RELATIVE_START_TIME,
				SFName::ABSOLUTE_START_TIME),
			JOIET_MAJOR);
	} else if (relative_start_time.has_value()) {
		if (duration <= static_cast<uint32_t>(_settings_time.ticks_per_minute) * 60 * 24) {
			start_tick = day_start + relative_start_time.value();
		} else {
			start_tick = StateTicks{relative_start_time.value()};
		}
	} else if (absolute_start_time.has_value()) {
		start_tick = StateTicks{absolute_start_time.value()};
	}
	json_importer.cmd_buffer.op_serialiser.AppendSchedule(start_tick, duration);

	if (auto result = json_importer.TryGetField<std::string_view>(SFName::NAME, JOIET_MINOR)) {
		json_importer.cmd_buffer.op_serialiser.RenameSchedule(*result);
	}
	if (auto result = json_importer.TryGetField<uint>(SFName::MAX_DELAY, JOIET_MINOR)) {
		json_importer.cmd_buffer.op_serialiser.SetScheduleMaxDelay(*result);
	}
	if (auto result = json_importer.TryGetField<bool>(SFName::RE_USE_ALL_SLOTS, JOIET_MINOR)) {
		json_importer.cmd_buffer.op_serialiser.SetScheduleReuseSlots(*result);
	}
	json_importer.cmd_buffer.PostDispatchCmd();

	if (auto it = json.find(SFName::RENAMED_TAGS); it != json.end() && it->is_object()) {
		for (const auto &names : it->items()) {
			int index = TagStringToIndex(names.key());

			if (index == -1 || !names.value().is_string()) {
				json_importer.LogError(fmt::format("'{}' is not a valid tag index.", names.key()), JOIET_MINOR);
			} else {
				if (auto result = json_importer[SFName::RENAMED_TAGS].TryGetField<std::string_view>(names.key(), JOIET_MINOR)) {
					json_importer.cmd_buffer.op_serialiser.RenameScheduleTag((uint16_t)index, *result);
					json_importer.cmd_buffer.PostDispatchCmd();
				}
			}
		}
	}

	if (auto it = json.find(SFName::Slots::OBJKEY); it != json.end()) {
		const auto &slots_json = *it;
		if (slots_json.is_array()) {
			for (const auto &slot_data : slots_json) {
				if (slot_data.is_object()) {
					auto local_importer = json_importer.WithNewJson(slot_data);

					uint32_t offset;
					if (!local_importer.TryGetField(SFName::Slots::OFFSET, offset, JOIET_MAJOR)) {
						continue;
					}

					bool re_use_slot = false;
					local_importer.TryGetField(SFName::Slots::RE_USE_SLOT, re_use_slot, JOIET_MAJOR);

					uint16_t flags = 0;
					if (re_use_slot) {
						SetBit(flags, DispatchSlot::SDSF_REUSE_SLOT);
					}

					if (auto it = slot_data.find(SFName::Slots::TAGS); it != slot_data.end() && it->is_array()) {
						for (nlohmann::json::const_reference tag_json : *it) {
							int tag = -1;
							if (tag_json.is_string()) {
								auto tag_str = local_importer.TryGetFromValue<std::string_view>(SFName::Slots::TAGS, tag_json, JOIET_MAJOR);
								if (tag_str.has_value()) tag = TagStringToIndex(*tag_str);
							} else {
								auto tag_num = local_importer.TryGetFromValue<int>(SFName::Slots::TAGS, tag_json, JOIET_MAJOR);
								if (tag_num.has_value() && IsValidSerialisedTagNumber(*tag_num)) tag = *tag_num - 1;
							}
							if (tag == -1) {
								json_importer.LogError(fmt::format("'{}' is not a valid tag index", tag_json.dump()), JOIET_MAJOR);
							} else {
								SetBit(flags, DispatchSlot::SDSF_FIRST_TAG + tag);
							}
						}
					}

					if (flags != 0) {
						json_importer.cmd_buffer.op_serialiser.AddScheduleSlotWithFlags(offset, flags);
					} else {
						json_importer.cmd_buffer.op_serialiser.AddScheduleSlot(offset);
					}
				} else {
					try {
						uint32_t offset = (uint32_t)slot_data;
						json_importer.cmd_buffer.op_serialiser.AddScheduleSlot(offset);
					} catch (...) {
						json_importer.LogError("Dispatch schedule slot key not in ticks", JOIET_MAJOR);
						continue;
					}
				}
				json_importer.cmd_buffer.PostDispatchCmd();
			}
		}
	}
}

OrderImportErrors ImportJsonOrderList(const Vehicle *veh, std::string_view json_str)
{
	using FName = OrderSerialisationFieldNames;

	assert(veh != nullptr);

	nlohmann::json json;
	OrderImportErrors errors = {};

	try {
		json = nlohmann::json::parse(json_str);
	} catch (const nlohmann::json::parse_error &) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_JSON), GetEncodedString(STR_ERROR_ORDERLIST_MALFORMED_JSON), WL_ERROR);
		return errors;
	}

	if (json.contains(FName::Orders::OBJKEY) && !json[FName::Orders::OBJKEY].is_array()) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_JSON), GetEncodedString(STR_ERROR_ORDERLIST_JSON_NEEDS_ORDERS), WL_ERROR);
		return errors;
	}

	/* Checking if the vehicle type matches */
	if (!json.contains(FName::VEHICLE_TYPE)) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_JSON), GetEncodedString(STR_ERROR_ORDERLIST_JSON_VEHICLE_TYPE_MISSING), WL_ERROR);
		return errors;
	}

	VehicleType vt;
	try {
		vt = json[FName::VEHICLE_TYPE];
	} catch (...) {
		vt = VEH_END;
	}

	if (vt != veh->type) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_JSON), GetEncodedString(STR_ERROR_ORDERLIST_JSON_VEHICLE_TYPE_DOES_NOT_MATCH), WL_ERROR);
		return errors;
	}

	JSONImportSettings import_settings_client{};

	/* If the json contains game-properties, we will try to parse them and apply them */
	if (auto it = json.find(FName::GameProperties::OBJKEY); it != json.end() && it->is_object()) {
		const auto &game_properties = *it;

		const auto makeMissingErrString = [](std::string field) -> std::string {
			return fmt::format("'{}' missing or invalid in '{}', this may cause discrepancies when loading the orderlist", field, FName::GameProperties::OBJKEY);
		};

		OrderStopLocation osl = game_properties.value<OrderStopLocation>(FName::GameProperties::DEFAULT_STOP_LOCATION, OSL_END);
		if (osl == OSL_END) {
			errors.global.push_back({
				makeMissingErrString(FName::GameProperties::DEFAULT_STOP_LOCATION),
				JOIET_MAJOR
			});
		} else {
			import_settings_client.stop_location = osl;
		}

		if (auto nnit = game_properties.find(FName::GameProperties::NEW_NONSTOP); nnit != game_properties.end() && nnit->is_boolean()) {
			bool new_nonstop = *nnit;
			if (!new_nonstop && _settings_game.order.nonstop_only) {
				errors.global.push_back({
					fmt::format("'{}' is not compatible with the current game setting, this may cause discrepancies when loading the orderlist",
							FName::GameProperties::NEW_NONSTOP),
					JOIET_MAJOR
				});
			}
			import_settings_client.new_nonstop = new_nonstop;
		} else {
			errors.global.push_back({
				makeMissingErrString(FName::GameProperties::NEW_NONSTOP),
				JOIET_MAJOR
			});
		}
	} else {
		errors.global.push_back({
			fmt::format("no valid '{}' found, current setings will be assumed to be correct", FName::GameProperties::OBJKEY),
			JOIET_MAJOR
		});
	}

	JSONBulkOrderCommandBuffer cmd_buffer(veh);
	JSONToVehicleCommandParser<> json_importer(veh, json, cmd_buffer, errors, import_settings_client);

	/* Delete all orders before setting the new orders */
	cmd_buffer.op_serialiser.ClearOrders();
	cmd_buffer.op_serialiser.ClearSchedules();

	const auto &orders_json = json[FName::Orders::OBJKEY];

	robin_hood::unordered_map<std::string, VehicleOrderID> jump_map; // Associates jump labels to actual order-ids until all orders are added

	if (auto it = json.find(FName::Schedules::OBJKEY); it != json.end()) {
		const auto &schedules = *it;

		if (!schedules.is_array()) {
			json_importer.LogGlobalError(fmt::format("'{}' must be an array", FName::Schedules::NAME), JOIET_CRITICAL);
		} else if (schedules.size() > 0) {
			bool have_schedule = false;
			for (const auto &value : orders_json) {
				if (value.contains(FName::Orders::SCHEDULE_INDEX)) {
					have_schedule = true;
					break;
				}
			}

			if (have_schedule && veh->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) {
				Command<CMD_TIMETABLE_SEPARATION>::Post(veh->index, false);
			}

			uint schedule_index = 0;
			for (const auto &value : schedules) {
				cmd_buffer.SetDispatchScheduleId(schedule_index);
				ImportJsonDispatchSchedule(json_importer.WithNewTarget<JSONToVehicleMode::Dispatch>(value, schedule_index));
				schedule_index++;
			}
			cmd_buffer.DispatchSchedulesDone();

			if (have_schedule && !veh->vehicle_flags.Test(VehicleFlag::ScheduledDispatch)) {
				cmd_buffer.op_serialiser.SetDispatchEnabled(true);
			}
		}
	}

	VehicleOrderID order_id = 0;
	for (const auto &value : orders_json) {
		auto order_importer = json_importer.WithNewTarget<JSONToVehicleMode::Order>(value, order_id);

		cmd_buffer.StartOrder();
		ImportJsonOrder(order_importer);

		std::string jump_label;
		if (order_importer.TryGetField(FName::Orders::JUMP_FROM, jump_label, JOIET_MAJOR)) {
			jump_map[jump_label] = order_id;
		}

		order_id++;
	}

	{
		Colours route_overlay_colour = COLOUR_WHITE;
		json_importer.TryGetField(FName::ROUTE_OVERLAY_COLOUR, route_overlay_colour, JOIET_MINOR);
		const Colours current = (veh->orders != nullptr) ? veh->orders->GetRouteOverlayColour() : COLOUR_WHITE;
		if (route_overlay_colour != current) {
			cmd_buffer.op_serialiser.SetRouteOverlayColour(route_overlay_colour);
		}
	}

	/* Post processing (link jumps and assign schedules) */
	order_id = 0;
	for (const auto &value : orders_json) {
		auto local_importer = json_importer.WithNewTarget<JSONToVehicleMode::Order>(value, order_id);

		cmd_buffer.StartOrder();
		local_importer.TryApplyTimetableCommand(FName::Orders::SCHEDULE_INDEX, MTF_ASSIGN_SCHEDULE, JOIET_MAJOR, order_id);

		std::string jump_label;
		if (local_importer.TryGetField(FName::Orders::JUMP_TO, jump_label, JOIET_MAJOR)) {
			auto jm_iter = jump_map.find(jump_label);
			if (jm_iter != jump_map.end()) {
				local_importer.ModifyOrder(MOF_COND_DESTINATION, jm_iter->second, INVALID_CARGO, {}, order_id);
			} else {
				local_importer.LogError(fmt::format("Unknown jump label '{}'",jump_label), JOIET_MAJOR);
			}
		}

		order_id++;
	}

	cmd_buffer.Flush();
	return errors;
}

bool OrderImportErrors::HasErrors() const
{
	return !this->global.empty() || !this->order.empty() || !this->schedule.empty();
}
