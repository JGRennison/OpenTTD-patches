/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_type.h Types related to commands. */

#ifndef COMMAND_TYPE_H
#define COMMAND_TYPE_H

#include "company_type.h"
#include "economy_type.h"
#include "string_type.h"
#include "strings_type.h"
#include "tile_type.h"
#include "core/serialisation.hpp"
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>

struct GRFFile;
enum ClientID : uint32_t;

enum CommandCostIntlFlags : uint8_t {
	CCIF_NONE                     = 0,
	CCIF_SUCCESS                  = 1 << 0,
	CCIF_VALID_RESULT             = 1 << 1,

	CCIF_TYPE_MASK                = 0xF0,
};
DECLARE_ENUM_AS_BIT_SET(CommandCostIntlFlags)

/**
 * Common return value for all commands. Wraps the cost and
 * a possible error message/state together.
 */
class CommandCost {
	Money cost;                                 ///< The cost of this action
	ExpensesType expense_type;                  ///< the type of expence as shown on the finances view
	CommandCostIntlFlags flags;                 ///< Flags: see CommandCostIntlFlags
	Owner owner = INVALID_COMPANY;              ///< Originator owner of error.
	StringID message;                           ///< Warning message for when success is unset

	enum class CommandCostInlineType {
		None,
		AuxiliaryData,
		ExtraMsg,
		Tile,
		Result,
		AdditionalCash,
	};

	CommandCostInlineType GetInlineType() const { return static_cast<CommandCostInlineType>(this->flags >> 4); }

	void SetInlineType(CommandCostInlineType inl_type)
	{
		this->flags &= ~CCIF_TYPE_MASK;
		this->flags |= static_cast<CommandCostIntlFlags>(to_underlying(inl_type) << 4);
	}

	struct CommandCostAuxiliaryData {
		Money additional_cash_required = 0;
		uint32_t textref_stack[16] = {};
		const GRFFile *textref_stack_grffile = nullptr; ///< NewGRF providing the #TextRefStack content.
		uint textref_stack_size = 0;                    ///< Number of uint32_t values to put on the #TextRefStack for the error message.
		EncodedString encoded_message;                  ///< Encoded error message, used if the error message includes parameters.
		StringID extra_message = INVALID_STRING_ID;     ///< Additional warning message for when success is unset
		TileIndex tile = INVALID_TILE;
		uint32_t result = 0;
	};

	union {
		uint32_t result = 0;
		StringID extra_message;                 ///< Additional warning message for when success is unset
		uint32_t tile;
		int64_t additional_cash_required;
		CommandCostAuxiliaryData *aux_data;
	} inl;

	void AllocAuxData();
	bool AddInlineData(CommandCostInlineType inl_type);

public:
	/**
	 * Creates a command cost return with no cost and no error
	 */
	CommandCost() : cost(0), expense_type(INVALID_EXPENSES), flags(CCIF_SUCCESS), message(INVALID_STRING_ID) {}

	/**
	 * Creates a command return value the is failed with the given message
	 */
	explicit CommandCost(StringID msg) : cost(0), expense_type(INVALID_EXPENSES), flags(CCIF_NONE), message(msg) {}

	CommandCost(const CommandCost &other);
	CommandCost &operator=(const CommandCost &other);

	CommandCost(CommandCost &&other) noexcept
	{
		*this = std::move(other);
	}

	CommandCost &operator=(CommandCost &&other) noexcept
	{
		this->cost = other.cost;
		this->expense_type = other.expense_type;
		this->flags = other.flags;
		this->message = other.message;
		this->inl = other.inl;
		other.flags = CCIF_NONE; // Clear any ownership of other.inl.aux_data
		return *this;
	}

	~CommandCost()
	{
		if (this->GetInlineType() == CommandCostInlineType::AuxiliaryData) delete this->inl.aux_data;
	}

	/**
	 * Creates a command return value the is failed with the given message
	 */
	static CommandCost DualErrorMessage(StringID msg, StringID extra_msg)
	{
		CommandCost cc(msg);
		cc.SetInlineType(CommandCostInlineType::ExtraMsg);
		cc.inl.extra_message = extra_msg;
		return cc;
	}

	/**
	 * Creates a command cost with given expense type and start cost of 0
	 * @param ex_t the expense type
	 */
	explicit CommandCost(ExpensesType ex_t) : cost(0), expense_type(ex_t), flags(CCIF_SUCCESS), message(INVALID_STRING_ID) {}

	/**
	 * Creates a command return value with the given start cost and expense type
	 * @param ex_t the expense type
	 * @param cst the initial cost of this command
	 */
	CommandCost(ExpensesType ex_t, const Money &cst) : cost(cst), expense_type(ex_t), flags(CCIF_SUCCESS), message(INVALID_STRING_ID) {}

	/**
	 * Set the 'owner' (the originator) of this error message. This is used to show a company owner's face if you
	 * attempt an action on something owned by other company.
	 */
	inline void SetErrorOwner(Owner owner)
	{
		this->owner = owner;
	}

	void SetEncodedMessage(EncodedString &&message);
	EncodedString &GetEncodedMessage();

	/**
	 * Get the originator owner for this error.
	 */
	inline CompanyID GetErrorOwner() const
	{
		return this->owner;
	}

	/**
	 * Adds the given cost to the cost of the command.
	 * @param cost the cost to add
	 */
	inline void AddCost(const Money &cost)
	{
		this->cost += cost;
	}

	void AddCost(const CommandCost &cmd_cost);

	/**
	 * Multiplies the cost of the command by the given factor.
	 * @param factor factor to multiply the costs with
	 */
	inline void MultiplyCost(int factor)
	{
		this->cost *= factor;
	}

	/**
	 * The costs as made up to this moment
	 * @return the costs
	 */
	inline Money GetCost() const
	{
		return this->cost;
	}

	/**
	 * The expense type of the cost
	 * @return the expense type
	 */
	inline ExpensesType GetExpensesType() const
	{
		return this->expense_type;
	}

	/**
	 * Makes this #CommandCost behave like an error command.
	 * @param message The error message.
	 */
	void MakeError(StringID message)
	{
		assert(message != INVALID_STRING_ID);
		this->flags &= ~CCIF_SUCCESS;
		this->message = message;

		/* Cleary any extra message */
		if (this->GetInlineType() == CommandCostInlineType::ExtraMsg) {
			this->SetInlineType(CommandCostInlineType::None);
		} else if (this->GetInlineType() == CommandCostInlineType::AuxiliaryData) {
			this->inl.aux_data->extra_message = INVALID_STRING_ID;
		}
	}

	void UseTextRefStack(const GRFFile *grffile, uint num_registers);

	/**
	 * Returns the NewGRF providing the #TextRefStack of the error message.
	 * @return the NewGRF.
	 */
	const GRFFile *GetTextRefStackGRF() const
	{
		return this->GetInlineType() == CommandCostInlineType::AuxiliaryData ? this->inl.aux_data->textref_stack_grffile : 0;
	}

	/**
	 * Returns the number of uint32_t values for the #TextRefStack of the error message.
	 * @return number of uint32_t values.
	 */
	uint GetTextRefStackSize() const
	{
		return this->GetInlineType() == CommandCostInlineType::AuxiliaryData ? this->inl.aux_data->textref_stack_size : 0;
	}

	/**
	 * Returns a pointer to the values for the #TextRefStack of the error message.
	 * @return uint32_t values for the #TextRefStack
	 */
	const uint32_t *GetTextRefStack() const
	{
		return this->GetInlineType() == CommandCostInlineType::AuxiliaryData ? this->inl.aux_data->textref_stack : nullptr;
	}

	/**
	 * Returns the error message of a command
	 * @return the error message, if succeeded #INVALID_STRING_ID
	 */
	StringID GetErrorMessage() const
	{
		if (this->Succeeded()) return INVALID_STRING_ID;
		return this->message;
	}

	/**
	 * Returns the extra error message of a command
	 * @return the extra error message, if succeeded #INVALID_STRING_ID
	 */
	StringID GetExtraErrorMessage() const
	{
		if (this->Succeeded()) return INVALID_STRING_ID;
		if (this->GetInlineType() == CommandCostInlineType::ExtraMsg) {
			return this->inl.extra_message;
		} else if (this->GetInlineType() == CommandCostInlineType::AuxiliaryData) {
			return this->inl.aux_data->extra_message;
		} else {
			return INVALID_STRING_ID;
		}
	}

	/**
	 * Did this command succeed?
	 * @return true if and only if it succeeded
	 */
	inline bool Succeeded() const
	{
		return (this->flags & CCIF_SUCCESS);
	}

	/**
	 * Did this command fail?
	 * @return true if and only if it failed
	 */
	inline bool Failed() const
	{
		return !(this->flags & CCIF_SUCCESS);
	}

	/**
	 * @param cmd_msg optional failure string as passed to DoCommand
	 * @return a string summarising the command result
	 */
	std::string SummaryMessage(StringID cmd_msg = 0) const;

	bool IsSuccessWithMessage() const
	{
		return this->Succeeded() && this->message != INVALID_STRING_ID;
	}

	void MakeSuccessWithMessage()
	{
		assert(this->message != INVALID_STRING_ID);
		this->flags |= CCIF_SUCCESS;
	}

	CommandCost UnwrapSuccessWithMessage() const
	{
		assert(this->IsSuccessWithMessage());
		CommandCost res = *this;
		res.flags &= ~CCIF_SUCCESS;
		return res;
	}

	TileIndex GetTile() const
	{
		if (this->GetInlineType() == CommandCostInlineType::Tile) {
			return TileIndex(this->inl.tile);
		} else if (this->GetInlineType() == CommandCostInlineType::AuxiliaryData) {
			return this->inl.aux_data->tile;
		} else {
			return INVALID_TILE;
		}
	}

	void SetTile(TileIndex tile);

	Money GetAdditionalCashRequired() const
	{
		if (this->GetInlineType() == CommandCostInlineType::AdditionalCash) {
			return this->inl.additional_cash_required;
		} else if (this->GetInlineType() == CommandCostInlineType::AuxiliaryData) {
			return this->inl.aux_data->additional_cash_required;
		} else {
			return 0;
		}
	}

	void SetAdditionalCashRequired(Money cash);

	bool HasResultData() const
	{
		return (this->flags & CCIF_VALID_RESULT);
	}

private:
	uint32_t GetResultDataIntl() const
	{
		if (this->GetInlineType() == CommandCostInlineType::Result) {
			return this->inl.result;
		} else if (this->GetInlineType() == CommandCostInlineType::AuxiliaryData) {
			return this->inl.aux_data->result;
		} else {
			return 0;
		}
	}

public:
	template <typename T = uint32_t>
	T GetResultData() const
	{
		if constexpr (std::is_base_of_v<struct PoolIDBase, T>) {
			return T(static_cast<typename T::BaseType>(this->GetResultDataIntl()));
		} else {
			return static_cast<T>(this->GetResultDataIntl());
		}
	}

	void SetResultData(uint32_t result);

	template <typename T> requires std::is_base_of_v<struct PoolIDBase, T>
	inline void SetResultData(T result) { this->SetResultData(static_cast<uint32_t>(result.base())); }
};

CommandCost CommandCostWithParam(StringID str, uint64_t value);
CommandCost CommandCostWithParam(StringID str, StringParameterAsBase auto value) { return CommandCostWithParam(str, value.base()); }

/**
 * Define a default return value for a failed command.
 *
 * This variable contains a CommandCost object with is declared as "failed".
 * Other functions just need to return this error if there is an error,
 * which doesn't need to specific by a StringID.
 */
static const CommandCost CMD_ERROR = CommandCost(INVALID_STRING_ID);

/**
 * List of commands.
 *
 * This enum defines all possible commands which can be executed to the game
 * engine. Observing the game like the query-tool or checking the profit of a
 * vehicle don't result in a command which should be executed in the engine
 * nor send to the server in a network game.
 *
 * @see _command_proc_table
 */
enum Commands : uint8_t {
	CMD_BUILD_RAILROAD_TRACK,         ///< build a rail track
	CMD_REMOVE_RAILROAD_TRACK,        ///< remove a rail track
	CMD_BUILD_SINGLE_RAIL,            ///< build a single rail track
	CMD_REMOVE_SINGLE_RAIL,           ///< remove a single rail track
	CMD_LANDSCAPE_CLEAR,              ///< demolish a tile
	CMD_BUILD_BRIDGE,                 ///< build a bridge
	CMD_BUILD_RAIL_STATION,           ///< build a rail station
	CMD_BUILD_TRAIN_DEPOT,            ///< build a train depot
	CMD_BUILD_SINGLE_SIGNAL,          ///< build a signal
	CMD_REMOVE_SINGLE_SIGNAL,         ///< remove a signal
	CMD_TERRAFORM_LAND,               ///< terraform a tile
	CMD_BUILD_OBJECT,                 ///< build an object
	CMD_PURCHASE_LAND_AREA,           ///< purchase an area of landscape
	CMD_BUILD_OBJECT_AREA,            ///< build an area of objects
	CMD_BUILD_TUNNEL,                 ///< build a tunnel

	CMD_REMOVE_FROM_RAIL_STATION,     ///< remove a (rectangle of) tiles from a rail station
	CMD_CONVERT_RAIL,                 ///< convert a rail type
	CMD_CONVERT_RAIL_TRACK,           ///< convert a rail type (track)

	CMD_BUILD_RAIL_WAYPOINT,          ///< build a waypoint
	CMD_RENAME_WAYPOINT,              ///< rename a waypoint
	CMD_REMOVE_FROM_RAIL_WAYPOINT,    ///< remove a (rectangle of) tiles from a rail waypoint

	CMD_BUILD_ROAD_WAYPOINT,          ///< build a road waypoint
	CMD_REMOVE_FROM_ROAD_WAYPOINT,    ///< remove a (rectangle of) tiles from a road waypoint

	CMD_SET_WAYPOINT_LABEL_HIDDEN,    ///< set whether waypoint label is hidden
	CMD_EXCHANGE_WAYPOINT_NAMES,      ///< exchange waypoint names

	CMD_BUILD_ROAD_STOP,              ///< build a road stop
	CMD_REMOVE_ROAD_STOP,             ///< remove a road stop
	CMD_BUILD_LONG_ROAD,              ///< build a complete road (not a "half" one)
	CMD_REMOVE_LONG_ROAD,             ///< remove a complete road (not a "half" one)
	CMD_BUILD_ROAD,                   ///< build a "half" road
	CMD_BUILD_ROAD_DEPOT,             ///< build a road depot
	CMD_CONVERT_ROAD,                 ///< convert a road type

	CMD_BUILD_AIRPORT,                ///< build an airport

	CMD_BUILD_DOCK,                   ///< build a dock

	CMD_BUILD_SHIP_DEPOT,             ///< build a ship depot
	CMD_BUILD_BUOY,                   ///< build a buoy

	CMD_PLANT_TREE,                   ///< plant a tree

	CMD_BUILD_VEHICLE,                ///< build a vehicle
	CMD_SELL_VEHICLE,                 ///< sell a vehicle
	CMD_REFIT_VEHICLE,                ///< refit the cargo space of a vehicle
	CMD_SEND_VEHICLE_TO_DEPOT,        ///< send a vehicle to a depot
	CMD_MASS_SEND_VEHICLE_TO_DEPOT,   ///< mass send vehicles to depots
	CMD_SET_VEHICLE_VISIBILITY,       ///< hide or unhide a vehicle in the build vehicle and autoreplace GUIs

	CMD_MOVE_RAIL_VEHICLE,            ///< move a rail vehicle (in the depot)
	CMD_FORCE_TRAIN_PROCEED,          ///< proceed a train to pass a red signal
	CMD_REVERSE_TRAIN_DIRECTION,      ///< turn a train around

	CMD_CLEAR_ORDER_BACKUP,           ///< clear the order backup of a given user/tile
	CMD_MODIFY_ORDER,                 ///< modify an order (like set full-load)
	CMD_SKIP_TO_ORDER,                ///< skip an order to the next of specific one
	CMD_DELETE_ORDER,                 ///< delete an order
	CMD_INSERT_ORDER,                 ///< insert a new order
	CMD_DUPLICATE_ORDER,              ///< duplicate an order
	CMD_MASS_CHANGE_ORDER,            ///< mass change the target of an order

	CMD_CHANGE_SERVICE_INT,           ///< change the service interval of a vehicle

	CMD_BUILD_INDUSTRY,               ///< build a new industry
	CMD_INDUSTRY_SET_FLAGS,           ///< change industry control flags
	CMD_INDUSTRY_SET_EXCLUSIVITY,     ///< change industry exclusive consumer/supplier
	CMD_INDUSTRY_SET_TEXT,            ///< change additional text for the industry
	CMD_INDUSTRY_SET_PRODUCTION,      ///< change industry production

	CMD_SET_COMPANY_MANAGER_FACE,     ///< set the manager's face of the company
	CMD_SET_COMPANY_COLOUR,           ///< set the colour of the company

	CMD_INCREASE_LOAN,                ///< increase the loan from the bank
	CMD_DECREASE_LOAN,                ///< decrease the loan from the bank
	CMD_SET_COMPANY_MAX_LOAN,         ///< sets the max loan for the company

	CMD_WANT_ENGINE_PREVIEW,          ///< confirm the preview of an engine
	CMD_ENGINE_CTRL,                  ///< control availability of the engine for companies

	CMD_RENAME_VEHICLE,               ///< rename a whole vehicle
	CMD_RENAME_ENGINE,                ///< rename a engine (in the engine list)
	CMD_RENAME_COMPANY,               ///< change the company name
	CMD_RENAME_PRESIDENT,             ///< change the president name
	CMD_RENAME_STATION,               ///< rename a station
	CMD_RENAME_DEPOT,                 ///< rename a depot
	CMD_EXCHANGE_STATION_NAMES,       ///< exchange station names
	CMD_SET_STATION_CARGO_ALLOWED_SUPPLY, ///< set station cargo allowed supply

	CMD_PLACE_SIGN,                   ///< place a sign
	CMD_RENAME_SIGN,                  ///< rename a sign

	CMD_TURN_ROADVEH,                 ///< turn a road vehicle around

	CMD_PAUSE,                        ///< pause the game

	CMD_BUY_SHARE_IN_COMPANY,         ///< buy a share from a company
	CMD_SELL_SHARE_IN_COMPANY,        ///< sell a share from a company
	CMD_BUY_COMPANY,                  ///< buy a company which is bankrupt
	CMD_DECLINE_BUY_COMPANY,          ///< decline to buy a company which is bankrupt

	CMD_FOUND_TOWN,                   ///< found a town
	CMD_RENAME_TOWN,                  ///< rename a town
	CMD_RENAME_TOWN_NON_ADMIN,        ///< rename a town, non-admin command
	CMD_DO_TOWN_ACTION,               ///< do a action from the town detail window (like advertises or bribe)
	CMD_TOWN_SETTING_OVERRIDE,        ///< override a town setting
	CMD_TOWN_SETTING_OVERRIDE_NON_ADMIN, ///< override a town setting, non-admin command
	CMD_TOWN_CARGO_GOAL,              ///< set the goal of a cargo for a town
	CMD_TOWN_GROWTH_RATE,             ///< set the town growth rate
	CMD_TOWN_RATING,                  ///< set rating of a company in a town
	CMD_TOWN_SET_TEXT,                ///< set the custom text of a town
	CMD_EXPAND_TOWN,                  ///< expand a town
	CMD_DELETE_TOWN,                  ///< delete a town
	CMD_PLACE_HOUSE,                  ///< place a house

	CMD_ORDER_REFIT,                  ///< change the refit information of an order (for "goto depot" )
	CMD_CLONE_ORDER,                  ///< clone (and share) an order
	CMD_CLEAR_AREA,                   ///< clear an area

	CMD_MONEY_CHEAT,                  ///< do the money cheat
	CMD_MONEY_CHEAT_ADMIN,            ///< do the money cheat (admin mode)
	CMD_CHANGE_BANK_BALANCE,          ///< change bank balance to charge costs or give money from a GS
	CMD_CHEAT_SETTING,                ///< change a cheat setting
	CMD_BUILD_CANAL,                  ///< build a canal

	CMD_CREATE_SUBSIDY,               ///< create a new subsidy
	CMD_COMPANY_CTRL,                 ///< used in multiplayer to create a new companies etc.
	CMD_COMPANY_ALLOW_LIST_CTRL,      ///< Used in multiplayer to add/remove a client's public key to/from the company's allow list.
	CMD_CUSTOM_NEWS_ITEM,             ///< create a custom news message
	CMD_CREATE_GOAL,                  ///< create a new goal
	CMD_REMOVE_GOAL,                  ///< remove a goal
	CMD_SET_GOAL_DESTINATION,         ///< update goal destination of a goal
	CMD_SET_GOAL_TEXT,                ///< update goal text of a goal
	CMD_SET_GOAL_PROGRESS,            ///< update goal progress text of a goal
	CMD_SET_GOAL_COMPLETED,           ///< update goal completed status of a goal
	CMD_GOAL_QUESTION,                ///< ask a goal related question
	CMD_GOAL_QUESTION_ANSWER,         ///< answer(s) to CMD_GOAL_QUESTION
	CMD_CREATE_STORY_PAGE,            ///< create a new story page
	CMD_CREATE_STORY_PAGE_ELEMENT,    ///< create a new story page element
	CMD_UPDATE_STORY_PAGE_ELEMENT,    ///< update a story page element
	CMD_SET_STORY_PAGE_TITLE,         ///< update title of a story page
	CMD_SET_STORY_PAGE_DATE,          ///< update date of a story page
	CMD_SHOW_STORY_PAGE,              ///< show a story page
	CMD_REMOVE_STORY_PAGE,            ///< remove a story page
	CMD_REMOVE_STORY_PAGE_ELEMENT,    ///< remove a story page element
	CMD_SCROLL_VIEWPORT,              ///< scroll main viewport of players
	CMD_STORY_PAGE_BUTTON,            ///< selection via story page button

	CMD_LEVEL_LAND,                   ///< level land

	CMD_BUILD_LOCK,                   ///< build a lock

	CMD_BUILD_SIGNAL_TRACK,           ///< add signals along a track (by dragging)
	CMD_REMOVE_SIGNAL_TRACK,          ///< remove signals along a track (by dragging)

	CMD_GIVE_MONEY,                   ///< give money to another company
	CMD_CHANGE_SETTING,               ///< change a setting
	CMD_CHANGE_COMPANY_SETTING,       ///< change a company setting

	CMD_SET_AUTOREPLACE,              ///< set an autoreplace entry

	CMD_CHANGE_TEMPLATE_FLAG,         ///< change template flag
	CMD_RENAME_TEMPLATE,              ///< rename a template

	CMD_VIRTUAL_TRAIN_FROM_TEMPLATE,  ///< Creates a virtual train from a template
	CMD_VIRTUAL_TRAIN_FROM_TRAIN,     ///< Creates a virtual train from a regular train
	CMD_DELETE_VIRTUAL_TRAIN,         ///< Delete a virtual train
	CMD_BUILD_VIRTUAL_RAIL_VEHICLE,   ///< Build a virtual train
	CMD_REPLACE_TEMPLATE,             ///< Replace a template vehicle with another one based on a virtual train
	CMD_MOVE_VIRTUAL_RAIL_VEHICLE,    ///< Move a virtual rail vehicle
	CMD_SELL_VIRTUAL_VEHICLE,         ///< Sell a virtual vehicle

	CMD_CLONE_TEMPLATE_FROM_TRAIN,    ///< clone a train and create a new template vehicle based on it
	CMD_DELETE_TEMPLATE_VEHICLE,      ///< delete a template vehicle

	CMD_ISSUE_TEMPLATE_REPLACEMENT,   ///< issue a template replacement for a vehicle group
	CMD_DELETE_TEMPLATE_REPLACEMENT,  ///< delete a template replacement from a vehicle group

	CMD_CLONE_VEHICLE,                ///< clone a vehicle
	CMD_CLONE_VEHICLE_FROM_TEMPLATE,  ///< clone a vehicle from a template
	CMD_START_STOP_VEHICLE,           ///< start or stop a vehicle
	CMD_MASS_START_STOP,              ///< start/stop all vehicles (in a depot)
	CMD_AUTOREPLACE_VEHICLE,          ///< replace/renew a vehicle while it is in a depot
	CMD_TEMPLATE_REPLACE_VEHICLE,     ///< template replace a vehicle while it is in a depot
	CMD_DEPOT_SELL_ALL_VEHICLES,      ///< sell all vehicles which are in a given depot
	CMD_DEPOT_MASS_AUTOREPLACE,       ///< force the autoreplace to take action in a given depot
	CMD_SET_TRAIN_SPEED_RESTRICTION,  ///< manually set train speed restriction

	CMD_CREATE_GROUP,                 ///< create a new group
	CMD_DELETE_GROUP,                 ///< delete a group
	CMD_ALTER_GROUP,                  ///< alter a group
	CMD_CREATE_GROUP_FROM_LIST,       ///< create and rename a new group from a vehicle list
	CMD_ADD_VEHICLE_GROUP,            ///< add a vehicle to a group
	CMD_ADD_SHARED_VEHICLE_GROUP,     ///< add all other shared vehicles to a group which are missing
	CMD_REMOVE_ALL_VEHICLES_GROUP,    ///< remove all vehicles from a group
	CMD_SET_GROUP_FLAG,               ///< set/clear a flag for a group
	CMD_SET_GROUP_LIVERY,             ///< set the livery for a group

	CMD_MOVE_ORDER,                   ///< move an order
	CMD_REVERSE_ORDER_LIST,           ///< reverse order list
	CMD_CHANGE_TIMETABLE,             ///< change the timetable for a vehicle
	CMD_BULK_CHANGE_TIMETABLE,        ///< change the timetable for all orders of a vehicle
	CMD_SET_VEHICLE_ON_TIME,          ///< set the vehicle on time feature (timetable)
	CMD_AUTOFILL_TIMETABLE,           ///< autofill the timetable
	CMD_AUTOMATE_TIMETABLE,           ///< automate the timetable
	CMD_TIMETABLE_SEPARATION,         ///< auto timetable separation
	CMD_SET_TIMETABLE_START,          ///< set the date that a timetable should start

	CMD_OPEN_CLOSE_AIRPORT,           ///< open/close an airport to incoming aircraft

	CMD_CREATE_LEAGUE_TABLE,               ///< create a new league table
	CMD_CREATE_LEAGUE_TABLE_ELEMENT,       ///< create a new element in a league table
	CMD_UPDATE_LEAGUE_TABLE_ELEMENT_DATA,  ///< update the data fields of a league table element
	CMD_UPDATE_LEAGUE_TABLE_ELEMENT_SCORE, ///< update the score of a league table element
	CMD_REMOVE_LEAGUE_TABLE_ELEMENT,       ///< remove a league table element

	CMD_PROGRAM_TRACERESTRICT_SIGNAL, ///< modify a signal tracerestrict program
	CMD_MANAGE_TRACERESTRICT_SIGNAL,  ///< modify a signal tracerestrict program (management)
	CMD_CREATE_TRACERESTRICT_SLOT,    ///< create a tracerestrict slot
	CMD_ALTER_TRACERESTRICT_SLOT,     ///< alter a tracerestrict slot
	CMD_DELETE_TRACERESTRICT_SLOT,    ///< delete a tracerestrict slot
	CMD_ADD_VEHICLE_TRACERESTRICT_SLOT,    ///< add a vehicle to a tracerestrict slot
	CMD_REMOVE_VEHICLE_TRACERESTRICT_SLOT, ///< remove a vehicle from a tracerestrict slot
	CMD_CREATE_TRACERESTRICT_SLOT_GROUP,   ///< create a tracerestrict slot group
	CMD_ALTER_TRACERESTRICT_SLOT_GROUP,    ///< alter a tracerestrict slot group
	CMD_DELETE_TRACERESTRICT_SLOT_GROUP,   ///< delete a tracerestrict slot group
	CMD_CREATE_TRACERESTRICT_COUNTER, ///< create a tracerestrict counter
	CMD_ALTER_TRACERESTRICT_COUNTER,  ///< alter a tracerestrict counter
	CMD_DELETE_TRACERESTRICT_COUNTER, ///< delete a tracerestrict counter

	CMD_PROGPRESIG_INSERT_INSTRUCTION,          ///< insert a signal instruction
	CMD_PROGPRESIG_MODIFY_INSTRUCTION,          ///< modifies a signal instruction
	CMD_PROGPRESIG_REMOVE_INSTRUCTION,          ///< removes a signal instruction
	CMD_PROGPRESIG_PROGRAM_MGMT,                ///< signal program management command

	CMD_SCH_DISPATCH,                           ///< scheduled dispatch start
	CMD_SCH_DISPATCH_ADD,                       ///< scheduled dispatch add
	CMD_SCH_DISPATCH_REMOVE,                    ///< scheduled dispatch remove
	CMD_SCH_DISPATCH_SET_DURATION,              ///< scheduled dispatch set schedule duration
	CMD_SCH_DISPATCH_SET_START_DATE,            ///< scheduled dispatch set start date
	CMD_SCH_DISPATCH_SET_DELAY,                 ///< scheduled dispatch set maximum allow delay
	CMD_SCH_DISPATCH_SET_REUSE_SLOTS,           ///< scheduled dispatch set whether to re-use dispatch slots
	CMD_SCH_DISPATCH_RESET_LAST_DISPATCH,       ///< scheduled dispatch reset last dispatch date
	CMD_SCH_DISPATCH_CLEAR,                     ///< scheduled dispatch clear schedule
	CMD_SCH_DISPATCH_ADD_NEW_SCHEDULE,          ///< scheduled dispatch add new schedule
	CMD_SCH_DISPATCH_REMOVE_SCHEDULE,           ///< scheduled dispatch remove schedule
	CMD_SCH_DISPATCH_RENAME_SCHEDULE,           ///< scheduled dispatch rename schedule
	CMD_SCH_DISPATCH_DUPLICATE_SCHEDULE,        ///< scheduled dispatch duplicate schedule
	CMD_SCH_DISPATCH_APPEND_VEH_SCHEDULE,       ///< scheduled dispatch append schedules from another vehicle
	CMD_SCH_DISPATCH_ADJUST,                    ///< scheduled dispatch adjust time offsets in schedule
	CMD_SCH_DISPATCH_ADJUST_SLOT,               ///< scheduled dispatch adjust time offset of single slot in schedule
	CMD_SCH_DISPATCH_SWAP_SCHEDULES,            ///< scheduled dispatch swap schedules in order
	CMD_SCH_DISPATCH_SET_SLOT_FLAGS,            ///< scheduled dispatch set flags of dispatch slot
	CMD_SCH_DISPATCH_RENAME_TAG,                ///< scheduled dispatch rename departure tag

	CMD_ADD_PLAN,
	CMD_ADD_PLAN_LINE,
	CMD_REMOVE_PLAN,
	CMD_REMOVE_PLAN_LINE,
	CMD_CHANGE_PLAN_VISIBILITY,
	CMD_CHANGE_PLAN_COLOUR,
	CMD_RENAME_PLAN,
	CMD_ACQUIRE_UNOWNED_PLAN,

	CMD_DESYNC_CHECK,                 ///< Force desync checks to be run

	CMD_END,                          ///< Must ALWAYS be on the end of this list!! (period)
};

/*** All command callbacks that exist ***/

enum class CommandCallback : uint8_t {
	None, ///< No callback

	/* ai/ai_instance.cpp */
	AI,

	/* airport_gui.cpp */
	BuildAirport,

	/* bridge_gui.cpp */
	BuildBridge,

	/* dock_gui.cpp */
	BuildDocks,
	PlaySound_CONSTRUCTION_WATER,

	/* depot_gui.cpp */
	CloneVehicle,

	/* game/game_instance.cpp */
	Game,

	/* group_gui.cpp */
	CreateGroup,
	AddVehicleNewGroup,

	/* industry_gui.cpp */
	BuildIndustry,

	/* main_gui.cpp */
	PlaySound_EXPLOSION,
	PlaceSign,
	Terraform,
	GiveMoney,

	/* plans_gui.cpp */
	AddPlan,

	/* rail_gui.cpp */
	PlaySound_CONSTRUCTION_RAIL,
	RailDepot,
	Station,
	BuildRailTunnel,

	/* road_gui.cpp */
	PlaySound_CONSTRUCTION_OTHER,
	BuildRoadTunnel,
	RoadDepot,
	RoadStop,

	/* train_gui.cpp */
	BuildWagon,

	/* town_gui.cpp */
	FoundTown,
	FoundRandomTown,

	/* vehicle_gui.cpp */
	BuildPrimaryVehicle,
	StartStopVehicle,

	/* tbtr_template_gui_create.cpp */
	SetVirtualTrain,
	VirtualTrainWagonsMoved,
	DeleteVirtualTrain,

	/* build_vehicle_gui.cpp */
	AddVirtualEngine,
	MoveNewVirtualEngine,

	/* schdispatch_gui.cpp */
	AddNewSchDispatchSchedule,
	SwapSchDispatchSchedules,
	AdjustSchDispatchSlot,

	/* tracerestrict_gui.cpp */
	CreateTraceRestrictSlot,
	CreateTraceRestrictCounter,

	End, ///< Must ALWAYS be on the end of this list
};

using CallbackParameter = uint32_t;

/** Defines the traits of a command. */
template <Commands Tcmd> struct CommandTraits;
template <Commands Tcmd> struct CommandHandlerTraits;

/**
 * List of flags for a command.
 *
 * This enums defines some flags which can be used for the commands.
 */
enum class DoCommandFlag : uint8_t {
	Execute,              ///< execute the given command
	Auto,                 ///< don't allow building on structures
	QueryCost,            ///< query cost only,  don't build.
	NoWater,              ///< don't allow building on water
	NoTestTownRating,     ///< town rating does not disallow you from building
	Bankrupt,             ///< company bankrupts, skip money check, skip vehicle on tile check in some cases
	AutoReplace,          ///< autoreplace/autorenew is in progress, this shall disable vehicle limits when building, and ignore certain restrictions when undoing things (like vehicle attach callback)
	NoCargoCapacityCheck, ///< when autoreplace/autorenew is in progress, this shall prevent truncating the amount of cargo in the vehicle to prevent testing the command to remove cargo
	AllTiles,             ///< allow this command also on MP_VOID tiles
	NoModifyTownRating,   ///< do not change town rating
	ForceClearTile,       ///< do not only remove the object on the tile, but also clear any water left on it
	AllowRemoveWater,     ///< always allow removing water
	Town,                 ///< town operation
};
using DoCommandFlags = EnumBitSet<DoCommandFlag, uint16_t>;

enum DoCommandIntlFlag : uint8_t {
	DCIF_NONE                = 0x0, ///< no flag is set
	DCIF_TYPE_CHECKED        = 0x1, ///< payload type has been checked
	DCIF_NETWORK_COMMAND     = 0x2, ///< execute the command without sending it on the network
	DCIF_NOT_MY_CMD          = 0x4, ///< not my own DoCommandP
};
DECLARE_ENUM_AS_BIT_SET(DoCommandIntlFlag)

/**
 * Used to combine a StringID with the command.
 *
 * This macro can be used to add a StringID (the error message to show) on a command-id
 * (CMD_xxx). Use the binary or-operator "|" to combine the command with the result from
 * this macro.
 *
 * @param x The StringID to combine with a command-id
 */
#define CMD_MSG(x) ((x) << 16)

/**
 * Command flags for the command table _command_proc_table.
 *
 * This enumeration defines flags for the _command_proc_table.
 */
enum class CommandFlag : uint8_t {
	Server,    ///< the command can only be initiated by the server
	Spectator, ///< the command may be initiated by a spectator
	Offline,   ///< the command cannot be executed in a multiplayer game; single-player only
	Auto,      ///< set the DoCommandFlag::Auto flag on this command
	AllTiles,  ///< allow this command also on MP_VOID tiles
	NoTest,    ///< the command's output may differ between test and execute due to town rating changes etc.
	NoWater,   ///< set the DoCommandFlag::NoWater flag on this command
	ClientID,  ///< set p2 with the ClientID of the sending client.
	Deity,     ///< the command may be executed by COMPANY_DEITY
	StrCtrl,   ///< the command's string may contain control strings
	NoEst,     ///< the command is never estimated.
	ServerNS,  ///< the command can only be initiated by the server (this is not executed in spectator mode).
	LogAux,    ///< the command should be logged in the auxiliary log instead of the main log.
};
using CommandFlags = EnumBitSet<CommandFlag, uint16_t>;

static constexpr CommandFlags CMD_SERVER{CommandFlag::Server};
static constexpr CommandFlags CMD_SPECTATOR{CommandFlag::Spectator};
static constexpr CommandFlags CMD_OFFLINE{CommandFlag::Offline};
static constexpr CommandFlags CMD_AUTO{CommandFlag::Auto};
static constexpr CommandFlags CMD_ALL_TILES{CommandFlag::AllTiles};
static constexpr CommandFlags CMD_NO_TEST{CommandFlag::NoTest};
static constexpr CommandFlags CMD_NO_WATER{CommandFlag::NoWater};
static constexpr CommandFlags CMD_CLIENT_ID{CommandFlag::ClientID};
static constexpr CommandFlags CMD_DEITY{CommandFlag::Deity};
static constexpr CommandFlags CMD_STR_CTRL{CommandFlag::StrCtrl};
static constexpr CommandFlags CMD_NO_EST{CommandFlag::NoEst};
static constexpr CommandFlags CMD_SERVER_NS{CommandFlag::ServerNS};
static constexpr CommandFlags CMD_LOG_AUX{CommandFlag::LogAux};

/** Types of commands we have. */
enum CommandType : uint8_t {
	CMDT_LANDSCAPE_CONSTRUCTION, ///< Construction and destruction of objects on the map.
	CMDT_VEHICLE_CONSTRUCTION,   ///< Construction, modification (incl. refit) and destruction of vehicles.
	CMDT_MONEY_MANAGEMENT,       ///< Management of money, i.e. loans and shares.
	CMDT_VEHICLE_MANAGEMENT,     ///< Stopping, starting, sending to depot, turning around, replace orders etc.
	CMDT_ROUTE_MANAGEMENT,       ///< Modifications to route management (orders, groups, etc).
	CMDT_OTHER_MANAGEMENT,       ///< Renaming stuff, changing company colours, placing signs, etc.
	CMDT_COMPANY_SETTING,        ///< Changing settings related to a company.
	CMDT_SERVER_SETTING,         ///< Pausing/removing companies/server settings.
	CMDT_CHEAT,                  ///< A cheat of some sorts.

	CMDT_END,                    ///< Magic end marker.
};

/** Different command pause levels. */
enum CommandPauseLevel : uint8_t {
	CMDPL_NO_ACTIONS,      ///< No user actions may be executed.
	CMDPL_NO_CONSTRUCTION, ///< No construction actions may be executed.
	CMDPL_NO_LANDSCAPING,  ///< No landscaping actions may be executed.
	CMDPL_ALL_ACTIONS,     ///< All actions may be executed.
};

/**
 * Abstract base type for command payloads.
 *
 * Implementing types should:
 * - Be final.
 * - Have a deserialisation function of the form below, which returns true on success:
 *   bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
 * - Have a FormatDebugSummary implementation where even remotely useful.
 * - Have a `ClientID &GetClientIDField()` function if used by commands with CMD_CLIENT_ID/CommandFlag::ClientID.
 */
struct CommandPayloadBase {
	virtual ~CommandPayloadBase() {}

	virtual std::unique_ptr<CommandPayloadBase> Clone() const = 0;

	virtual void Serialise(struct BufferSerialisationRef buffer) const = 0;

	virtual void SanitiseStrings(StringValidationSettings settings) {}

	/* FormatDebugSummary may be called when populating the crash log so should not allocate */
	virtual void FormatDebugSummary(struct format_target &) const {}

	inline void fmt_format_value(struct format_target &output) const
	{
		this->FormatDebugSummary(output);
	}
};

template <typename T>
struct CommandPayloadSerialisable : public CommandPayloadBase {
	std::unique_ptr<CommandPayloadBase> Clone() const override;
};

template <typename T>
std::unique_ptr<CommandPayloadBase> CommandPayloadSerialisable<T>::Clone() const
{
	static_assert(std::is_final_v<T>);
	return std::make_unique<T>(*static_cast<const T *>(this));
}

struct CommandPayloadSerialised final {
	std::vector<uint8_t> serialised_data;

	void Serialise(BufferSerialisationRef buffer) const { buffer.Send_binary(this->serialised_data.data(), this->serialised_data.size()); }
};

void SetPreCheckedCommandPayloadClientID(Commands cmd, CommandPayloadBase &payload, ClientID client_id);

template <typename T>
void SetCommandPayloadClientID(T &payload, ClientID client_id)
{
	if constexpr (requires { payload.GetClientIDField(); }) {
		if (payload.GetClientIDField() == (ClientID)0) payload.GetClientIDField() = client_id;
	} else {
		constexpr size_t idx = GetTupleIndexIgnoreCvRef<ClientID, decltype(payload.GetValues())>();
		static_assert(idx < std::tuple_size_v<std::remove_cvref_t<decltype(payload.GetValues())>>,
				"There must be exactly one ClientID value in the command payload tuple unless a GetClientIDField method is present");
		if (std::get<idx>(payload.GetValues()) == (ClientID)0) std::get<idx>(payload.GetValues()) = client_id;
	}
}

struct CommandProcTupleAdapter {
	template <typename T>
	using replace_string_t = std::conditional_t<std::is_same_v<std::string, T>, const std::string &, T>;
};

struct BaseTupleCmdDataTag{};

namespace TupleCmdDataDetail {
	/**
	 * For internal use by TupleCmdData, AutoFmtTupleCmdData.
	 */
	template <typename... T>
	struct EMPTY_BASES BaseTupleCmdData : public CommandPayloadBase, public BaseTupleCmdDataTag {
		using CommandProc = CommandCost(DoCommandFlags, TileIndex, typename CommandProcTupleAdapter::replace_string_t<T>...);
		using CommandProcNoTile = CommandCost(DoCommandFlags, typename CommandProcTupleAdapter::replace_string_t<T>...);
		using Tuple = std::tuple<T...>;
		Tuple values;

		template <typename... Args>
		BaseTupleCmdData(Args&& ... args) : values(std::forward<Args>(args)...) {}

		BaseTupleCmdData(Tuple&& values) : values(std::move(values)) {}

		virtual void Serialise(BufferSerialisationRef buffer) const override;
		virtual void SanitiseStrings(StringValidationSettings settings) override;
		bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);

		Tuple &GetValues() { return this->values; }
		const Tuple &GetValues() const { return this->values; }
	};
};

template <typename Parent, typename... T>
struct TupleCmdData : public TupleCmdDataDetail::BaseTupleCmdData<T...> {
	using TupleCmdDataDetail::BaseTupleCmdData<T...>::BaseTupleCmdData;
	using Tuple = typename TupleCmdDataDetail::BaseTupleCmdData<T...>::Tuple;

	std::unique_ptr<CommandPayloadBase> Clone() const override;

	static Parent Make(T... args)
	{
		Parent out;
		out.values = Tuple(std::forward<T>(args)...);
		return out;
	}
};

template <typename Parent, typename... T>
std::unique_ptr<CommandPayloadBase> TupleCmdData<Parent, T...>::Clone() const
{
	static_assert(std::is_final_v<Parent>);
	return std::make_unique<Parent>(*static_cast<const Parent *>(this));
}

enum TupleCmdDataFlags : uint8_t {
	TCDF_NONE    =  0x0, ///< no flags
	TCDF_STRINGS =  0x1, ///< include strings in summary
};
DECLARE_ENUM_AS_BIT_SET(TupleCmdDataFlags)

template <typename Parent, TupleCmdDataFlags flags, typename... T>
struct AutoFmtTupleCmdData : public TupleCmdData<Parent, T...> {
	using TupleCmdData<Parent, T...>::TupleCmdData;
	static inline constexpr const char fmt_str[] = "";

	void FormatDebugSummary(struct format_target &output) const override;
};

template <typename Parent, typename T>
struct EMPTY_BASES TupleRefCmdData : public CommandPayloadSerialisable<Parent>, public T, public BaseTupleCmdDataTag {
private:
	template <typename H> struct TupleHelper;

	template <typename... Targs>
	struct TupleHelper<std::tuple<Targs...>> {
		using CommandProc = CommandCost(DoCommandFlags, TileIndex, typename CommandProcTupleAdapter::replace_string_t<std::remove_cvref_t<Targs>>...);
		using CommandProcNoTile = CommandCost(DoCommandFlags, typename CommandProcTupleAdapter::replace_string_t<std::remove_cvref_t<Targs>>...);
		using ValueTuple = std::tuple<std::remove_cvref_t<Targs>...>;
		using ConstRefTuple = std::tuple<const std::remove_reference_t<Targs> &...>;

		static_assert((std::is_lvalue_reference_v<Targs> && ...));
	};
	using Helper = TupleHelper<decltype(std::declval<T>().GetRefTuple())>;

public:
	using Tuple = typename Helper::ValueTuple;
	using CommandProc = typename Helper::CommandProc;
	using CommandProcNoTile = typename Helper::CommandProcNoTile;

private:
	template <typename H> struct MakeHelper;

	template <typename... Targs>
	struct MakeHelper<std::tuple<Targs...>> {
		Parent operator()(Targs... args) const
		{
			Parent out;
			out.T::GetRefTuple() = std::forward_as_tuple(args...);
			return out;
		}
	};

public:
	static inline constexpr MakeHelper<Tuple> Make{};

	virtual void Serialise(BufferSerialisationRef buffer) const override;
	virtual void SanitiseStrings(StringValidationSettings settings) override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);

	auto GetValues() { return this->T::GetRefTuple(); }
	typename Helper::ConstRefTuple GetValues() const { return typename Helper::ConstRefTuple(const_cast<TupleRefCmdData *>(this)->GetValues()); }
};

/** Wrapper for commands to handle the most common case where no custom/special behaviour is required. */
template <typename... T>
struct EMPTY_BASES CmdDataT final : public AutoFmtTupleCmdData<CmdDataT<T...>, TCDF_NONE, T...> {};

/** Specialisation for string which doesn't bother implementing FormatDebugSummary at all. */
template <>
struct EMPTY_BASES CmdDataT<std::string> final : public TupleCmdData<CmdDataT<std::string>, std::string> {};
template <>
struct EMPTY_BASES CmdDataT<std::string, std::string> final : public TupleCmdData<CmdDataT<std::string, std::string>, std::string, std::string> {};
template <>
struct EMPTY_BASES CmdDataT<std::string, std::string, std::string> final : public TupleCmdData<CmdDataT<std::string, std::string, std::string>, std::string, std::string, std::string> {};

template <>
struct EMPTY_BASES CmdDataT<> final : public CommandPayloadSerialisable<CmdDataT<>>, public BaseTupleCmdDataTag {
	using CommandProc = CommandCost(DoCommandFlags, TileIndex);
	using CommandProcNoTile = CommandCost(DoCommandFlags);
	using Tuple = std::tuple<>;

	Tuple GetValues() const { return {}; }
	void Serialise(BufferSerialisationRef buffer) const override {}
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation) { return true; }
	static inline CmdDataT<> Make() { return CmdDataT<>{}; }
};
using EmptyCmdData = CmdDataT<>;

template <Commands Tcmd>
struct BaseCommandContainer {
	static inline constexpr Commands cmd = Tcmd;
	StringID error_msg{};                                ///< error message
	TileIndex tile{};                                    ///< tile command being executed on.
	typename CommandTraits<Tcmd>::PayloadType payload{}; ///< payload

	BaseCommandContainer() = default;
	BaseCommandContainer(StringID error_msg, TileIndex tile, typename CommandTraits<Tcmd>::PayloadType payload)
			: error_msg(error_msg), tile(tile), payload(std::move(payload)) {}
};

template <Commands Tcmd>
struct CommandContainer : public BaseCommandContainer<Tcmd> {
	CommandCallback callback = CommandCallback::None;    ///< any callback function executed upon successful completion of the command.
	CallbackParameter callback_param{};                  ///< callback function parameter.

	CommandContainer() = default;
	CommandContainer(StringID error_msg, TileIndex tile, typename CommandTraits<Tcmd>::PayloadType payload, CommandCallback callback = CommandCallback::None, CallbackParameter callback_param = 0)
			: BaseCommandContainer<Tcmd>(error_msg, tile, std::move(payload)), callback(callback), callback_param(callback_param) {}
};

struct SerialisedBaseCommandContainer {
	Commands cmd{};                              ///< command being executed.
	StringID error_msg{};                        ///< error message
	TileIndex tile{};                            ///< tile command being executed on.
	CommandPayloadSerialised payload{};          ///< serialised payload

	void Serialise(BufferSerialisationRef buffer) const;
};

struct DynBaseCommandContainer {
	Commands cmd{};                              ///< command being executed.
	StringID error_msg{};                        ///< error message
	TileIndex tile{};                            ///< tile command being executed on.
	std::unique_ptr<CommandPayloadBase> payload; ///< payload

	DynBaseCommandContainer() = default;
	DynBaseCommandContainer(Commands cmd, StringID error_msg, TileIndex tile, std::unique_ptr<CommandPayloadBase> payload)
			: cmd(cmd), error_msg(error_msg), tile(tile), payload(std::move(payload)) {}

	template <Commands Tcmd>
	DynBaseCommandContainer(const BaseCommandContainer<Tcmd> &src) : cmd(Tcmd), error_msg(src.error_msg), tile(src.tile), payload(src.payload.Clone()) {}

	DynBaseCommandContainer(DynBaseCommandContainer &&) = default;
	DynBaseCommandContainer(const DynBaseCommandContainer &src) { *this = src; }
	DynBaseCommandContainer &operator=(DynBaseCommandContainer &&other) = default;

	DynBaseCommandContainer &operator=(const DynBaseCommandContainer &other)
	{
		this->cmd = other.cmd;
		this->error_msg = other.error_msg;
		this->tile = other.tile;
		this->payload = (other.payload != nullptr) ? other.payload->Clone() : nullptr;
		return *this;
	}

	void Serialise(BufferSerialisationRef buffer) const;
	const char *Deserialise(DeserialisationBuffer &buffer);
};

struct DynCommandContainer {
	DynBaseCommandContainer command{};
	CommandCallback callback = CommandCallback::None; ///< any callback function executed upon successful completion of the command.
	CallbackParameter callback_param{};

	DynCommandContainer() = default;
	DynCommandContainer(Commands cmd, StringID error_msg, TileIndex tile, std::unique_ptr<CommandPayloadBase> payload, CommandCallback callback, CallbackParameter callback_param)
			: command(cmd, error_msg, tile, std::move(payload)), callback(callback), callback_param(callback_param) {}

	template <Commands Tcmd>
	DynCommandContainer(const CommandContainer<Tcmd> &src) : command(src), callback(src.callback), callback_param(src.callback_param) {}
};

struct CommandExecData {
	TileIndex tile;
	DoCommandFlags flags;
	const CommandPayloadBase &payload;
};

using CommandPayloadDeserialiser = std::unique_ptr<CommandPayloadBase>(DeserialisationBuffer &, StringValidationSettings default_string_validation);

template <typename T>
using CommandProcDirect = CommandCost(DoCommandFlags flags, TileIndex tile, const T &data);
template <typename T>
using CommandProcDirectNoTile = CommandCost(DoCommandFlags flags, const T &data);

#ifdef CMD_DEFINE
#define DEF_CMD_HANDLER(cmd_, proctype_, proc_, flags_, type_) \
template <> struct CommandHandlerTraits<cmd_> { \
	static constexpr auto &proc = proc_; \
	static inline constexpr const char *name = #proc_; \
};
#else
#define DEF_CMD_HANDLER(cmd_, proctype_, proc_, flags_, type_)
#endif

#define DEF_CMD_PROC_GENERAL(cmd_, proctype_, proc_, payload_, flags_, type_, input_no_tile_, output_no_tile_) \
proctype_ proc_; \
DEF_CMD_HANDLER(cmd_, proctype_, proc_, flags_, type_) \
template <> struct CommandTraits<cmd_> { \
	using PayloadType = payload_; \
	static constexpr Commands cmd = cmd_; \
	static constexpr CommandFlags flags = flags_; \
	static constexpr CommandType type = type_; \
	static constexpr bool input_no_tile = input_no_tile_; \
	static constexpr bool output_no_tile = output_no_tile_; \
};

/*
 * Command macro variants:
 *
 * DEF_CMD_TUPLE:
 * Command<...>::Do/Post assemble the payload according the payload's Tuple typedef, using Payload::Make(...).
 * The payload is unpacked at the other end for the call to the handler.
 *
 * DEF_CMD_DIRECT:
 * The payload is passed to DoCommand/DoCommandP directly and forwarded to the command handler as a `const T &` with no packing/unpacking
 *
 * Suffixes:
 * <none>: Normal command, call and handler both use the tile index
 * _LT:    Location tile only, the call on the input side uses the tile index (for error message location, etc), but this is not passed to the command handler. For scripts the tile index is omitted.
 * _NT:    No tile, neither the call nor handler use the tile index
 */

#define DEF_CMD_DIRECT(cmd_, proc_, flags_, type_, payload_) DEF_CMD_PROC_GENERAL(cmd_, CommandProcDirect<payload_>, proc_, payload_, flags_, type_, false, false)
#define DEF_CMD_DIRECT_LT(cmd_, proc_, flags_, type_, payload_) DEF_CMD_PROC_GENERAL(cmd_, CommandProcDirectNoTile<payload_>, proc_, payload_, flags_, type_, false, true)
#define DEF_CMD_DIRECT_NT(cmd_, proc_, flags_, type_, payload_) DEF_CMD_PROC_GENERAL(cmd_, CommandProcDirectNoTile<payload_>, proc_, payload_, flags_, type_, true, true)

/* The .../__VA_ARGS__ part is the payload type, this is to support template types which include comma ',' characters. */
#define DEF_CMD_TUPLE(cmd_, proc_, flags_, type_, ...) \
namespace cmd_detail { using payload_ ## cmd_ = __VA_ARGS__ ; }; \
DEF_CMD_PROC_GENERAL(cmd_, cmd_detail::payload_ ## cmd_ ::CommandProc, proc_, cmd_detail::payload_ ## cmd_, flags_, type_, false, false)
#define DEF_CMD_TUPLE_LT(cmd_, proc_, flags_, type_, ...) \
namespace cmd_detail { using payload_ ## cmd_ = __VA_ARGS__ ; }; \
DEF_CMD_PROC_GENERAL(cmd_, cmd_detail::payload_ ## cmd_ ::CommandProcNoTile, proc_, cmd_detail::payload_ ## cmd_, flags_, type_, false, true)
#define DEF_CMD_TUPLE_NT(cmd_, proc_, flags_, type_, ...) \
namespace cmd_detail { using payload_ ## cmd_ = __VA_ARGS__ ; }; \
DEF_CMD_PROC_GENERAL(cmd_, cmd_detail::payload_ ## cmd_ ::CommandProcNoTile, proc_, cmd_detail::payload_ ## cmd_, flags_, type_, true, true)

template <Commands Tcmd>
using CmdPayload = typename CommandTraits<Tcmd>::PayloadType;

#endif /* COMMAND_TYPE_H */
