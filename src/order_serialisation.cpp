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

	if (sd.GetScheduledDispatchDuration() != _settings_client.company.default_sched_dispatch_duration) {
		json["duration"] = sd.GetScheduledDispatchDuration();
	}

	if (sd.GetScheduledDispatchDelay() != 0) {
		json["delay"] = sd.GetScheduledDispatchDelay();
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
		game_properties["default-scheduled-dispatch-duration"] = _settings_client.company.default_sched_dispatch_duration;

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

static Colours ErrorTypeToColour(JsonOrderImportErrorType error_type)
{
	switch (error_type) {
		case JOIET_CRITICAL: return COLOUR_RED;
		case JOIET_MAJOR: return COLOUR_ORANGE;
		case JOIET_MINOR: return COLOUR_CREAM;
		default: NOT_REACHED();
	}
}

class JSONToVehicleCommandParser {
public:
	struct Errors {
		struct Error {
			std::string error;
			JsonOrderImportErrorType error_type;
		};

		std::vector<Error> global_errors;
		robin_hood::unordered_map<VehicleOrderID, Error> order_error;
	};

	const ClientSettings &importSettings;

private:
	const Vehicle *veh;
	const nlohmann::json &json;
	Errors &errors;

	template <typename F>
	bool ParserFuncWrapper(JsonOrderImportErrorType error_type, std::string error_msg, F exec)
	{
		bool success = exec();
		if (!success) {
			this->LogError(std::move(error_msg), error_type, true);
		}

		return success;
	}

	template <typename T, typename F>
	bool ParserFuncWrapper(std::optional<std::string> field, std::optional<T> default_val, JsonOrderImportErrorType error_type, std::optional<VehicleOrderID> oid, F exec)
	{
		static_assert(std::is_same_v<T, std::string> || std::is_convertible_v<T, int> || std::is_base_of_v<PoolIDBase, T>, "data is either a string or it's convertible to int");
		assert(field.has_value() || default_val.has_value());

		T val;
		bool suppress_error = false;
		if (!field || !this->TryGetField<T>(*field, val, error_type, !oid.has_value(), oid)) {
			suppress_error = true;
			if (default_val) {
				val = *default_val;
			} else {
				return false;
			}
		}

		bool success = exec(val);
		if (field.has_value() && !success && !suppress_error) {
			this->LogError("Value for '" + field.value_or("<INTERNAL_ERROR>") + "' is invalid", error_type, !oid.has_value(), oid);
		}

		return success || suppress_error;
	}

public:

	JSONToVehicleCommandParser(const Vehicle *veh, const nlohmann::json &json, Errors &errors, const ClientSettings &importSettings = _settings_client)
			: importSettings(importSettings), veh(veh), json(json), errors(errors) {}

	const Vehicle *GetVehicle() const { return this->veh; }
	const nlohmann::json &GetJson() const { return this->json; }

	bool ModifyOrder(ModifyOrderFlags mof, uint16_t val, CargoType cargo = INVALID_CARGO, std::optional<std::string> text = std::nullopt, std::optional<VehicleOrderID> oid = std::nullopt)
	{
		return Command<CMD_MODIFY_ORDER>::Post(this->veh->tile, this->veh->index, oid.value_or(this->veh->GetNumOrders() - 1), mof, val, cargo, text.value_or(std::string()));
	}

	void LogError(std::string error, JsonOrderImportErrorType error_type, VehicleOrderID oid) {
		this->LogError(std::move(error), error_type, false, oid);
	}

	void LogError(std::string error, JsonOrderImportErrorType error_type, bool is_global_error = false, std::optional<VehicleOrderID> oid = std::nullopt) {
		assert(!(is_global_error && oid.has_value()));

		if (error_type == JOIET_OK) return;

		Debug(misc, 1, "Order import error: {}, type: {}, global: {}, oid: {}", error, error_type, is_global_error, oid);

		if (is_global_error) {
			this->errors.global_errors.push_back({ error, error_type });
			return;
		}

		VehicleOrderID order_index = oid.value_or(veh->GetNumOrders() - 1);

		if (error_type == JOIET_CRITICAL) {
			/* If the order id is not defined and it's a critical error, we are adding a label at the end of the list. */
			if (!oid.has_value()) {
				order_index += 1;
			} else {
				/* If we are get critical error on an existing orrer (oid defined), it will be replaced with a label, so we are deleting it. */
				Command<CMD_DELETE_ORDER>::Post(veh->tile, veh->index, order_index);
			}

			Order error_order;
			error_order.MakeLabel(OLST_TEXT);

			DoCommandP<CMD_INSERT_ORDER>(veh->tile, InsertOrderCmdData(this->veh->index, order_index, error_order), STR_ERROR_CAN_T_INSERT_NEW_ORDER);

			ModifyOrder(MOF_LABEL_TEXT, 0, 0, "[Critical Error] This order could not be parsed", order_index);
			ModifyOrder(MOF_COLOUR, ErrorTypeToColour(JOIET_CRITICAL), 0, std::nullopt, order_index);
		}

		this->errors.order_error[order_index] = { std::move(error), error_type };
	}

	template <typename T>
	bool TryGetField(std::string_view key, T &value, JsonOrderImportErrorType fail_type, VehicleOrderID oid) {
		return TryGetField(key, value, fail_type, false, oid);
	}

	template <typename T>
	bool TryGetField(std::string_view key, T &value, JsonOrderImportErrorType fail_type, bool is_global_error = false, std::optional<VehicleOrderID> oid = std::nullopt)
	{
		if (json.contains(key)) {
			try {
				const T &temp = (T)json[key];

				/* Special case for enums, here we can also check if the value is valid. */
				if constexpr (std::is_enum<T>::value) {
					const char *result = nullptr;
					to_json(result, temp);
					if (result == nullptr) {
						LogError(fmt::format("Value of '{}' is invalid", key), fail_type, is_global_error, oid);
						return false;
					}
				}

				value = temp;
				return true;
			} catch (...) {
				LogError(fmt::format("Data type of '{}' is invalid", key), fail_type, is_global_error, oid);
				return false;
			}
		} else if (fail_type == JOIET_CRITICAL) {
			LogError(fmt::format("Required '{}' is missing", key), fail_type, is_global_error, oid);
			return false;
		}
		return false;
	}

	template <typename T = uint16_t>
	bool TryApplyTimetableCommand(std::optional<std::string> field, ModifyTimetableFlags mtf, JsonOrderImportErrorType error_type) {
		return TryApplyTimetableCommand(std::move(field), mtf, error_type, this->veh->GetNumOrders() - 1);
	}

	template <typename T = uint16_t>
	bool TryApplyTimetableCommand(std::optional<std::string> field, ModifyTimetableFlags mtf, JsonOrderImportErrorType error_type, VehicleOrderID oid)
	{
		VehicleID vid = this->veh->index;

		static_assert(std::is_convertible_v<T,int>, "Timetable operations only take numerical values");
		return ParserFuncWrapper<T>(std::move(field), std::nullopt, error_type, oid,
			[&](T val) {
				return Command<CMD_CHANGE_TIMETABLE>::Post(vid, oid, mtf, val, MTCF_NONE);
			}
		);

	}

	template <typename T>
	bool TryApplyModifyOrder(std::optional<std::string> field, ModifyOrderFlags mof, JsonOrderImportErrorType error_type, std::optional<T> default_val = std::nullopt, CargoType cargo = INVALID_CARGO) {
		return TryApplyModifyOrder(std::move(field), mof, veh->GetNumOrders() - 1, error_type, default_val, cargo);
	}

	template <typename T>
	bool TryApplyModifyOrder(std::optional<std::string> field, ModifyOrderFlags mof, VehicleOrderID oid, JsonOrderImportErrorType error_type, std::optional<T> default_val = std::nullopt, CargoType cargo = INVALID_CARGO)
	{
		return ParserFuncWrapper<T>(std::move(field), default_val, error_type, oid,
			[&](T val) {
				if constexpr (std::is_same_v<std::string, T>) {
					return ModifyOrder(mof, 0, cargo, val);
				} else if constexpr (std::is_base_of_v<PoolIDBase, T>) {
					return ModifyOrder(mof, val.base(), cargo);
				} else {
					return ModifyOrder(mof, val, cargo);
				}
			});
	}


	bool TryApplySchDispatchSlotFlagsCommand(JsonOrderImportErrorType error_type, std::string error_msg, uint32_t offset, uint32_t flags)
	{
		uint schedule_index = (this->veh->orders == nullptr) ? 0 : this->veh->orders->GetScheduledDispatchScheduleCount() - 1;
		error_msg = "Error imoprting schedule " + std::to_string(schedule_index) + " : " + error_msg;
		return ParserFuncWrapper(error_type, error_msg,
			[&]() {
				return Command<CMD_SCH_DISPATCH_SET_SLOT_FLAGS>::Post((StringID)0, this->veh->index, schedule_index, offset, flags, flags);
			});
	}

	template <typename T, Commands cmd>
	bool TryApplySchDispatchCommand(std::optional<std::string> field, JsonOrderImportErrorType error_type, std::optional<T> default_val, uint32_t offset)
	{
		uint schedule_index = (this->veh->orders == nullptr) ? 0 : this->veh->orders->GetScheduledDispatchScheduleCount() - 1;
		return ParserFuncWrapper<T>(std::move(field), default_val, error_type, std::nullopt,
			[&](T val) {
				return Command<cmd>::Post((StringID)0, this->veh->index, schedule_index, offset, val);
			});
	}

	template <typename T, Commands cmd>
	bool TryApplySchDispatchCommand(std::optional<std::string> field, JsonOrderImportErrorType error_type, std::optional<T> default_val)
	{
		uint schedule_index = (this->veh->orders == nullptr) ? 0 : this->veh->orders->GetScheduledDispatchScheduleCount() - 1;
		return ParserFuncWrapper<T>(std::move(field), default_val, error_type, std::nullopt,
			[&](T val) {
				return Command<cmd>::Post((StringID)0, this->veh->index, schedule_index, val);
			});
	}


	/* Exposing some useful functions from json for easier access */
	bool Contains(std::string_view val) const { return json.contains(val); }
	bool IsObject() const { return json.is_object(); }
	bool IsNumber() const { return json.is_number(); }
	bool IsString() const { return json.is_string(); }

	JSONToVehicleCommandParser WithNewJson(const nlohmann::json &new_json)
	{
		return JSONToVehicleCommandParser(this->veh,new_json, this->errors, this->importSettings);
	}

	JSONToVehicleCommandParser operator[](auto val)
	{
		return this->WithNewJson(this->json[val]);
	}

};

static void ImportJsonOrder(JSONToVehicleCommandParser json_importer)
{
	const Vehicle *veh = json_importer.GetVehicle();
	nlohmann::json json = json_importer.GetJson();

	OrderType type;

	if (!json_importer.TryGetField("type", type, JOIET_CRITICAL)) return;

	DestinationID destination = StationID::Invalid();
	OrderLabelSubType labelSubtype = OLST_TEXT;

	/* Get basic order data required to build order. */
	switch (type) {
		case OT_LABEL:
			json_importer.TryGetField("label-subtype", labelSubtype, JOIET_MAJOR, veh->GetNumOrders());
			if (labelSubtype == OLST_DEPARTURES_REMOVE_VIA || labelSubtype == OLST_DEPARTURES_VIA) {
				if (json_importer.TryGetField("destination-id", destination.edit_base(), JOIET_MAJOR, veh->GetNumOrders())) {
					destination = StationID(destination.edit_base());
				}
			}
			break;

		case OT_GOTO_STATION:
		case OT_GOTO_WAYPOINT:
		case OT_IMPLICIT:
			if (json_importer.TryGetField("destination-id", destination.edit_base(), JOIET_MAJOR, veh->GetNumOrders())) {
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
						json_importer.LogError("Value of 'depot-id' is invalid", JOIET_MAJOR, veh->GetNumOrders());
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
	bool success = DoCommandP<CMD_INSERT_ORDER>(veh->tile, InsertOrderCmdData(veh->index, veh->GetNumOrders(), new_order), (StringID)0);

	if (!success) {
		json_importer.LogError("The order could not be imported for an unknown reason", JOIET_CRITICAL);
		return;
	}

	json_importer.TryApplyTimetableCommand("max-speed", MTF_TRAVEL_SPEED, JOIET_MINOR);
	json_importer.TryApplyTimetableCommand("wait-time", MTF_WAIT_TIME, JOIET_MINOR);
	json_importer.TryApplyTimetableCommand("travel-time", MTF_TRAVEL_TIME, JOIET_MINOR);

	json_importer.TryApplyModifyOrder<OrderStopLocation>("stop-location", MOF_STOP_LOCATION, JOIET_MINOR,
			veh->type == VEH_TRAIN ? std::optional((OrderStopLocation)json_importer.importSettings.gui.stop_location) : std::nullopt);

	json_importer.TryApplyModifyOrder<DiagDirection>("stop-direction", MOF_RV_TRAVEL_DIR, JOIET_MINOR);
	json_importer.TryApplyModifyOrder<OrderWaypointFlags>("waypoint-action", MOF_WAYPOINT_FLAGS, JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<std::string>("label-text", MOF_LABEL_TEXT, JOIET_MINOR);
	json_importer.TryApplyModifyOrder<OrderDepotAction>("depot-action", MOF_DEPOT_ACTION, JOIET_MAJOR, DA_ALWAYS_GO);
	json_importer.TryApplyModifyOrder<Colours>("colour", MOF_COLOUR, JOIET_MINOR);

	bool isDefaultNonStop = json_importer.importSettings.gui.new_nonstop || _settings_game.order.nonstop_only;
	OrderNonStopFlags defaultNonStop;
	if (new_order.IsType(OT_GOTO_WAYPOINT)) {
		defaultNonStop = isDefaultNonStop ? ONSF_NO_STOP_AT_ANY_STATION : ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS;
	} else {
		defaultNonStop = isDefaultNonStop ? ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS : ONSF_STOP_EVERYWHERE;
	}

	json_importer.TryApplyModifyOrder<OrderNonStopFlags>("stopping-pattern", MOF_NON_STOP, JOIET_MAJOR,
			veh->IsGroundVehicle() ? std::optional(defaultNonStop) : std::nullopt);

	json_importer.TryApplyModifyOrder<OrderConditionVariable>("condition", MOF_COND_VARIABLE, type == OT_CONDITIONAL ? JOIET_CRITICAL : JOIET_MAJOR);
	json_importer.TryApplyModifyOrder<OrderConditionComparator>("comparator", MOF_COND_COMPARATOR, JOIET_MAJOR);
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
					json_importer.LogError("in 'load-by-cargo-type','" + key + "' is not a valid cargo_id", JOIET_MAJOR);
					continue;
				}
				CargoType cargo_id = *cargo_res;

				if (!val.is_object()) {
					json_importer.LogError("loading options in 'load-by-cargo-type'[" + key + "] are not valid", JOIET_MAJOR);
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
				Command<CMD_ORDER_REFIT>::Post(veh->tile, veh->index, veh->GetNumOrders() - 1, CARGO_AUTO_REFIT);
			} else {
				json_importer.LogError("Value of 'refit-cargo' is invalid", JOIET_MAJOR);
			}
		} else {
			CargoType cargo_id;
			json_importer.TryGetField("refit-cargo", cargo_id, JOIET_MAJOR);
			bool success = Command<CMD_ORDER_REFIT>::Post(veh->tile, veh->index, veh->GetNumOrders() - 1, cargo_id);
			if (!success) {
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

static void ImportJsonDispatchSchedule(JSONToVehicleCommandParser json_importer)
{
	const Vehicle *veh = json_importer.GetVehicle();
	const nlohmann::json &json = json_importer.GetJson();

	AddNewScheduledDispatchSchedule(veh->index);

	if (json.is_null()) {
		return;
	}

	json_importer.TryApplySchDispatchCommand<std::string, CMD_SCH_DISPATCH_RENAME_SCHEDULE>("name", JOIET_MINOR, std::nullopt);
	json_importer.TryApplySchDispatchCommand<uint, CMD_SCH_DISPATCH_SET_DURATION>("duration", JOIET_MAJOR, json_importer.importSettings.company.default_sched_dispatch_duration);
	json_importer.TryApplySchDispatchCommand<uint, CMD_SCH_DISPATCH_SET_DELAY>("max-delay", JOIET_MAJOR, std::nullopt);
	json_importer.TryApplySchDispatchCommand<bool, CMD_SCH_DISPATCH_SET_REUSE_SLOTS>("re-use-all-slots", JOIET_MAJOR, std::nullopt);

	if (json.contains("renamed-tags") && json["renamed-tags"].is_object()) {
		for (const auto &names : json["renamed-tags"].items()) {
			int index = TagStringToIndex(names.key());

			if (index == -1 || !names.value().is_string()) {
				json_importer.LogError("'" + names.key() + "' is not a tag.", JOIET_MINOR);
			} else {
				json_importer["renamed-tags"].TryApplySchDispatchCommand<std::string, CMD_SCH_DISPATCH_RENAME_TAG>(names.key(), JOIET_MINOR, std::nullopt, index);
			}
		}
	}

	if (json.contains("slots")) {
		const auto &slotsJson = json.at("slots");
		if (slotsJson.is_object()) {
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
					Command<CMD_SCH_DISPATCH_ADD>::Post((StringID)0, veh->index, veh->orders->GetScheduledDispatchScheduleCount() - 1, offset, 0, 0);

					bool re_use_slot = false;
					local_importer.TryGetField("re-use-slot", re_use_slot, JOIET_MAJOR, true);

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
									json_importer.LogError("'" + tagString + "' is not a tag.", JOIET_MAJOR, true);
									continue;
								}
								SetBit(flags, DispatchSlot::SDSF_FIRST_TAG + tag);
							}
						}
					}

					local_importer.TryApplySchDispatchSlotFlagsCommand(JOIET_MAJOR, "Could not load slot flags", offset, flags);
				}
			}
		}
	}
}

void ImportJsonOrderList(const Vehicle *veh, std::string_view json_str)
{
	assert(veh != nullptr);

	nlohmann::json json;
	JSONToVehicleCommandParser::Errors errors = {};

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

	ClientSettings import_settings_client = _settings_client;

	/* If the json cntains game-properties, we will try to parse them and apply them */
	if (json.contains("game-properties") && json["game-properties"].is_object()) {
		const auto &game_properties = json["game-properties"];

		OrderStopLocation osl = game_properties.value<OrderStopLocation>("default-stop-location", OSL_END);
		if (osl == OSL_END) {
			errors.global_errors.push_back({ "'default-stop-location' missing or invalid in 'game-properties', this may cause discrepancies when loading the orderlist", JOIET_MAJOR });
		} else {
			import_settings_client.gui.stop_location = osl;
		}

		if (game_properties.contains("new-nonstop") && game_properties["new-nonstop"].is_boolean()) {
			bool new_nonstop = game_properties["new-nonstop"];
			if (!new_nonstop && _settings_game.order.nonstop_only) {
				errors.global_errors.push_back({ "'new-nonstop' is not compatible with the current game setting, this may cause discrepancies when loading the orderlist", JOIET_MAJOR });
			}
			import_settings_client.gui.new_nonstop = new_nonstop;
		} else {
			errors.global_errors.push_back({ "'new-nonstop' missing or invalid in 'game-properties', this may cause discrepancies when loading the orderlist", JOIET_MAJOR });
		}

		if (json.contains("schedules")) {
			if (game_properties.contains("default-scheduled-dispatch-duration") && game_properties["default-scheduled-dispatch-duration"].is_number_integer() && game_properties["default-scheduled-dispatch-duration"] > 0) {
				int def_sched_dispatch_duration = game_properties["default-scheduled-dispatch-duration"];
				import_settings_client.company.default_sched_dispatch_duration = def_sched_dispatch_duration;
			} else {
				errors.global_errors.push_back({ "'default-scheduled-dispatch-duration' missing or invalid in 'game-properties', this may cause discrepancies when loading the orderlist", JOIET_MAJOR });
			}
		}
	} else {
		errors.global_errors.push_back({ "no valid 'game-properties' found, current setings will be assumed to be correct", JOIET_MAJOR });
	}


	/* Delete all orders before setting the new orders */
	Command<CMD_DELETE_ORDER>::Post(veh->tile, veh->index, veh->GetNumOrders());

	JSONToVehicleCommandParser json_importer(veh, json, errors, import_settings_client);
	const auto &orders_json = json["orders"];

	robin_hood::unordered_map<std::string, VehicleOrderID> jump_map; // Associates jump labels to actual order-ids until all orders are added
	std::vector<VehicleOrderID> order_index;
	order_index.reserve(orders_json.size());

	VehicleOrderID orders_inserted = 0;
	for (const auto &value : orders_json) {
		auto order_importer = json_importer.WithNewJson(value);

		ImportJsonOrder(order_importer);

		const VehicleOrderID num_orders = veh->GetNumOrders();
		if (orders_inserted == num_orders) {
			/* No order was inserted. */
			order_index.push_back(INVALID_VEH_ORDER_ID);
			continue;
		}
		orders_inserted = num_orders;
		order_index.push_back(num_orders - 1);

		std::string jump_label;
		if (order_importer.TryGetField("jump-from", jump_label, JOIET_MAJOR)) {
			jump_map[jump_label] = num_orders - 1;
		};
	}

	if (json.contains("schedules") && json["schedules"].is_array()) {
		const auto &schedules = json["schedules"];
		if (schedules.is_array()) {
			for (const auto &value : schedules) {
				ImportJsonDispatchSchedule(json_importer.WithNewJson(value));
			}
		}
	}

	/* Post processing (link jumps and assign schedules) */
	size_t idx = 0;
	for (const auto &value : orders_json) {
		VehicleOrderID order_id = order_index[idx++];
		if (order_id == INVALID_VEH_ORDER_ID) continue;

		auto local_importer = json_importer.WithNewJson(value);

		local_importer.TryApplyTimetableCommand<uint64_t>("schedule-index", MTF_ASSIGN_SCHEDULE, JOIET_MAJOR, order_id);

		std::string jump_label;
		if (local_importer.TryGetField("jump-to", jump_label, JOIET_MAJOR)) {
			auto jm_iter = jump_map.find(jump_label);
			if (jm_iter != jump_map.end()) {
				local_importer.ModifyOrder(MOF_COND_DESTINATION, jm_iter->second, INVALID_CARGO, std::nullopt, order_id);
			} else {
				local_importer.LogError("Unknown jump label '" + jump_label + "'", JOIET_MAJOR, true);
			}
		}
	}
}
