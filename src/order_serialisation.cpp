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

#include <type_traits>

static constexpr uint8_t ORDERLIST_JSON_OUTPUT_VERSION = 0;

static nlohmann::ordered_json OrderToJSON(const Order &o, VehicleType vt)
{
	nlohmann::ordered_json json;

	json["type"] = o.GetType();

	if (o.IsType(OT_GOTO_WAYPOINT) || o.IsType(OT_GOTO_STATION) || (o.IsType(OT_LABEL) && IsDestinationOrderLabelSubType(o.GetLabelSubType()))) {
		json["destination-id"] = o.GetDestination().ToStationID().base();

		const BaseStation *station = BaseStation::GetIfValid(o.GetDestination().ToStationID());
		if (station != nullptr) {
			json["destination-name"] = station->GetCachedName();
			json["destination-location"]["X"] = TileX(station->xy);
			json["destination-location"]["Y"] = TileY(station->xy);
		}
	} else if (o.IsType(OT_GOTO_DEPOT)) {
		if (o.GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
			json["depot-id"] = "nearest";
		} else {
			json["depot-id"] = o.GetDestination().ToDepotID().base();
		}

		if (o.GetDepotActionType() & ODATFB_SELL) {
			json["depot-action"] = DA_SELL;
		} else if (o.GetDepotActionType() & ODATFB_UNBUNCH) {
			json["depot-action"] = DA_UNBUNCH;
		} else if (o.GetDepotActionType() & ODATFB_HALT) {
			json["depot-action"] = DA_STOP;
		} else if (o.GetDepotActionType() & ODATF_SERVICE_ONLY) {
			json["depot-action"] = DA_SERVICE;
		}
	}

	if (o.GetColour() != INVALID_COLOUR) {
		json["colour"] = o.GetColour();
	}

	if (o.IsGotoOrder() || o.GetType() == OT_CONDITIONAL) {
		if (o.IsTravelTimetabled()) {
			json["travel-time"] = o.GetTravelTime();
		}

		if (o.GetMaxSpeed() != UINT16_MAX) {
			json["max-speed"] = o.GetMaxSpeed();
		}
	}

	if (o.IsGotoOrder()) {
		if (o.IsWaitTimetabled()) {
			json["wait-time"] = o.GetWaitTime();
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
				json["stopping-pattern"] = o.GetNonStopType();
			}
		}
	}

	if (o.IsType(OT_GOTO_STATION)) {
		if (o.GetLoadType() != OLFB_CARGO_TYPE_LOAD && o.GetLoadType() != OLF_LOAD_IF_POSSIBLE) {
			json["load"] = o.GetLoadType();
		}

		if (o.GetUnloadType() != OUFB_CARGO_TYPE_UNLOAD && o.GetUnloadType() != OUF_UNLOAD_IF_POSSIBLE) {
			json["unload"] = o.GetUnloadType();
		}

		for (CargoType i = 0; i < NUM_CARGO; i++) {
			if (o.GetLoadType() == OLFB_CARGO_TYPE_LOAD && o.GetCargoLoadType(i) != OLF_LOAD_IF_POSSIBLE) {
				json["load-by-cargo-type"][std::to_string(i)]["load"] = o.GetCargoLoadType(i);
			}

			if (o.GetUnloadType() == OUFB_CARGO_TYPE_UNLOAD && o.GetCargoUnloadType(i) != OUF_UNLOAD_IF_POSSIBLE) {
				json["load-by-cargo-type"][std::to_string(i)]["unload"] = o.GetCargoUnloadType(i);
			}
		}

		if (vt == VEH_TRAIN && o.GetStopLocation() != _settings_client.gui.stop_location) {
			json["stop-location"] = o.GetStopLocation();
		} else if (vt == VEH_ROAD && o.GetRoadVehTravelDirection() != INVALID_DIAGDIR) {
			json["stop-direction"] = o.GetRoadVehTravelDirection();
		}
	}

	if (o.IsSlotCounterOrder()) {
		json["id"] = o.GetDestination().ToSlotID().base();
	}

	if (o.IsType(OT_LABEL)) {
		if (o.GetLabelSubType() == OLST_TEXT) {
			json["label-text"] = o.GetLabelText();
		} else {
			json["label-subtype"] = o.GetLabelSubType();
		}
	}
	if (o.IsType(OT_COUNTER)) {
		json["operator"] = o.GetCounterOperation();
		json["value"] = o.GetXData();
	}

	if (o.IsType(OT_SLOT)) {
		json["operation"] = o.GetSlotSubType();
	}

	if (o.IsType(OT_CONDITIONAL)) {
		json["condition"] = o.GetConditionVariable();

		if (o.GetConditionVariable() != OCV_UNCONDITIONALLY) {
			json["comparator"] = o.GetConditionComparator();
		}

		json["jump-to"] = o.GetConditionSkipToOrder(); // NB: this gets overwritten later by the labeling system.

		if (ConditionVariableHasStationID(o.GetConditionVariable())) {
			json["station"] = o.GetConditionStationID().base();
		}

		switch (o.GetConditionVariable()) {
			case OCV_UNCONDITIONALLY:
				break;

			case OCV_SLOT_OCCUPANCY:
			case OCV_CARGO_LOAD_PERCENTAGE:
			case OCV_TIME_DATE:
			case OCV_TIMETABLE:
			case OCV_VEH_IN_SLOT_GROUP:
			case OCV_VEH_IN_SLOT:
				json["value1"] = o.GetXData();
				break;

			case OCV_COUNTER_VALUE:
			case OCV_CARGO_WAITING_AMOUNT:
			case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
				json["value1"] = o.GetXDataLow();
				break;

			default:
				json["value1"] = o.GetConditionValue();
				break;
		}

		switch (o.GetConditionVariable()) {
			case OCV_COUNTER_VALUE:
				json["value2"] = o.GetXDataHigh();
				break;

			case OCV_DISPATCH_SLOT:
				json["value2"] = o.GetConditionDispatchScheduleID();
				break;

			case OCV_CARGO_LOAD_PERCENTAGE:
			case OCV_CARGO_WAITING_AMOUNT:
			case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
			case OCV_TIME_DATE:
			case OCV_TIMETABLE:
				json["value2"] = o.GetConditionValue();
				break;

			default:
				break;
		}

		if (o.HasConditionViaStation()) {
			json["value3"] = o.GetConditionViaStationID().base();
		}

		if (o.GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT_PERCENTAGE) {
			json["value4"] = GB(o.GetXData2(), 16, 1);
		}
	}

	if (o.IsRefit()) {
		json["refit-cargo"] = o.GetRefitCargo();
	}

	if (o.IsScheduledDispatchOrder(false)) {
		json["schedule-index"] = o.GetDispatchScheduleIndex();
	}

	return json;
}

static nlohmann::ordered_json DispatchScheduleToJSON(const DispatchSchedule &sd)
{
	nlohmann::ordered_json json;

	for (uint i = 0; i < DispatchSchedule::DEPARTURE_TAG_COUNT; i++) {
		std::string_view rename = sd.GetSupplementaryName(SDSNT_DEPARTURE_TAG, i);
		if (!rename.empty()) {
			json["renamed-tags"][std::to_string(i + 1)] = rename;
		}
	}

	for (const auto &sd_slot : sd.GetScheduledDispatch()) {
		auto &slotJson = json["slots"][std::to_string(sd_slot.offset)];

		if (HasBit(sd_slot.flags, DispatchSlot::SDSF_REUSE_SLOT)) {
			slotJson["re-use-slot"] = true;
		}

		for (uint i = 0; i <= (DispatchSlot::SDSF_LAST_TAG - DispatchSlot::SDSF_FIRST_TAG); i++) {
			if (HasBit(sd_slot.flags, DispatchSlot::SDSF_FIRST_TAG + i)) {
				slotJson["tags"].push_back(std::to_string(i + 1));
			}
		}
	}

	if (!sd.ScheduleName().empty()) {
		json["name"] = sd.ScheduleName();
	}

	json["duration"] = sd.GetScheduledDispatchDuration();

	if (sd.GetScheduledDispatchDelay() != 0) {
		json["max-delay"] = sd.GetScheduledDispatchDelay();
	}

	if (sd.GetScheduledDispatchReuseSlots()) {
		json["re-use-all-slots"] = true;
	}

	return json;
}

std::string OrderListToJSONString(const OrderList *ol)
{
	nlohmann::ordered_json json;

	json["version"] = ORDERLIST_JSON_OUTPUT_VERSION;
	json["source"] = std::string(_openttd_revision);

	if (ol == nullptr) { // order list not initialised, return an empty result
		json["error"] = "Orderlist was not initialised";
		return json;
	};

	VehicleType vt = ol->GetFirstSharedVehicle()->type;
	json["vehicle-type"] = vt;

	auto &game_properties = json["game-properties"];

	game_properties["default-stop-location"] = (OrderStopLocation)_settings_client.gui.stop_location;
	game_properties["new-nonstop"] = _settings_client.gui.new_nonstop;

	if (_settings_time.time_in_minutes) {
		game_properties["ticks-per-minute"] = _settings_time.ticks_per_minute;
	} else {
		game_properties["ticks-per-day"] = TicksPerCalendarDay();
	}

	const auto &sd_data = ol->GetScheduledDispatchScheduleSet();

	if (sd_data.size() != 0) {
		auto schedules = nlohmann::ordered_json::array();
		for (const auto &sd : sd_data) {
			schedules.push_back(DispatchScheduleToJSON(sd));
		}
		json["schedules"] = std::move(schedules);
	}

	auto orders = nlohmann::ordered_json::array();
	for (const Order *o : ol->Orders()) {
		orders.push_back(OrderToJSON(*o, vt));
	}

	/* Tagging system for jumps */
	std::string tag = fmt::format("{:04X}-", InteractiveRandomRange(0xFFFF));

	for (auto &val : orders) {
		if (val.contains("jump-to")) {
			const auto &target = orders[(VehicleOrderID)val["jump-to"]];
			std::string label = target.contains("jump-from") ? (std::string)target["jump-from"] : tag + std::to_string((VehicleOrderID)val["jump-to"]);

			orders[(VehicleOrderID)val["jump-to"]]["jump-from"] = label;
			val["jump-to"] = std::move(label);
		}
	}

	json["orders"] = std::move(orders);

	return json.dump(4);
}

Colours ErrorTypeToColour(JsonOrderImportErrorType error_type)
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
	size_t order_pos = 0;

	JSONBulkOrderCommandBuffer(const Vehicle *v) : tile(v->tile), op_serialiser(this->cmd_data.cmds)
	{
		cmd_data.veh = v->index;
	}

	JSONBulkOrderCommandBuffer(const JSONBulkOrderCommandBuffer &) = delete;
	JSONBulkOrderCommandBuffer(JSONBulkOrderCommandBuffer &&) = delete;
	JSONBulkOrderCommandBuffer &operator=(const JSONBulkOrderCommandBuffer &) = delete;
	JSONBulkOrderCommandBuffer &operator=(JSONBulkOrderCommandBuffer &&) = delete;

	void SendCmd()
	{
		if (!this->cmd_data.cmds.empty()) {
			EnqueueDoCommandP<CMD_BULK_ORDER>(this->tile, this->cmd_data, (StringID)0);
			this->cmd_data.cmds.clear();
		}
	}

	void StartOrder()
	{
		if (this->cmd_data.cmds.size() >= BULK_ORDER_MAX_CMD_SIZE) {
			this->next_buffer.clear();
			this->next_buffer.insert(this->next_buffer.begin(), this->cmd_data.cmds.begin() + this->order_pos, this->cmd_data.cmds.end());
			this->cmd_data.cmds.resize(this->order_pos);
			this->SendCmd();
			this->cmd_data.cmds.swap(this->next_buffer);
			this->next_buffer.clear();
		}
		this->order_pos = this->cmd_data.cmds.size();
	}

	void Flush()
	{
		this->StartOrder();
		this->SendCmd();
	}
};

struct JsonErrors {
	struct Error {
		std::string error;
		JsonOrderImportErrorType error_type;
	};

	std::vector<Error> global_errors;
	robin_hood::unordered_map<VehicleOrderID, Error> order_errors;
	robin_hood::unordered_map<int, Error> schedule_errors;
};

//temp
typedef int ScheduleDispatchID;

template <typename ID=void*>
class JSONToVehicleCommandParser {
	static_assert(
		std::is_same_v<ID, void*> || std::is_same_v<ID, VehicleOrderID> || std::is_same_v<ID, ScheduleDispatchID>,
		"JSONToVehicleCommandParser: ID must be void*, VehicleOrderID, or ScheduleDispatchID"
		);

public:
	const JSONImportSettings &import_settings;
	JSONBulkOrderCommandBuffer &cmd_buffer;

private:
	const Vehicle *veh;
	const nlohmann::json &json;
	const ID target_index;

	JsonErrors &errors;

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
	JSONToVehicleCommandParser(const Vehicle *veh, const nlohmann::json &json, JSONBulkOrderCommandBuffer &cmd_buffer, JsonErrors &errors, const JSONImportSettings &import_settings, ID target_index = nullptr)
			: import_settings(import_settings), cmd_buffer(cmd_buffer), veh(veh), json(json), target_index(target_index), errors(errors) {}

	const Vehicle *GetVehicle() const { return this->veh; }
	const nlohmann::json &GetJson() const { return this->json; }

	void LogGlobalError(std::string error, JsonOrderImportErrorType error_type)
	{
		Debug(misc, 1, "Order import error: {}, type: {}, global", error, error_type);
		this->errors.global_errors.push_back({ error, error_type });
	}

	void LogError(std::string error, JsonOrderImportErrorType error_type)
	{
		if (error_type == JOIET_OK) return;

		if constexpr (std::is_same_v<ID, void*>) {
			LogGlobalError(error, error_type);
		} else if constexpr (std::is_same_v<ID, VehicleOrderID>) {
			Debug(misc, 1, "Order import error: {}, type: {}, order: {}", error, error_type, this->target_index);
			this->errors.order_errors[this->target_index] = { std::move(error), error_type };
		} else if constexpr (std::is_same_v<ID, ScheduleDispatchID >){
			Debug(misc, 1, "Order import error: {}, type: {}, dispatch_slot: {}", error, error_type, this->target_index);
			this->errors.schedule_errors[this->target_index] = { std::move(error), error_type };
		}
	}

	template <typename T>
	bool TryGetField(std::string_view key, T &value, JsonOrderImportErrorType fail_type)
	{
		if (json.contains(key)) {
			try {
				const T &temp = (T)json[key];

				/* Special case for enums, here we can also check if the value is valid. */
				if constexpr (std::is_enum<T>::value) {
					const char *result = nullptr;
					to_json(result, temp);
					if (result == nullptr) {
						LogError(fmt::format("Value of '{}' is invalid", key), fail_type);
						return false;
					}
				}

				value = temp;
				return true;
			} catch (...) {
				LogError(fmt::format("Data type of '{}' is invalid", key), fail_type);
				return false;
			}
		} else if (fail_type == JOIET_CRITICAL) {
			LogError(fmt::format("Required '{}' is missing", key), fail_type);
		}
		return false;
	}

	template <typename T = uint16_t>
	bool TryApplyTimetableCommand(std::string_view field, ModifyTimetableFlags mtf,
		JsonOrderImportErrorType error_type, VehicleOrderID oid = INVALID_VEH_ORDER_ID)
	requires std::is_same_v<ID, VehicleOrderID>
	{
		static_assert(std::is_convertible_v<T, int>, "Timetable operations only take numerical values");

		return ParserFuncWrapper<T>(field, std::nullopt, error_type,
			[&](T val) {
				if (oid != INVALID_VEH_ORDER_ID) this->cmd_buffer.op_serialiser.SeekTo(oid);
				cmd_buffer.op_serialiser.Timetable(mtf, val, MTCF_NONE);
				return true;
			}
		);
	}
	
	void ModifyOrder(ModifyOrderFlags mof, uint16_t val, CargoType cargo = INVALID_CARGO, std::string text = {}, VehicleOrderID oid = INVALID_VEH_ORDER_ID)
	requires std::is_same_v<ID, VehicleOrderID>
	{
		if (oid != INVALID_VEH_ORDER_ID) this->cmd_buffer.op_serialiser.SeekTo(oid);
		this->cmd_buffer.op_serialiser.Modify(mof, val, cargo, std::move(text));
	}

	template <typename T>
	bool TryApplyModifyOrder(std::string_view field, ModifyOrderFlags mof, JsonOrderImportErrorType error_type, std::optional<T> default_val = std::nullopt, CargoType cargo = INVALID_CARGO, VehicleOrderID oid = INVALID_VEH_ORDER_ID)
	requires std::is_same_v<ID, VehicleOrderID>
	{
		return ParserFuncWrapper<T>(field, default_val, error_type,
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

	bool TryApplySchDispatchAddSlotsCommand (JsonOrderImportErrorType error_type, std::string error_msg, std::vector<std::pair<uint32_t, uint16_t>> &&slots)
	requires std::is_same_v<ID, ScheduleDispatchID>
	{
		SchDispatchBulkAddCmdData payload;
		payload.veh = this->veh->index;
		payload.schedule_index = (this->veh->orders == nullptr) ? 0 : this->veh->orders->GetScheduledDispatchScheduleCount() - 1;
		payload.slots = std::move(slots);
		bool result = DoCommandP<CMD_SCH_DISPATCH_BULK_ADD>(veh->tile, payload, (StringID)0);
		if (!result) {
			this->LogError(fmt::format("Error importing schedule {}: {} ", std::to_string(payload.schedule_index), error_msg), error_type);
		}
		return result;
	}

	template <typename T, Commands cmd>
	bool TryApplySchDispatchCommand(std::string_view field, JsonOrderImportErrorType error_type, std::optional<T> default_val, uint32_t offset)
	requires std::is_same_v<ID, ScheduleDispatchID>
	{
		uint schedule_index = (this->veh->orders == nullptr) ? 0 : this->veh->orders->GetScheduledDispatchScheduleCount() - 1;
		return ParserFuncWrapper<T>(field, default_val, error_type,
			[&](T val) {
				return Command<cmd>::Post(this->veh->index, schedule_index, offset, val);
			});
	}

	template <typename T, Commands cmd>
	bool TryApplySchDispatchCommand (std::string_view field, JsonOrderImportErrorType error_type, std::optional<T> default_val)
	requires std::is_same_v<ID, ScheduleDispatchID>
	{
		uint schedule_index = (this->veh->orders == nullptr) ? 0 : this->veh->orders->GetScheduledDispatchScheduleCount() - 1;
		return ParserFuncWrapper<T>(field, default_val, error_type,
			[&](T val) {
				return Command<cmd>::Post(this->veh->index, schedule_index, val);
			});
	}

	JSONToVehicleCommandParser WithNewJson(const nlohmann::json &new_json)
	{
		return JSONToVehicleCommandParser(this->veh, new_json, this->cmd_buffer, this->errors, this->import_settings, this->target_index);
	}

	template <typename T=ID>
	JSONToVehicleCommandParser<T> WithNewTarget(T order_id)
	{
		return JSONToVehicleCommandParser<T> (this->veh, this->json, this->cmd_buffer, this->errors, this->import_settings, order_id);
	}
	
	JSONToVehicleCommandParser operator[](auto val)
	{
		return this->WithNewJson(this->json[val]);
	}
};

static void ImportJsonOrder(JSONToVehicleCommandParser<VehicleOrderID> json_importer)
{
	const Vehicle *veh = json_importer.GetVehicle();
	const nlohmann::json &json = json_importer.GetJson();

	OrderType type;

	if (!json_importer.TryGetField("type", type, JOIET_CRITICAL)) {
		json_importer.cmd_buffer.op_serialiser.InsertFail();
		return;
	}

	DestinationID destination = StationID::Invalid();
	OrderLabelSubType labelSubtype = OLST_TEXT;

	/* Get basic order data required to build order. */
	switch (type) {
		case OT_LABEL:
			json_importer.TryGetField("label-subtype", labelSubtype, JOIET_MAJOR);
			if (labelSubtype == OLST_DEPARTURES_REMOVE_VIA || labelSubtype == OLST_DEPARTURES_VIA) {
				if (json_importer.TryGetField("destination-id", destination.edit_base(), JOIET_MAJOR)) {
					destination = StationID(destination.edit_base());
				}
			}
			break;

		case OT_GOTO_STATION:
		case OT_GOTO_WAYPOINT:
		case OT_IMPLICIT:
			if (json_importer.TryGetField("destination-id", destination.edit_base(), JOIET_MAJOR)) {
				destination = StationID(destination.edit_base());
			}
			break;

		case OT_GOTO_DEPOT:
			if (json_importer.TryGetField("depot-id", destination.edit_base(), JOIET_OK)) {
				destination = DepotID(destination.edit_base());
			} else {
				destination = DepotID::Invalid();
				if (json.contains("depot-id")) {
					if (!json["depot-id"].is_string() || !(json["depot-id"] == "nearest")) {
						json_importer.LogError("Value of 'depot-id' is invalid", JOIET_MAJOR);
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
			if (!json_importer.TryGetField("operation", osst, JOIET_CRITICAL)) {
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
	} else {
		if (_settings_game.order.nonstop_only) {
			new_order.SetNonStopType(ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
		}
	}

	/* Create the order */
	json_importer.cmd_buffer.op_serialiser.Insert(new_order);
	json_importer.cmd_buffer.op_serialiser.ReplaceOnFail();

	json_importer.TryApplyTimetableCommand("max-speed", MTF_TRAVEL_SPEED, JOIET_MINOR);
	json_importer.TryApplyTimetableCommand("wait-time", MTF_WAIT_TIME, JOIET_MINOR);
	json_importer.TryApplyTimetableCommand("travel-time", MTF_TRAVEL_TIME, JOIET_MINOR);

	json_importer.TryApplyModifyOrder<OrderStopLocation>("stop-location", MOF_STOP_LOCATION, JOIET_MINOR,
			veh->type == VEH_TRAIN ? std::optional(json_importer.import_settings.stop_location) : std::nullopt);

	json_importer.TryApplyModifyOrder<DiagDirection>("stop-direction", MOF_RV_TRAVEL_DIR, JOIET_MINOR);
	json_importer.TryApplyModifyOrder<OrderWaypointFlags>("waypoint-action", MOF_WAYPOINT_FLAGS, JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<std::string>("label-text", MOF_LABEL_TEXT, JOIET_MINOR);
	json_importer.TryApplyModifyOrder<OrderDepotAction>("depot-action", MOF_DEPOT_ACTION, JOIET_MAJOR, DA_ALWAYS_GO);
	json_importer.TryApplyModifyOrder<Colours>("colour", MOF_COLOUR, JOIET_MINOR);

	bool is_default_non_stop = json_importer.import_settings.new_nonstop || _settings_game.order.nonstop_only;
	OrderNonStopFlags default_non_stop;
	if (new_order.IsType(OT_GOTO_WAYPOINT)) {
		default_non_stop = is_default_non_stop ? ONSF_NO_STOP_AT_ANY_STATION : ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS;
	} else {
		default_non_stop = is_default_non_stop ? ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS : ONSF_STOP_EVERYWHERE;
	}

	json_importer.TryApplyModifyOrder<OrderNonStopFlags>("stopping-pattern", MOF_NON_STOP, JOIET_MAJOR,
			veh->IsGroundVehicle() ? std::optional(default_non_stop) : std::nullopt);

	if (type == OT_CONDITIONAL) {
		json_importer.TryApplyModifyOrder<OrderConditionVariable>("condition", MOF_COND_VARIABLE, JOIET_CRITICAL, OCV_END);
		json_importer.TryApplyModifyOrder<OrderConditionComparator>("comparator", MOF_COND_COMPARATOR, JOIET_MAJOR);
	}
	json_importer.TryApplyModifyOrder<StationID>("station", MOF_COND_STATION_ID, JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<uint16_t>("value1", MOF_COND_VALUE, JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<uint16_t>("value2", MOF_COND_VALUE_2, JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<uint16_t>("value3", MOF_COND_VALUE_3, JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<uint16_t>("value4", MOF_COND_VALUE_4, JOIET_MAJOR);

	switch (type) {
		case OT_SLOT:
			json_importer.TryApplyModifyOrder<uint16_t>("id", MOF_SLOT, JOIET_MAJOR);
			break;

		case OT_SLOT_GROUP:
			json_importer.TryApplyModifyOrder<uint16_t>("id", MOF_SLOT_GROUP, JOIET_MAJOR);
			break;

		case OT_COUNTER:
			json_importer.TryApplyModifyOrder<uint16_t>("id", MOF_COUNTER_ID, JOIET_MAJOR);
			json_importer.TryApplyModifyOrder<uint8_t>("operator", MOF_COUNTER_OP, JOIET_MAJOR);
			json_importer.TryApplyModifyOrder<uint16_t>("value", MOF_COUNTER_VALUE, JOIET_MAJOR);
			break;

		default:
			break;
	}

	json_importer.TryApplyModifyOrder<OrderLoadFlags>("load", MOF_LOAD, JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<OrderUnloadFlags>("unload", MOF_UNLOAD, JOIET_MAJOR);

	if (json.contains("load-by-cargo-type")) {
		if (json["load-by-cargo-type"].is_object()) {
			for (const auto &[key, val] : json["load-by-cargo-type"].items()) {
				auto cargo_res = IntFromChars<CargoType>((std::string_view)key);
				if (!cargo_res.has_value() || *cargo_res >= NUM_CARGO) {
					json_importer.LogError(fmt::format("in 'load-by-cargo-type','{}' is not a valid cargo_id", key), JOIET_MAJOR);
					continue;
				}
				CargoType cargo_id = *cargo_res;

				if (!val.is_object()) {
					json_importer.LogError(fmt::format("loading options in 'load-by-cargo-type'[{}] are not valid",key), JOIET_MAJOR);
					continue;
				};

				if (val.contains("load")) {
					json_importer["load-by-cargo-type"][key].TryApplyModifyOrder<OrderLoadFlags>("load", MOF_CARGO_TYPE_LOAD, JOIET_MAJOR, std::nullopt, cargo_id);
				}

				if (val.contains("unload")) {
					json_importer["load-by-cargo-type"][key].TryApplyModifyOrder<OrderUnloadFlags>("unload", MOF_CARGO_TYPE_UNLOAD, JOIET_MAJOR, std::nullopt, cargo_id);
				}
			}
		} else { // 'load-by-cargo-type' is not an object
			json_importer.LogError("'load-by-cargo-type' must be an object", JOIET_MAJOR);
		}
	}

	/* Refit works in a weird way, so it gets treated weirdly. */
	if (json.contains("refit-cargo")) {
		if (json["refit-cargo"].is_string()) {
			if (json["refit-cargo"] == "auto") {
				json_importer.cmd_buffer.op_serialiser.Refit(CARGO_AUTO_REFIT);
			} else {
				json_importer.LogError("Value of 'refit-cargo' is invalid", JOIET_MAJOR);
			}
		} else {
			CargoType cargo_id;
			if (json_importer.TryGetField("refit-cargo", cargo_id, JOIET_MAJOR)) {
				json_importer.cmd_buffer.op_serialiser.Refit(cargo_id);
			} else {
				json_importer.LogError("Value of 'refit-cargo' is invalid", JOIET_MAJOR);
			}
		}
	}
}

/**
* Returns the tag index for a given rapresentative tag string, or -1 if it fails to parse the string
*/
static int TagStringToIndex(std::string_view tag)
{
	/* Format : ^[1-4]$ */
	auto res = IntFromChars<int>(tag);
	if (res.has_value() && *res >= 1 && *res <= 4) return *res - 1;
	return -1;
}

static void ImportJsonDispatchSchedule(JSONToVehicleCommandParser<ScheduleDispatchID> json_importer)
{
	const Vehicle *veh = json_importer.GetVehicle();
	const nlohmann::json &json = json_importer.GetJson();

	if (json.is_null()) {
		return;
	}

	uint32_t duration = 0;
	if (!json_importer.TryGetField("duration", duration, JOIET_MAJOR)) {
		return;
	}
	if (duration == 0) {
		return;
	}

	// TODO: More changes required for this to work in multiplayer

	StateTicks start_tick = _settings_time.FromTickMinutes(_settings_time.NowInTickMinutes().ToSameDayClockTime(0, 0));
	Command<CMD_SCH_DISPATCH_ADD_NEW_SCHEDULE>::Post(veh->index, start_tick, duration);
	
	json_importer.TryApplySchDispatchCommand<std::string, CMD_SCH_DISPATCH_RENAME_SCHEDULE>("name", JOIET_MINOR, std::nullopt);
	json_importer.TryApplySchDispatchCommand<uint, CMD_SCH_DISPATCH_SET_DELAY>("max-delay", JOIET_MAJOR, std::nullopt);
	json_importer.TryApplySchDispatchCommand<bool, CMD_SCH_DISPATCH_SET_REUSE_SLOTS>("re-use-all-slots", JOIET_MAJOR, std::nullopt);

	if (json.contains("renamed-tags") && json["renamed-tags"].is_object()) {
		for (const auto &names : json["renamed-tags"].items()) {
			int index = TagStringToIndex(names.key());

			if (index == -1 || !names.value().is_string()) {
				json_importer.LogError(fmt::format("'{}' is not a valid tag index.", names.key()), JOIET_MINOR);
			} else {
				json_importer["renamed-tags"].TryApplySchDispatchCommand<std::string, CMD_SCH_DISPATCH_RENAME_TAG>(names.key(), JOIET_MINOR, std::nullopt, index);
			}
		}
	}

	if (json.contains("slots")) {
		const auto &slotsJson = json.at("slots");
		if (slotsJson.is_object()) {
			std::vector<std::pair<uint32_t, uint16_t>> slots_to_add;
			auto flush = [&]() {
				if (slots_to_add.empty()) return;
				json_importer.TryApplySchDispatchAddSlotsCommand(JOIET_MAJOR, "Could not load slots", std::move(slots_to_add));
				slots_to_add.clear();
			};

			for (const auto &it : slotsJson.items()) {
				auto res = IntFromChars<uint32_t>(it.key());
				if (!res.has_value()) {
					json_importer.LogError("Dispatch schedule slot key not in ticks", JOIET_MAJOR);
					continue;
				}
				uint32_t offset = *res;

				const nlohmann::json &slotData = it.value();

				if (slotData.is_object() || slotData.is_null()) {
					auto local_importer = json_importer["slots"][it.key()];

					bool re_use_slot = false;
					local_importer.TryGetField("re-use-slot", re_use_slot, JOIET_MAJOR);

					uint32_t flags =  0;
					if (re_use_slot) {
						SetBit(flags, DispatchSlot::SDSF_REUSE_SLOT);
					}

					if (slotData.contains("tags") && slotData["tags"].is_array()) {
						for (nlohmann::json::const_reference tag : slotData["tags"]) {
							if (tag.is_string()) {
								std::string tagString = std::string(tag);
								int tag = TagStringToIndex(tagString);
								if (tag == -1) {
									json_importer.LogError(fmt::format("'{}' is not a valid tag index",tagString), JOIET_MAJOR);
									continue;
								}
								SetBit(flags, DispatchSlot::SDSF_FIRST_TAG + tag);
							}
						}
					}

					slots_to_add.push_back({ offset, flags });

					if (slots_to_add.size() >= 512) flush(); // Limit number of slots per command
				}
			}

			flush();
		}
	}
}

void ImportJsonOrderList(const Vehicle *veh, std::string_view json_str)
{
	assert(veh != nullptr);

	nlohmann::json json;
	JsonErrors errors = {};

	try {
		json = nlohmann::json::parse(json_str);
	} catch (const nlohmann::json::parse_error &) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_JSON), GetEncodedString(STR_ERROR_ORDERLIST_MALFORMED_JSON), WL_ERROR);
		return;
	}

	if (!json.contains("orders") || !json["orders"].is_array() || json["orders"].size() == 0) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_JSON), GetEncodedString(STR_ERROR_ORDERLIST_JSON_NEEDS_ORDERS), WL_ERROR);
		return;
	}

	/* Checking if the vehicle type matches */
	if (!json.contains("vehicle-type")) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_JSON), GetEncodedString(STR_ERROR_ORDERLIST_JSON_VEHICLE_TYPE_MISSING), WL_ERROR);
		return;
	}

	VehicleType vt;
	try {
		vt = json["vehicle-type"];
	} catch (...) {
		vt = VEH_END;
	}

	if (vt != veh->type) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_JSON), GetEncodedString(STR_ERROR_ORDERLIST_JSON_VEHICLE_TYPE_DOES_NOT_MATCH), WL_ERROR);
		return;
	}

	JSONImportSettings import_settings_client{};

	/* If the json cntains game-properties, we will try to parse them and apply them */
	if (json.contains("game-properties") && json["game-properties"].is_object()) {
		const auto &game_properties = json["game-properties"];

		OrderStopLocation osl = game_properties.value<OrderStopLocation>("default-stop-location", OSL_END);
		if (osl == OSL_END) {
			errors.global_errors.push_back({ "'default-stop-location' missing or invalid in 'game-properties', this may cause discrepancies when loading the orderlist", JOIET_MAJOR });
		} else {
			import_settings_client.stop_location = osl;
		}

		if (game_properties.contains("new-nonstop") && game_properties["new-nonstop"].is_boolean()) {
			bool new_nonstop = game_properties["new-nonstop"];
			if (!new_nonstop && _settings_game.order.nonstop_only) {
				errors.global_errors.push_back({ "'new-nonstop' is not compatible with the current game setting, this may cause discrepancies when loading the orderlist", JOIET_MAJOR });
			}
			import_settings_client.new_nonstop = new_nonstop;
		} else {
			errors.global_errors.push_back({ "'new-nonstop' missing or invalid in 'game-properties', this may cause discrepancies when loading the orderlist", JOIET_MAJOR });
		}
	} else {
		errors.global_errors.push_back({ "no valid 'game-properties' found, current setings will be assumed to be correct", JOIET_MAJOR });
	}

	JSONBulkOrderCommandBuffer cmd_buffer(veh);
	JSONToVehicleCommandParser<> json_importer(veh, json, cmd_buffer, errors, import_settings_client);

	const auto &orders_json = json["orders"];

	robin_hood::unordered_map<std::string, VehicleOrderID> jump_map; // Associates jump labels to actual order-ids until all orders are added

	if (json.contains("schedules")) {
		const auto &schedules = json["schedules"];
		if (schedules.is_array() && schedules.size() > 0) {
			bool have_schedule = false;
			for (const auto &value : orders_json) {
				if (value.contains("schedule-index")) {
					have_schedule = true;
					break;
				}
			}

			if (have_schedule && HasBit(veh->vehicle_flags, VF_TIMETABLE_SEPARATION)) {
				Command<CMD_TIMETABLE_SEPARATION>::Post(veh->index, false);
			}

			ScheduleDispatchID schedule_index = 0;
			for (const auto &value : schedules) {
				ImportJsonDispatchSchedule(json_importer.WithNewJson(value).WithNewTarget(schedule_index));
				schedule_index++;
			}

			if (have_schedule && !HasBit(veh->vehicle_flags, VF_SCHEDULED_DISPATCH)) {
				Command<CMD_SCH_DISPATCH>::Post(veh->index, true);
			}
		}
	}

	/* Delete all orders before setting the new orders */
	cmd_buffer.op_serialiser.ClearOrders();

	VehicleOrderID order_id = 0;
	for (const auto &value : orders_json) {
		auto order_importer = json_importer.WithNewJson(value).WithNewTarget(order_id);

		cmd_buffer.StartOrder();
		ImportJsonOrder(order_importer);

		std::string jump_label;
		if (order_importer.TryGetField("jump-from", jump_label, JOIET_MAJOR)) {
			jump_map[jump_label] = order_id;
		}

		order_id++;
	}

	/* Post processing (link jumps and assign schedules) */
	order_id = 0;
	for (const auto &value : orders_json) {
		auto local_importer = json_importer.WithNewJson(value).WithNewTarget(order_id);

		cmd_buffer.StartOrder();
		local_importer.TryApplyTimetableCommand("schedule-index", MTF_ASSIGN_SCHEDULE, JOIET_MAJOR, order_id);

		std::string jump_label;
		if (local_importer.TryGetField("jump-to", jump_label, JOIET_MAJOR)) {
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
}
