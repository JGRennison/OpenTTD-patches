/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_type.h Types related to commands. */

#ifndef COMMAND_TYPE_H
#define COMMAND_TYPE_H

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

enum CommandCostIntlFlags : uint8_t {
	CCIF_NONE                     = 0,
	CCIF_SUCCESS                  = 1 << 0,
	CCIF_INLINE_EXTRA_MSG         = 1 << 1,
	CCIF_INLINE_TILE              = 1 << 2,
	CCIF_INLINE_RESULT            = 1 << 3,
	CCIF_VALID_RESULT             = 1 << 4,
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
	StringID message;                           ///< Warning message for when success is unset
	union {
		uint32_t result = 0;
		StringID extra_message;                 ///< Additional warning message for when success is unset
		TileIndex tile;
	} inl;

	struct CommandCostAuxiliaryData {
		uint32_t textref_stack[16] = {};
		const GRFFile *textref_stack_grffile = nullptr; ///< NewGRF providing the #TextRefStack content.
		uint textref_stack_size = 0;                    ///< Number of uint32_t values to put on the #TextRefStack for the error message.
		StringID extra_message = INVALID_STRING_ID;     ///< Additional warning message for when success is unset
		TileIndex tile = INVALID_TILE;
		uint32_t result = 0;
	};
	std::unique_ptr<CommandCostAuxiliaryData> aux_data;

	void AllocAuxData();
	bool AddInlineData(CommandCostIntlFlags inline_flag);

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
	CommandCost(CommandCost &&other) = default;
	CommandCost &operator=(const CommandCost &other);
	CommandCost &operator=(CommandCost &&other) = default;

	/**
	 * Creates a command return value the is failed with the given message
	 */
	static CommandCost DualErrorMessage(StringID msg, StringID extra_msg)
	{
		CommandCost cc(msg);
		cc.flags |= CCIF_INLINE_EXTRA_MSG;
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
		this->flags &= ~(CCIF_SUCCESS | CCIF_INLINE_EXTRA_MSG);
		this->message = message;
		if (this->aux_data) this->aux_data->extra_message = INVALID_STRING_ID;
	}

	void UseTextRefStack(const GRFFile *grffile, uint num_registers);

	/**
	 * Returns the NewGRF providing the #TextRefStack of the error message.
	 * @return the NewGRF.
	 */
	const GRFFile *GetTextRefStackGRF() const
	{
		return this->aux_data != nullptr ? this->aux_data->textref_stack_grffile : 0;
	}

	/**
	 * Returns the number of uint32_t values for the #TextRefStack of the error message.
	 * @return number of uint32_t values.
	 */
	uint GetTextRefStackSize() const
	{
		return this->aux_data != nullptr ? this->aux_data->textref_stack_size : 0;
	}

	/**
	 * Returns a pointer to the values for the #TextRefStack of the error message.
	 * @return uint32_t values for the #TextRefStack
	 */
	const uint32_t *GetTextRefStack() const
	{
		return this->aux_data != nullptr ? this->aux_data->textref_stack : nullptr;
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
		if (this->flags & CCIF_INLINE_EXTRA_MSG) return this->inl.extra_message;
		return this->aux_data != nullptr ? this->aux_data->extra_message : INVALID_STRING_ID;
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
		if (this->flags & CCIF_INLINE_TILE) return this->inl.tile;
		return this->aux_data != nullptr ? this->aux_data->tile : INVALID_TILE;
	}

	void SetTile(TileIndex tile);

	bool HasResultData() const
	{
		return (this->flags & CCIF_VALID_RESULT);
	}

	uint32_t GetResultData() const
	{
		if (this->flags & CCIF_INLINE_RESULT) return this->inl.result;
		return this->aux_data != nullptr ? this->aux_data->result : 0;
	}

	void SetResultData(uint32_t result);
};

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
enum Commands : uint16_t {
	CMD_BUILD_RAILROAD_TRACK,         ///< build a rail track
	CMD_REMOVE_RAILROAD_TRACK,        ///< remove a rail track
	CMD_BUILD_SINGLE_RAIL,            ///< build a single rail track
	CMD_REMOVE_SINGLE_RAIL,           ///< remove a single rail track
	CMD_LANDSCAPE_CLEAR,              ///< demolish a tile
	CMD_BUILD_BRIDGE,                 ///< build a bridge
	CMD_BUILD_RAIL_STATION,           ///< build a rail station
	CMD_BUILD_TRAIN_DEPOT,            ///< build a train depot
	CMD_BUILD_SIGNALS,                ///< build a signal
	CMD_REMOVE_SIGNALS,               ///< remove a signal
	CMD_TERRAFORM_LAND,               ///< terraform a tile
	CMD_BUILD_OBJECT,                 ///< build an object
	CMD_PURCHASE_LAND_AREA,           ///< purchase an area of landscape
	CMD_BUILD_OBJECT_AREA,            ///< build an area of objects
	CMD_BUILD_TUNNEL,                 ///< build a tunnel

	CMD_REMOVE_FROM_RAIL_STATION,     ///< remove a (rectangle of) tiles from a rail station
	CMD_CONVERT_RAIL,                 ///< convert a rail type
	CMD_CONVERT_RAIL_TRACK,           ///< convert a rail type (track)

	CMD_BUILD_RAIL_WAYPOINT,          ///< build a waypoint
	CMD_BUILD_ROAD_WAYPOINT,          ///< build a road waypoint
	CMD_RENAME_WAYPOINT,              ///< rename a waypoint
	CMD_SET_WAYPOINT_LABEL_HIDDEN,    ///< set whether waypoint label is hidden
	CMD_REMOVE_FROM_RAIL_WAYPOINT,    ///< remove a (rectangle of) tiles from a rail waypoint

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

	CMD_TOGGLE_REUSE_DEPOT_VEHICLES,  ///< toggle 'reuse depot vehicles' on template
	CMD_TOGGLE_KEEP_REMAINING_VEHICLES, ///< toggle 'keep remaining vehicles' on template
	CMD_SET_REFIT_AS_TEMPLATE,        ///< set/unset 'refit as template' on template
	CMD_TOGGLE_TMPL_REPLACE_OLD_ONLY, ///< toggle 'replace old vehicles only' on template
	CMD_RENAME_TMPL_REPLACE,          ///< rename a template

	CMD_VIRTUAL_TRAIN_FROM_TEMPLATE_VEHICLE, ///< Creates a virtual train from a template
	CMD_VIRTUAL_TRAIN_FROM_TRAIN,     ///< Creates a virtual train from a regular train
	CMD_DELETE_VIRTUAL_TRAIN,         ///< Delete a virtual train
	CMD_BUILD_VIRTUAL_RAIL_VEHICLE,   ///< Build a virtual train
	CMD_REPLACE_TEMPLATE_VEHICLE,     ///< Replace a template vehicle with another one based on a virtual train
	CMD_MOVE_VIRTUAL_RAIL_VEHICLE,    ///< Move a virtual rail vehicle
	CMD_SELL_VIRTUAL_VEHICLE,         ///< Sell a virtual vehicle

	CMD_CLONE_TEMPLATE_VEHICLE_FROM_TRAIN, ///< clone a train and create a new template vehicle based on it
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

	CMD_INSERT_SIGNAL_INSTRUCTION,    ///< insert a signal instruction
	CMD_MODIFY_SIGNAL_INSTRUCTION,    ///< modifies a signal instruction
	CMD_REMOVE_SIGNAL_INSTRUCTION,    ///< removes a signal instruction
	CMD_SIGNAL_PROGRAM_MGMT,          ///< signal program management command

	CMD_SCHEDULED_DISPATCH,                     ///< scheduled dispatch start
	CMD_SCHEDULED_DISPATCH_ADD,                 ///< scheduled dispatch add
	CMD_SCHEDULED_DISPATCH_REMOVE,              ///< scheduled dispatch remove
	CMD_SCHEDULED_DISPATCH_SET_DURATION,        ///< scheduled dispatch set schedule duration
	CMD_SCHEDULED_DISPATCH_SET_START_DATE,      ///< scheduled dispatch set start date
	CMD_SCHEDULED_DISPATCH_SET_DELAY,           ///< scheduled dispatch set maximum allow delay
	CMD_SCHEDULED_DISPATCH_SET_REUSE_SLOTS,     ///< scheduled dispatch set whether to re-use dispatch slots
	CMD_SCHEDULED_DISPATCH_RESET_LAST_DISPATCH, ///< scheduled dispatch reset last dispatch date
	CMD_SCHEDULED_DISPATCH_CLEAR,               ///< scheduled dispatch clear schedule
	CMD_SCHEDULED_DISPATCH_ADD_NEW_SCHEDULE,    ///< scheduled dispatch add new schedule
	CMD_SCHEDULED_DISPATCH_REMOVE_SCHEDULE,     ///< scheduled dispatch remove schedule
	CMD_SCHEDULED_DISPATCH_RENAME_SCHEDULE,     ///< scheduled dispatch rename schedule
	CMD_SCHEDULED_DISPATCH_DUPLICATE_SCHEDULE,  ///< scheduled dispatch duplicate schedule
	CMD_SCHEDULED_DISPATCH_APPEND_VEHICLE_SCHEDULE, ///< scheduled dispatch append schedules from another vehicle
	CMD_SCHEDULED_DISPATCH_ADJUST,              ///< scheduled dispatch adjust time offsets in schedule
	CMD_SCHEDULED_DISPATCH_SWAP_SCHEDULES,      ///< scheduled dispatch swap schedules in order
	CMD_SCHEDULED_DISPATCH_SET_SLOT_FLAGS,      ///< scheduled dispatch set flags of dispatch slot
	CMD_SCHEDULED_DISPATCH_RENAME_TAG,          ///< scheduled dispatch rename departure tag

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
enum DoCommandFlag {
	DC_NONE                  = 0x000, ///< no flag is set
	DC_EXEC                  = 0x001, ///< execute the given command
	DC_AUTO                  = 0x002, ///< don't allow building on structures
	DC_QUERY_COST            = 0x004, ///< query cost only,  don't build.
	DC_NO_WATER              = 0x008, ///< don't allow building on water
	// 0x010 is unused
	DC_NO_TEST_TOWN_RATING   = 0x020, ///< town rating does not disallow you from building
	DC_BANKRUPT              = 0x040, ///< company bankrupts, skip money check, skip vehicle on tile check in some cases
	DC_AUTOREPLACE           = 0x080, ///< autoreplace/autorenew is in progress, this shall disable vehicle limits when building, and ignore certain restrictions when undoing things (like vehicle attach callback)
	DC_NO_CARGO_CAP_CHECK    = 0x100, ///< when autoreplace/autorenew is in progress, this shall prevent truncating the amount of cargo in the vehicle to prevent testing the command to remove cargo
	DC_ALL_TILES             = 0x200, ///< allow this command also on MP_VOID tiles
	DC_NO_MODIFY_TOWN_RATING = 0x400, ///< do not change town rating
	DC_FORCE_CLEAR_TILE      = 0x800, ///< do not only remove the object on the tile, but also clear any water left on it
	DC_ALLOW_REMOVE_WATER    = 0x1000,///< always allow removing water
	DC_TOWN                  = 0x2000,///< town operation
};
DECLARE_ENUM_AS_BIT_SET(DoCommandFlag)

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
enum CommandFlags : uint16_t {
	CMD_SERVER    =  0x001, ///< the command can only be initiated by the server
	CMD_SPECTATOR =  0x002, ///< the command may be initiated by a spectator
	CMD_OFFLINE   =  0x004, ///< the command cannot be executed in a multiplayer game; single-player only
	CMD_AUTO      =  0x008, ///< set the DC_AUTO flag on this command
	CMD_ALL_TILES =  0x010, ///< allow this command also on MP_VOID tiles
	CMD_NO_TEST   =  0x020, ///< the command's output may differ between test and execute due to town rating changes etc.
	CMD_NO_WATER  =  0x040, ///< set the DC_NO_WATER flag on this command
	CMD_CLIENT_ID =  0x080, ///< set p2 with the ClientID of the sending client.
	CMD_DEITY     =  0x100, ///< the command may be executed by COMPANY_DEITY
	CMD_STR_CTRL  =  0x200, ///< the command's string may contain control strings
	CMD_NO_EST    =  0x400, ///< the command is never estimated.
	CMD_SERVER_NS = 0x1000, ///< the command can only be initiated by the server (this is not executed in spectator mode)
	CMD_LOG_AUX   = 0x2000, ///< the command should be logged in the auxiliary log instead of the main log
	CMD_ERR_TILE  = 0x4000, ///< use payload error message tile for money text and error tile
};
DECLARE_ENUM_AS_BIT_SET(CommandFlags)

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

struct CommandAuxiliaryBase{}; // To be removed later

/**
 * Abstract base type for command payloads.
 *
 * Implementing types should:
 * - Be final.
 * - Have a deserialisation function of the form below, which returns true on success:
 *   bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
 * - Have a FormatDebugSummary implementation where even remotely useful.
 * - Implement GetErrorMessageTile/SetClientID if required by commands using this payload type.
 */
struct CommandPayloadBase {
	virtual ~CommandPayloadBase() {}

	virtual std::unique_ptr<CommandPayloadBase> Clone() const = 0;

	virtual void Serialise(struct BufferSerialisationRef buffer) const = 0;

	virtual void SanitiseStrings(StringValidationSettings settings) {}

	virtual TileIndex GetErrorMessageTile() const { return INVALID_TILE; }

	virtual void SetClientID(uint32_t client_id) { NOT_REACHED(); }

	/* FormatDebugSummary may be called when populating the crash log so should not allocate */
	virtual void FormatDebugSummary(struct format_target &) const {}

	std::string GetDebugSummaryString() const;
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

struct CommandEmptyPayload final : public CommandPayloadSerialisable<CommandEmptyPayload> {
	void Serialise(BufferSerialisationRef buffer) const override {}
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation) { return true; }
};

struct P123CmdData final : public CommandPayloadSerialisable<P123CmdData> {
	uint32_t p1;
	uint32_t p2;
	uint64_t p3;
	std::string text;

	P123CmdData() : p1(0), p2(0), p3(0) {}
	P123CmdData(uint32_t p1, uint32_t p2, uint64_t p3) : p1(p1), p2(p2), p3(p3) {}
	P123CmdData(uint32_t p1, uint32_t p2, uint64_t p3, std::string text) : p1(p1), p2(p2), p3(p3), text(std::move(text)) {}

	void Serialise(BufferSerialisationRef buffer) const override;
	void SanitiseStrings(StringValidationSettings settings) override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	TileIndex GetErrorMessageTile() const override;
	void SetClientID(uint32_t client_id) override;
	void FormatDebugSummary(struct format_target &) const override;
};

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
	struct BaseTupleCmdData : public CommandPayloadBase, public BaseTupleCmdDataTag {
		using CommandProc = CommandCost(TileIndex, DoCommandFlag, typename CommandProcTupleAdapter::replace_string_t<T>...);
		using CommandProcNoTile = CommandCost(DoCommandFlag, typename CommandProcTupleAdapter::replace_string_t<T>...);
		using Tuple = std::tuple<T...>;
		Tuple values;

		template <typename... Args>
		BaseTupleCmdData(Args&& ... args) : values(std::forward<Args>(args)...) {}

		BaseTupleCmdData(Tuple&& values) : values(std::move(values)) {}

		virtual void Serialise(BufferSerialisationRef buffer) const override;
		virtual void SanitiseStrings(StringValidationSettings settings) override;
		bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	};
};

template <typename Parent, typename... T>
struct TupleCmdData : public TupleCmdDataDetail::BaseTupleCmdData<T...> {
	using TupleCmdDataDetail::BaseTupleCmdData<T...>::BaseTupleCmdData;
	using Tuple = TupleCmdDataDetail::BaseTupleCmdData<T...>::Tuple;

	std::unique_ptr<CommandPayloadBase> Clone() const override;

	template <typename... Args>
	static Parent Make(Args&&... args)
	{
		Parent out;
		out.values = Tuple(std::forward<Args>(args)...);
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

	void FormatDebugSummary(struct format_target &output) const override;
};

template <typename T>
struct BaseCommandContainer {
	Commands cmd{};                              ///< command being executed.
	StringID error_msg{};                        ///< error message
	TileIndex tile{};                            ///< tile command being executed on.
	T payload{};                                 ///< payload
};

template <typename T>
struct CommandContainer : public BaseCommandContainer<T> {
	CommandCallback callback = CommandCallback::None; ///< any callback function executed upon successful completion of the command.
	CallbackParameter callback_param{};
};

struct SerialisedBaseCommandContainer : public BaseCommandContainer<CommandPayloadSerialised> {
	void Serialise(BufferSerialisationRef buffer) const;
};

struct DynBaseCommandContainer {
	Commands cmd{};                              ///< command being executed.
	StringID error_msg{};                        ///< error message
	TileIndex tile{};                            ///< tile command being executed on.
	std::unique_ptr<CommandPayloadBase> payload; ///< payload

	DynBaseCommandContainer() = default;

	template <typename T>
	DynBaseCommandContainer(const BaseCommandContainer<T> &src) : cmd(src.cmd), error_msg(src.error_msg), tile(src.tile), payload(src.payload.Clone()) {}

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

	template <typename T>
	DynCommandContainer(const CommandContainer<T> &src) : command(src), callback(src.callback), callback_param(src.callback_param) {}
};

inline BaseCommandContainer<P123CmdData> NewBaseCommandContainerBasic(TileIndex tile, uint32_t p1, uint32_t p2, uint32_t cmd)
{
	return { static_cast<Commands>(cmd & 0xFFFF), static_cast<StringID>(cmd >> 16), tile, P123CmdData(p1, p2, 0) };
}

inline CommandContainer<P123CmdData> NewCommandContainerBasic(TileIndex tile, uint32_t p1, uint32_t p2, uint32_t cmd, CommandCallback callback = CommandCallback::None)
{
	return { NewBaseCommandContainerBasic(tile, p1, p2, cmd), callback, 0 };
}

struct CommandExecData {
	TileIndex tile;
	DoCommandFlag flags;
	const CommandPayloadBase &payload;
};

using CommandPayloadDeserialiser = std::unique_ptr<CommandPayloadBase>(DeserialisationBuffer &, StringValidationSettings default_string_validation);

#ifdef CMD_DEFINE
using CommandProc = CommandCost(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text);
using CommandProcEx = CommandCost(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, uint64_t p3, const char *text, const CommandAuxiliaryBase *aux_data);

template <typename T>
using CommandProcDirect = CommandCost(TileIndex tile, DoCommandFlag flags, const T &data);

#define DEF_CMD_HANDLER(cmd_, proctype_, proc_, flags_, type_) \
proctype_ proc_; \
template <> struct CommandHandlerTraits<cmd_> { \
	static constexpr auto &proc = proc_; \
	static inline constexpr const char *name = #proc_; \
};
#else
#define DEF_CMD_HANDLER(cmd_, proctype_, proc_, flags_, type_)
#endif

#define DEF_CMD_PROC_GENERAL(cmd_, proctype_, proc_, payload_, flags_, type_, no_tile_) \
DEF_CMD_HANDLER(cmd_, proctype_, proc_, flags_, type_) \
template <> struct CommandTraits<cmd_> { \
	using PayloadType = payload_; \
	static constexpr Commands cmd = cmd_; \
	static constexpr CommandFlags flags = flags_; \
	static constexpr CommandType type = type_; \
	static constexpr bool no_tile = no_tile_; \
};

#define DEF_CMD_PROC(cmd_, proc_, flags_, type_) DEF_CMD_PROC_GENERAL(cmd_, CommandProc, proc_, P123CmdData, flags_, type_, false)
#define DEF_CMD_PROCEX(cmd_, proc_, flags_, type_) DEF_CMD_PROC_GENERAL(cmd_, CommandProcEx, proc_, P123CmdData, flags_, type_, false)
#define DEF_CMD_DIRECT(cmd_, proc_, payload_, flags_, type_) DEF_CMD_PROC_GENERAL(cmd_, CommandProcDirect<payload_>, proc_, payload_, flags_, type_, false)
#define DEF_CMD_TUPLE(cmd_, proc_, payload_, flags_, type_) DEF_CMD_PROC_GENERAL(cmd_, payload_::CommandProc, proc_, payload_, flags_, type_, false)
#define DEF_CMD_TUPLE_NT(cmd_, proc_, payload_, flags_, type_) DEF_CMD_PROC_GENERAL(cmd_, payload_::CommandProcNoTile, proc_, payload_, flags_, type_, true)

DEF_CMD_PROC  (CMD_BUILD_RAILROAD_TRACK, CmdBuildRailroadTrack,       CMD_NO_WATER | CMD_AUTO | CMD_ERR_TILE, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_REMOVE_RAILROAD_TRACK, CmdRemoveRailroadTrack,                     CMD_AUTO | CMD_ERR_TILE, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_SINGLE_RAIL, CmdBuildSingleRail,          CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_REMOVE_SINGLE_RAIL, CmdRemoveSingleRail,                        CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_LANDSCAPE_CLEAR, CmdLandscapeClear,                         CMD_DEITY, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_BRIDGE, CmdBuildBridge,  CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROCEX(CMD_BUILD_RAIL_STATION, CmdBuildRailStation,         CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_TRAIN_DEPOT, CmdBuildTrainDepot,          CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_SIGNALS, CmdBuildSingleSignal,                       CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_REMOVE_SIGNALS, CmdRemoveSingleSignal,                      CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_TERRAFORM_LAND, CmdTerraformLand,           CMD_ALL_TILES | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_OBJECT, CmdBuildObject,  CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_PURCHASE_LAND_AREA, CmdPurchaseLandArea, CMD_NO_WATER | CMD_AUTO | CMD_NO_TEST, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_OBJECT_AREA, CmdBuildObjectArea,  CMD_NO_WATER | CMD_AUTO | CMD_NO_TEST, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_TUNNEL, CmdBuildTunnel,                 CMD_DEITY | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_REMOVE_FROM_RAIL_STATION, CmdRemoveFromRailStation,                          {}, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_CONVERT_RAIL, CmdConvertRail,                                    {}, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_CONVERT_RAIL_TRACK, CmdConvertRailTrack,                               {}, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROCEX(CMD_BUILD_RAIL_WAYPOINT, CmdBuildRailWaypoint,                              {}, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROCEX(CMD_BUILD_ROAD_WAYPOINT, CmdBuildRoadWaypoint,                              {}, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_RENAME_WAYPOINT, CmdRenameWaypoint,                                 {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SET_WAYPOINT_LABEL_HIDDEN, CmdSetWaypointLabelHidden,                         {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_REMOVE_FROM_RAIL_WAYPOINT, CmdRemoveFromRailWaypoint,                         {}, CMDT_LANDSCAPE_CONSTRUCTION)

DEF_CMD_PROCEX(CMD_BUILD_ROAD_STOP, CmdBuildRoadStop,            CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_REMOVE_ROAD_STOP, CmdRemoveRoadStop,                                 {}, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_LONG_ROAD, CmdBuildLongRoad,CMD_DEITY | CMD_NO_WATER | CMD_AUTO | CMD_ERR_TILE, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_REMOVE_LONG_ROAD, CmdRemoveLongRoad,            CMD_NO_TEST | CMD_AUTO | CMD_ERR_TILE, CMDT_LANDSCAPE_CONSTRUCTION) // towns may disallow removing road bits (as they are connected) in test, but in exec they're removed and thus removing is allowed.
DEF_CMD_PROC  (CMD_BUILD_ROAD, CmdBuildRoad,    CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_ROAD_DEPOT, CmdBuildRoadDepot,           CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_CONVERT_ROAD, CmdConvertRoad,                                    {}, CMDT_LANDSCAPE_CONSTRUCTION)

DEF_CMD_PROC  (CMD_BUILD_AIRPORT, CmdBuildAirport,             CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_DOCK, CmdBuildDock,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_SHIP_DEPOT, CmdBuildShipDepot,                          CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_BUILD_BUOY, CmdBuildBuoy,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_PLANT_TREE, CmdPlantTree,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)

DEF_CMD_PROC  (CMD_BUILD_VEHICLE, CmdBuildVehicle,                       CMD_CLIENT_ID, CMDT_VEHICLE_CONSTRUCTION  )
DEF_CMD_PROC  (CMD_SELL_VEHICLE, CmdSellVehicle,                        CMD_CLIENT_ID, CMDT_VEHICLE_CONSTRUCTION  )
DEF_CMD_PROC  (CMD_REFIT_VEHICLE, CmdRefitVehicle,                                   {}, CMDT_VEHICLE_CONSTRUCTION  )
DEF_CMD_PROC  (CMD_SEND_VEHICLE_TO_DEPOT, CmdSendVehicleToDepot,                             {}, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_SET_VEHICLE_VISIBILITY, CmdSetVehicleVisibility,                           {}, CMDT_COMPANY_SETTING       )

DEF_CMD_PROC  (CMD_MOVE_RAIL_VEHICLE, CmdMoveRailVehicle,                                {}, CMDT_VEHICLE_CONSTRUCTION  )
DEF_CMD_PROC  (CMD_FORCE_TRAIN_PROCEED, CmdForceTrainProceed,                              {}, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_REVERSE_TRAIN_DIRECTION, CmdReverseTrainDirection,                          {}, CMDT_VEHICLE_MANAGEMENT    )

DEF_CMD_PROC  (CMD_CLEAR_ORDER_BACKUP, CmdClearOrderBackup,                   CMD_CLIENT_ID, CMDT_SERVER_SETTING        )
DEF_CMD_PROCEX(CMD_MODIFY_ORDER, CmdModifyOrder,                                    {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SKIP_TO_ORDER, CmdSkipToOrder,                                    {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_DELETE_ORDER, CmdDeleteOrder,                                    {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROCEX(CMD_INSERT_ORDER, CmdInsertOrder,                                    {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_DUPLICATE_ORDER, CmdDuplicateOrder,                                 {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_MASS_CHANGE_ORDER, CmdMassChangeOrder,                                {}, CMDT_ROUTE_MANAGEMENT      )

DEF_CMD_PROC  (CMD_CHANGE_SERVICE_INT, CmdChangeServiceInt,                               {}, CMDT_VEHICLE_MANAGEMENT    )

DEF_CMD_PROC  (CMD_BUILD_INDUSTRY, CmdBuildIndustry,                          CMD_DEITY, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_INDUSTRY_SET_FLAGS, CmdIndustrySetFlags,        CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_INDUSTRY_SET_EXCLUSIVITY, CmdIndustrySetExclusivity,  CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_INDUSTRY_SET_TEXT, CmdIndustrySetText,         CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_INDUSTRY_SET_PRODUCTION, CmdIndustrySetProduction,                  CMD_DEITY, CMDT_OTHER_MANAGEMENT      )

DEF_CMD_PROC  (CMD_SET_COMPANY_MANAGER_FACE, CmdSetCompanyManagerFace,                          {}, CMDT_COMPANY_SETTING       )
DEF_CMD_PROC  (CMD_SET_COMPANY_COLOUR, CmdSetCompanyColour,                               {}, CMDT_COMPANY_SETTING       )

DEF_CMD_PROC  (CMD_INCREASE_LOAN, CmdIncreaseLoan,                                   {}, CMDT_MONEY_MANAGEMENT      )
DEF_CMD_PROC  (CMD_DECREASE_LOAN, CmdDecreaseLoan,                                   {}, CMDT_MONEY_MANAGEMENT      )
DEF_CMD_PROCEX(CMD_SET_COMPANY_MAX_LOAN, CmdSetCompanyMaxLoan,                      CMD_DEITY, CMDT_MONEY_MANAGEMENT      )

DEF_CMD_PROC  (CMD_WANT_ENGINE_PREVIEW, CmdWantEnginePreview,                              {}, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_ENGINE_CTRL, CmdEngineCtrl,                             CMD_DEITY, CMDT_VEHICLE_MANAGEMENT    )

DEF_CMD_PROC  (CMD_RENAME_VEHICLE, CmdRenameVehicle,                                  {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_RENAME_ENGINE, CmdRenameEngine,                          CMD_SERVER, CMDT_OTHER_MANAGEMENT      )

DEF_CMD_PROC  (CMD_RENAME_COMPANY, CmdRenameCompany,                                  {}, CMDT_COMPANY_SETTING       )
DEF_CMD_PROC  (CMD_RENAME_PRESIDENT, CmdRenamePresident,                                {}, CMDT_COMPANY_SETTING       )

DEF_CMD_PROC  (CMD_RENAME_STATION, CmdRenameStation,                                  {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_RENAME_DEPOT, CmdRenameDepot,                                    {}, CMDT_OTHER_MANAGEMENT      )

DEF_CMD_PROC  (CMD_EXCHANGE_STATION_NAMES, CmdExchangeStationNames,                           {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SET_STATION_CARGO_ALLOWED_SUPPLY, CmdSetStationCargoAllowedSupply,                   {}, CMDT_OTHER_MANAGEMENT      )

DEF_CMD_PROC  (CMD_PLACE_SIGN, CmdPlaceSign,                CMD_LOG_AUX | CMD_DEITY, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_RENAME_SIGN, CmdRenameSign,               CMD_LOG_AUX | CMD_DEITY, CMDT_OTHER_MANAGEMENT      )

DEF_CMD_PROC  (CMD_TURN_ROADVEH, CmdTurnRoadVeh,                                    {}, CMDT_VEHICLE_MANAGEMENT    )

DEF_CMD_PROC  (CMD_PAUSE, CmdPause,                    CMD_SERVER | CMD_NO_EST, CMDT_SERVER_SETTING        )

DEF_CMD_PROC  (CMD_BUY_SHARE_IN_COMPANY, CmdBuyShareInCompany,                              {}, CMDT_MONEY_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SELL_SHARE_IN_COMPANY, CmdSellShareInCompany,                             {}, CMDT_MONEY_MANAGEMENT      )
DEF_CMD_PROC  (CMD_BUY_COMPANY, CmdBuyCompany,                                     {}, CMDT_MONEY_MANAGEMENT      )
DEF_CMD_PROC  (CMD_DECLINE_BUY_COMPANY, CmdDeclineBuyCompany,                     CMD_NO_EST, CMDT_SERVER_SETTING        )

DEF_CMD_PROC  (CMD_FOUND_TOWN, CmdFoundTown,                CMD_DEITY | CMD_NO_TEST, CMDT_LANDSCAPE_CONSTRUCTION) // founding random town can fail only in exec run
DEF_CMD_PROC  (CMD_RENAME_TOWN, CmdRenameTown,                CMD_DEITY | CMD_SERVER, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_RENAME_TOWN_NON_ADMIN, CmdRenameTownNonAdmin,                             {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_DO_TOWN_ACTION, CmdDoTownAction,                                   {}, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_TOWN_SETTING_OVERRIDE, CmdOverrideTownSetting,       CMD_DEITY | CMD_SERVER, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_TOWN_SETTING_OVERRIDE_NON_ADMIN, CmdOverrideTownSettingNonAdmin,                    {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_TOWN_CARGO_GOAL, CmdTownCargoGoal,            CMD_LOG_AUX | CMD_DEITY, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_TOWN_GROWTH_RATE, CmdTownGrowthRate,           CMD_LOG_AUX | CMD_DEITY, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_TOWN_RATING, CmdTownRating,               CMD_LOG_AUX | CMD_DEITY, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_TOWN_SET_TEXT, CmdTownSetText,    CMD_LOG_AUX | CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT )
DEF_CMD_PROC  (CMD_EXPAND_TOWN, CmdExpandTown,                             CMD_DEITY, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_DELETE_TOWN, CmdDeleteTown,                           CMD_OFFLINE, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_PLACE_HOUSE, CmdPlaceHouse,                             CMD_DEITY, CMDT_OTHER_MANAGEMENT      )

DEF_CMD_PROC  (CMD_ORDER_REFIT, CmdOrderRefit,                                     {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_CLONE_ORDER, CmdCloneOrder,                                     {}, CMDT_ROUTE_MANAGEMENT      )

DEF_CMD_PROC  (CMD_CLEAR_AREA, CmdClearArea,                            CMD_NO_TEST, CMDT_LANDSCAPE_CONSTRUCTION) // destroying multi-tile houses makes town rating differ between test and execution

DEF_CMD_PROCEX(CMD_MONEY_CHEAT, CmdMoneyCheat,                                     {}, CMDT_CHEAT                 )
DEF_CMD_PROCEX(CMD_MONEY_CHEAT_ADMIN, CmdMoneyCheatAdmin,                    CMD_SERVER_NS, CMDT_CHEAT                 )
DEF_CMD_PROCEX(CMD_CHANGE_BANK_BALANCE, CmdChangeBankBalance,                      CMD_DEITY, CMDT_MONEY_MANAGEMENT      )
DEF_CMD_PROC  (CMD_CHEAT_SETTING, CmdCheatSetting,                          CMD_SERVER, CMDT_CHEAT                 )
DEF_CMD_PROC  (CMD_BUILD_CANAL, CmdBuildCanal,                  CMD_DEITY | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROC  (CMD_CREATE_SUBSIDY, CmdCreateSubsidy,                          CMD_DEITY, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_COMPANY_CTRL, CmdCompanyCtrl, CMD_SPECTATOR | CMD_CLIENT_ID | CMD_NO_EST, CMDT_SERVER_SETTING  )
DEF_CMD_PROC  (CMD_COMPANY_ALLOW_LIST_CTRL, CmdCompanyAllowListCtrl,                               CMD_NO_TEST, CMDT_SERVER_SETTING        )
DEF_CMD_PROC  (CMD_CUSTOM_NEWS_ITEM, CmdCustomNewsItem,          CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_CREATE_GOAL, CmdCreateGoal,              CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_REMOVE_GOAL, CmdRemoveGoal,                             CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROCEX(CMD_SET_GOAL_DESTINATION, CmdSetGoalDestination,                     CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SET_GOAL_TEXT, CmdSetGoalText,             CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SET_GOAL_PROGRESS, CmdSetGoalProgress,         CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SET_GOAL_COMPLETED, CmdSetGoalCompleted,        CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROCEX(CMD_GOAL_QUESTION, CmdGoalQuestion,            CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_GOAL_QUESTION_ANSWER, CmdGoalQuestionAnswer,                     CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_CREATE_STORY_PAGE, CmdCreateStoryPage,         CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_CREATE_STORY_PAGE_ELEMENT, CmdCreateStoryPageElement,  CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_UPDATE_STORY_PAGE_ELEMENT, CmdUpdateStoryPageElement,  CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SET_STORY_PAGE_TITLE, CmdSetStoryPageTitle,       CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SET_STORY_PAGE_DATE, CmdSetStoryPageDate,                       CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SHOW_STORY_PAGE, CmdShowStoryPage,                          CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_REMOVE_STORY_PAGE, CmdRemoveStoryPage,                        CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_REMOVE_STORY_PAGE_ELEMENT, CmdRemoveStoryPageElement,                 CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCROLL_VIEWPORT, CmdScrollViewport,                         CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_STORY_PAGE_BUTTON, CmdStoryPageButton,                        CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      )

DEF_CMD_PROC  (CMD_LEVEL_LAND, CmdLevelLand, CMD_ALL_TILES | CMD_NO_TEST | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION) // test run might clear tiles multiple times, in execution that only happens once

DEF_CMD_PROC  (CMD_BUILD_LOCK, CmdBuildLock,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)

DEF_CMD_PROCEX(CMD_BUILD_SIGNAL_TRACK, CmdBuildSignalTrack,                        CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)
DEF_CMD_PROCEX(CMD_REMOVE_SIGNAL_TRACK, CmdRemoveSignalTrack,                       CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION)

DEF_CMD_PROCEX(CMD_GIVE_MONEY, CmdGiveMoney,                                      {}, CMDT_MONEY_MANAGEMENT      )
DEF_CMD_PROC  (CMD_CHANGE_SETTING, CmdChangeSetting,                         CMD_SERVER, CMDT_SERVER_SETTING        )
DEF_CMD_PROC  (CMD_CHANGE_COMPANY_SETTING, CmdChangeCompanySetting,                           {}, CMDT_COMPANY_SETTING       )
DEF_CMD_PROC  (CMD_SET_AUTOREPLACE, CmdSetAutoReplace,                                 {}, CMDT_VEHICLE_MANAGEMENT    )

DEF_CMD_PROC  (CMD_TOGGLE_REUSE_DEPOT_VEHICLES, CmdToggleReuseDepotVehicles,           CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_TOGGLE_KEEP_REMAINING_VEHICLES, CmdToggleKeepRemainingVehicles,        CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_SET_REFIT_AS_TEMPLATE, CmdSetRefitAsTemplate,                 CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_TOGGLE_TMPL_REPLACE_OLD_ONLY, CmdToggleTemplateReplaceOldOnly,       CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_RENAME_TMPL_REPLACE, CmdRenameTemplateReplace,              CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    )

DEF_CMD_PROC  (CMD_VIRTUAL_TRAIN_FROM_TEMPLATE_VEHICLE, CmdVirtualTrainFromTemplateVehicle,   CMD_CLIENT_ID | CMD_NO_TEST | CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT)
DEF_CMD_PROC  (CMD_VIRTUAL_TRAIN_FROM_TRAIN, CmdVirtualTrainFromTrain,             CMD_CLIENT_ID | CMD_NO_TEST | CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT)
DEF_CMD_PROC  (CMD_DELETE_VIRTUAL_TRAIN, CmdDeleteVirtualTrain,                                              CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT)
DEF_CMD_PROC  (CMD_BUILD_VIRTUAL_RAIL_VEHICLE, CmdBuildVirtualRailVehicle,           CMD_CLIENT_ID | CMD_NO_TEST | CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT)
DEF_CMD_PROC  (CMD_REPLACE_TEMPLATE_VEHICLE, CmdReplaceTemplateVehicle,                                          CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT)
DEF_CMD_PROC  (CMD_MOVE_VIRTUAL_RAIL_VEHICLE, CmdMoveVirtualRailVehicle,                                          CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT)
DEF_CMD_PROC  (CMD_SELL_VIRTUAL_VEHICLE, CmdSellVirtualVehicle,                              CMD_CLIENT_ID | CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT)

DEF_CMD_PROC  (CMD_CLONE_TEMPLATE_VEHICLE_FROM_TRAIN, CmdTemplateVehicleFromTrain,           CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_DELETE_TEMPLATE_VEHICLE, CmdDeleteTemplateVehicle,              CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    )

DEF_CMD_PROC  (CMD_ISSUE_TEMPLATE_REPLACEMENT, CmdIssueTemplateReplacement,           CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_DELETE_TEMPLATE_REPLACEMENT, CmdDeleteTemplateReplacement,          CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    )

DEF_CMD_PROC  (CMD_CLONE_VEHICLE, CmdCloneVehicle,                         CMD_NO_TEST, CMDT_VEHICLE_CONSTRUCTION  ) // NewGRF callbacks influence building and refitting making it impossible to correctly estimate the cost
DEF_CMD_PROC  (CMD_CLONE_VEHICLE_FROM_TEMPLATE, CmdCloneVehicleFromTemplate,             CMD_NO_TEST, CMDT_VEHICLE_CONSTRUCTION  ) // NewGRF callbacks influence building and refitting making it impossible to correctly estimate the cost
DEF_CMD_PROC  (CMD_START_STOP_VEHICLE, CmdStartStopVehicle,                               {}, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_MASS_START_STOP, CmdMassStartStopVehicle,                           {}, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_AUTOREPLACE_VEHICLE, CmdAutoreplaceVehicle,                             {}, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_TEMPLATE_REPLACE_VEHICLE, CmdTemplateReplaceVehicle,               CMD_NO_TEST, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_DEPOT_SELL_ALL_VEHICLES, CmdDepotSellAllVehicles,                           {}, CMDT_VEHICLE_CONSTRUCTION  )
DEF_CMD_PROC  (CMD_DEPOT_MASS_AUTOREPLACE, CmdDepotMassAutoReplace,                 CMD_NO_TEST, CMDT_VEHICLE_CONSTRUCTION  )
DEF_CMD_PROC  (CMD_SET_TRAIN_SPEED_RESTRICTION, CmdSetTrainSpeedRestriction,                       {}, CMDT_VEHICLE_MANAGEMENT    )
DEF_CMD_PROC  (CMD_CREATE_GROUP, CmdCreateGroup,                                    {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_DELETE_GROUP, CmdDeleteGroup,                                    {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_ALTER_GROUP, CmdAlterGroup,                                     {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_CREATE_GROUP_FROM_LIST, CmdCreateGroupFromList,                            {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_ADD_VEHICLE_GROUP, CmdAddVehicleGroup,                                {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_ADD_SHARED_VEHICLE_GROUP, CmdAddSharedVehicleGroup,                          {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_REMOVE_ALL_VEHICLES_GROUP, CmdRemoveAllVehiclesGroup,                         {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SET_GROUP_FLAG, CmdSetGroupFlag,                                   {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SET_GROUP_LIVERY, CmdSetGroupLivery,                                 {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_MOVE_ORDER, CmdMoveOrder,                                      {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_REVERSE_ORDER_LIST, CmdReverseOrderList,                               {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROCEX(CMD_CHANGE_TIMETABLE, CmdChangeTimetable,                                {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_BULK_CHANGE_TIMETABLE, CmdBulkChangeTimetable,                            {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SET_VEHICLE_ON_TIME, CmdSetVehicleOnTime,                               {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_AUTOFILL_TIMETABLE, CmdAutofillTimetable,                              {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_AUTOMATE_TIMETABLE, CmdAutomateTimetable,                              {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_TIMETABLE_SEPARATION, CmdTimetableSeparation,                            {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROCEX(CMD_SET_TIMETABLE_START, CmdSetTimetableStart,                              {}, CMDT_ROUTE_MANAGEMENT      )

DEF_CMD_PROC  (CMD_OPEN_CLOSE_AIRPORT, CmdOpenCloseAirport,                               {}, CMDT_ROUTE_MANAGEMENT      )

DEF_CMD_PROC  (CMD_INSERT_SIGNAL_INSTRUCTION, CmdInsertSignalInstruction,                        {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_MODIFY_SIGNAL_INSTRUCTION, CmdModifySignalInstruction,                        {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_REMOVE_SIGNAL_INSTRUCTION, CmdRemoveSignalInstruction,                        {}, CMDT_OTHER_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SIGNAL_PROGRAM_MGMT, CmdSignalProgramMgmt,                              {}, CMDT_OTHER_MANAGEMENT      )

DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH, CmdScheduledDispatch,                              {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROCEX(CMD_SCHEDULED_DISPATCH_ADD, CmdScheduledDispatchAdd,                           {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_REMOVE, CmdScheduledDispatchRemove,                        {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_SET_DURATION, CmdScheduledDispatchSetDuration,                   {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROCEX(CMD_SCHEDULED_DISPATCH_SET_START_DATE, CmdScheduledDispatchSetStartDate,                  {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_SET_DELAY, CmdScheduledDispatchSetDelay,                      {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_SET_REUSE_SLOTS, CmdScheduledDispatchSetReuseSlots,                 {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_RESET_LAST_DISPATCH, CmdScheduledDispatchResetLastDispatch,             {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_CLEAR, CmdScheduledDispatchClear,                         {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROCEX(CMD_SCHEDULED_DISPATCH_ADD_NEW_SCHEDULE, CmdScheduledDispatchAddNewSchedule,                {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_REMOVE_SCHEDULE, CmdScheduledDispatchRemoveSchedule,                {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_RENAME_SCHEDULE, CmdScheduledDispatchRenameSchedule,                {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_DUPLICATE_SCHEDULE, CmdScheduledDispatchDuplicateSchedule,             {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_APPEND_VEHICLE_SCHEDULE, CmdScheduledDispatchAppendVehicleSchedules,        {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_ADJUST, CmdScheduledDispatchAdjust,                        {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_SWAP_SCHEDULES, CmdScheduledDispatchSwapSchedules,                 {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROCEX(CMD_SCHEDULED_DISPATCH_SET_SLOT_FLAGS, CmdScheduledDispatchSetSlotFlags,                  {}, CMDT_ROUTE_MANAGEMENT      )
DEF_CMD_PROC  (CMD_SCHEDULED_DISPATCH_RENAME_TAG, CmdScheduledDispatchRenameTag,                     {}, CMDT_ROUTE_MANAGEMENT      )

DEF_CMD_DIRECT(CMD_DESYNC_CHECK, CmdDesyncCheck, CommandEmptyPayload, CMD_SERVER, CMDT_SERVER_SETTING)

#endif /* COMMAND_TYPE_H */
