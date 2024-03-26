/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command.cpp Handling of commands. */

#include "stdafx.h"
#include "landscape.h"
#include "error.h"
#include "gui.h"
#include "command_func.h"
#include "command_aux.h"
#include "network/network_type.h"
#include "network/network.h"
#include "genworld.h"
#include "strings_func.h"
#include "texteff.hpp"
#include "town.h"
#include "date_func.h"
#include "company_func.h"
#include "company_base.h"
#include "signal_func.h"
#include "core/backup_type.hpp"
#include "object_base.h"
#include "newgrf_text.h"
#include "string_func.h"
#include "scope_info.h"
#include "core/random_func.hpp"
#include "settings_func.h"
#include "signal_func.h"
#include "debug_settings.h"
#include "debug_desync.h"
#include "order_backup.h"
#include "core/ring_buffer.hpp"
#include "core/checksum_func.hpp"
#include "3rdparty/nlohmann/json.hpp"
#include <array>

#include "table/strings.h"

#include "safeguards.h"

CommandProc CmdBuildRailroadTrack;
CommandProc CmdRemoveRailroadTrack;
CommandProc CmdBuildSingleRail;
CommandProc CmdRemoveSingleRail;

CommandProc CmdLandscapeClear;

CommandProc CmdBuildBridge;

CommandProcEx CmdBuildRailStation;
CommandProc CmdRemoveFromRailStation;
CommandProc CmdConvertRail;
CommandProc CmdConvertRailTrack;

CommandProc CmdBuildSingleSignal;
CommandProc CmdRemoveSingleSignal;

CommandProc CmdTerraformLand;

CommandProc CmdBuildObject;
CommandProc CmdPurchaseLandArea;
CommandProc CmdBuildObjectArea;
CommandProc CmdBuildHouse;
CommandProc CmdSellLandArea;

CommandProc CmdBuildTunnel;

CommandProc CmdBuildTrainDepot;
CommandProcEx CmdBuildRailWaypoint;
CommandProcEx CmdBuildRoadWaypoint;
CommandProc CmdRenameWaypoint;
CommandProc CmdSetWaypointLabelHidden;
CommandProc CmdRemoveFromRailWaypoint;

CommandProcEx CmdBuildRoadStop;
CommandProc CmdRemoveRoadStop;

CommandProc CmdBuildLongRoad;
CommandProc CmdRemoveLongRoad;
CommandProc CmdBuildRoad;

CommandProc CmdBuildRoadDepot;

CommandProc CmdConvertRoad;

CommandProc CmdBuildAirport;

CommandProc CmdBuildDock;

CommandProc CmdBuildShipDepot;

CommandProc CmdBuildBuoy;

CommandProc CmdPlantTree;

CommandProc CmdMoveRailVehicle;

CommandProc CmdBuildVehicle;
CommandProc CmdSellVehicle;
CommandProc CmdRefitVehicle;
CommandProc CmdSendVehicleToDepot;
CommandProc CmdSetVehicleVisibility;

CommandProc CmdForceTrainProceed;
CommandProc CmdReverseTrainDirection;

CommandProc CmdClearOrderBackup;
CommandProcEx CmdModifyOrder;
CommandProc CmdSkipToOrder;
CommandProc CmdDeleteOrder;
CommandProcEx CmdInsertOrder;
CommandProc CmdDuplicateOrder;
CommandProc CmdMassChangeOrder;
CommandProc CmdChangeServiceInt;

CommandProc CmdBuildIndustry;
CommandProc CmdIndustrySetFlags;
CommandProc CmdIndustrySetExclusivity;
CommandProc CmdIndustrySetText;
CommandProc CmdIndustrySetProduction;

CommandProc CmdSetCompanyManagerFace;
CommandProc CmdSetCompanyColour;

CommandProc CmdIncreaseLoan;
CommandProc CmdDecreaseLoan;
CommandProcEx CmdSetCompanyMaxLoan;

CommandProc CmdWantEnginePreview;
CommandProc CmdEngineCtrl;

CommandProc CmdRenameVehicle;
CommandProc CmdRenameEngine;

CommandProc CmdRenameCompany;
CommandProc CmdRenamePresident;

CommandProc CmdRenameStation;
CommandProc CmdRenameDepot;

CommandProc CmdExchangeStationNames;
CommandProc CmdSetStationCargoAllowedSupply;

CommandProc CmdPlaceSign;
CommandProc CmdRenameSign;

CommandProc CmdTurnRoadVeh;

CommandProc CmdPause;

CommandProc CmdBuyShareInCompany;
CommandProc CmdSellShareInCompany;
CommandProc CmdBuyCompany;
CommandProc CmdDeclineBuyCompany;

CommandProc CmdFoundTown;
CommandProc CmdRenameTown;
CommandProc CmdRenameTownNonAdmin;
CommandProc CmdDoTownAction;
CommandProc CmdOverrideTownSetting;
CommandProc CmdOverrideTownSettingNonAdmin;
CommandProc CmdTownGrowthRate;
CommandProc CmdTownRating;
CommandProc CmdTownCargoGoal;
CommandProc CmdTownSetText;
CommandProc CmdExpandTown;
CommandProc CmdDeleteTown;

CommandProc CmdChangeSetting;
CommandProc CmdChangeCompanySetting;

CommandProc CmdOrderRefit;
CommandProc CmdCloneOrder;

CommandProc CmdClearArea;

CommandProcEx CmdGiveMoney;
CommandProcEx CmdMoneyCheat;
CommandProcEx CmdMoneyCheatAdmin;
CommandProcEx CmdChangeBankBalance;
CommandProc CmdCheatSetting;
CommandProc CmdBuildCanal;
CommandProc CmdBuildLock;

CommandProc CmdCreateSubsidy;
CommandProc CmdCompanyCtrl;
CommandProc CmdCustomNewsItem;
CommandProc CmdCreateGoal;
CommandProc CmdRemoveGoal;
CommandProcEx CmdSetGoalDestination;
CommandProc CmdSetGoalText;
CommandProc CmdSetGoalProgress;
CommandProc CmdSetGoalCompleted;
CommandProcEx CmdGoalQuestion;
CommandProc CmdGoalQuestionAnswer;
CommandProc CmdCreateStoryPage;
CommandProc CmdCreateStoryPageElement;
CommandProc CmdUpdateStoryPageElement;
CommandProc CmdSetStoryPageTitle;
CommandProc CmdSetStoryPageDate;
CommandProc CmdShowStoryPage;
CommandProc CmdRemoveStoryPage;
CommandProc CmdRemoveStoryPageElement;
CommandProc CmdScrollViewport;
CommandProc CmdStoryPageButton;

CommandProc CmdLevelLand;

CommandProcEx CmdBuildSignalTrack;
CommandProcEx CmdRemoveSignalTrack;

CommandProc CmdSetAutoReplace;

CommandProc CmdToggleReuseDepotVehicles;
CommandProc CmdToggleKeepRemainingVehicles;
CommandProc CmdSetRefitAsTemplate;
CommandProc CmdToggleTemplateReplaceOldOnly;
CommandProc CmdRenameTemplateReplace;

CommandProc CmdVirtualTrainFromTemplateVehicle;
CommandProc CmdVirtualTrainFromTrain;
CommandProc CmdDeleteVirtualTrain;
CommandProc CmdBuildVirtualRailVehicle;
CommandProc CmdReplaceTemplateVehicle;
CommandProc CmdMoveVirtualRailVehicle;
CommandProc CmdSellVirtualVehicle;

CommandProc CmdTemplateVehicleFromTrain;
CommandProc CmdDeleteTemplateVehicle;

CommandProc CmdIssueTemplateReplacement;
CommandProc CmdDeleteTemplateReplacement;

CommandProc CmdCloneVehicle;
CommandProc CmdCloneVehicleFromTemplate;
CommandProc CmdStartStopVehicle;
CommandProc CmdMassStartStopVehicle;
CommandProc CmdAutoreplaceVehicle;
CommandProc CmdTemplateReplaceVehicle;
CommandProc CmdDepotSellAllVehicles;
CommandProc CmdDepotMassAutoReplace;
CommandProc CmdSetTrainSpeedRestriction;

CommandProc CmdCreateGroup;
CommandProc CmdAlterGroup;
CommandProc CmdDeleteGroup;
CommandProc CmdCreateGroupFromList;
CommandProc CmdAddVehicleGroup;
CommandProc CmdAddSharedVehicleGroup;
CommandProc CmdRemoveAllVehiclesGroup;
CommandProc CmdSetGroupFlag;
CommandProc CmdSetGroupLivery;

CommandProc CmdMoveOrder;
CommandProc CmdReverseOrderList;
CommandProcEx CmdChangeTimetable;
CommandProc CmdBulkChangeTimetable;
CommandProc CmdSetVehicleOnTime;
CommandProc CmdAutofillTimetable;
CommandProc CmdAutomateTimetable;
CommandProc CmdTimetableSeparation;
CommandProcEx CmdSetTimetableStart;

CommandProc CmdOpenCloseAirport;

CommandProcEx CmdCreateLeagueTable;
CommandProcEx CmdCreateLeagueTableElement;
CommandProc CmdUpdateLeagueTableElementData;
CommandProcEx CmdUpdateLeagueTableElementScore;
CommandProc CmdRemoveLeagueTableElement;

CommandProc CmdProgramSignalTraceRestrict;
CommandProc CmdCreateTraceRestrictSlot;
CommandProc CmdAlterTraceRestrictSlot;
CommandProc CmdDeleteTraceRestrictSlot;
CommandProc CmdAddVehicleTraceRestrictSlot;
CommandProc CmdRemoveVehicleTraceRestrictSlot;
CommandProc CmdCreateTraceRestrictCounter;
CommandProc CmdAlterTraceRestrictCounter;
CommandProc CmdDeleteTraceRestrictCounter;

CommandProc CmdInsertSignalInstruction;
CommandProc CmdModifySignalInstruction;
CommandProc CmdRemoveSignalInstruction;
CommandProc CmdSignalProgramMgmt;

CommandProc CmdScheduledDispatch;
CommandProcEx CmdScheduledDispatchAdd;
CommandProc CmdScheduledDispatchRemove;
CommandProc CmdScheduledDispatchSetDuration;
CommandProcEx CmdScheduledDispatchSetStartDate;
CommandProc CmdScheduledDispatchSetDelay;
CommandProc CmdScheduledDispatchSetReuseSlots;
CommandProc CmdScheduledDispatchResetLastDispatch;
CommandProc CmdScheduledDispatchClear;
CommandProcEx CmdScheduledDispatchAddNewSchedule;
CommandProc CmdScheduledDispatchRemoveSchedule;
CommandProc CmdScheduledDispatchRenameSchedule;
CommandProc CmdScheduledDispatchDuplicateSchedule;
CommandProc CmdScheduledDispatchAppendVehicleSchedules;
CommandProc CmdScheduledDispatchAdjust;
CommandProc CmdScheduledDispatchSwapSchedules;
CommandProcEx CmdScheduledDispatchSetSlotFlags;

CommandProc CmdAddPlan;
CommandProcEx CmdAddPlanLine;
CommandProc CmdRemovePlan;
CommandProc CmdRemovePlanLine;
CommandProc CmdChangePlanVisibility;
CommandProc CmdChangePlanColour;
CommandProc CmdRenamePlan;
CommandProc CmdAcquireUnownedPlan;

CommandProc CmdDesyncCheck;

#define DEF_CMD(proc, flags, type) Command(proc, #proc, (CommandFlags)flags, type)

/**
 * The master command table
 *
 * This table contains all possible CommandProc functions with
 * the flags which belongs to it. The indices are the same
 * as the value from the CMD_* enums.
 */
static const Command _command_proc_table[] = {
	DEF_CMD(CmdBuildRailroadTrack,       CMD_NO_WATER | CMD_AUTO | CMD_P1_TILE, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_RAILROAD_TRACK
	DEF_CMD(CmdRemoveRailroadTrack,                     CMD_AUTO | CMD_P1_TILE, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_RAILROAD_TRACK
	DEF_CMD(CmdBuildSingleRail,          CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_SINGLE_RAIL
	DEF_CMD(CmdRemoveSingleRail,                        CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_SINGLE_RAIL
	DEF_CMD(CmdLandscapeClear,                         CMD_DEITY, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_LANDSCAPE_CLEAR
	DEF_CMD(CmdBuildBridge,  CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_BRIDGE
	DEF_CMD(CmdBuildRailStation,         CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_RAIL_STATION
	DEF_CMD(CmdBuildTrainDepot,          CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_TRAIN_DEPOT
	DEF_CMD(CmdBuildSingleSignal,                       CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_SIGNALS
	DEF_CMD(CmdRemoveSingleSignal,                      CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_SIGNALS
	DEF_CMD(CmdTerraformLand,           CMD_ALL_TILES | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_TERRAFORM_LAND
	DEF_CMD(CmdBuildObject,  CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_OBJECT
	DEF_CMD(CmdPurchaseLandArea, CMD_NO_WATER | CMD_AUTO | CMD_NO_TEST, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_PURCHASE_LAND_AREA
	DEF_CMD(CmdBuildObjectArea,  CMD_NO_WATER | CMD_AUTO | CMD_NO_TEST, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_OBJECT_AREA
	DEF_CMD(CmdBuildHouse,   CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_HOUSE
	DEF_CMD(CmdBuildTunnel,                 CMD_DEITY | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_TUNNEL
	DEF_CMD(CmdRemoveFromRailStation,                          0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_FROM_RAIL_STATION
	DEF_CMD(CmdConvertRail,                                    0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_CONVERT_RAIL
	DEF_CMD(CmdConvertRailTrack,                               0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_CONVERT_RAIL_TRACK
	DEF_CMD(CmdBuildRailWaypoint,                              0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_RAIL_WAYPOINT
	DEF_CMD(CmdBuildRoadWaypoint,                              0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_ROAD_WAYPOINT
	DEF_CMD(CmdRenameWaypoint,                                 0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_WAYPOINT
	DEF_CMD(CmdSetWaypointLabelHidden,                         0, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_WAYPOINT_LABEL_HIDDEN
	DEF_CMD(CmdRemoveFromRailWaypoint,                         0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_FROM_RAIL_WAYPOINT

	DEF_CMD(CmdBuildRoadStop,            CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_ROAD_STOP
	DEF_CMD(CmdRemoveRoadStop,                                 0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_ROAD_STOP
	DEF_CMD(CmdBuildLongRoad,CMD_DEITY | CMD_NO_WATER | CMD_AUTO | CMD_P1_TILE, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_LONG_ROAD
	DEF_CMD(CmdRemoveLongRoad,            CMD_NO_TEST | CMD_AUTO | CMD_P1_TILE, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_LONG_ROAD; towns may disallow removing road bits (as they are connected) in test, but in exec they're removed and thus removing is allowed.
	DEF_CMD(CmdBuildRoad,    CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_ROAD
	DEF_CMD(CmdBuildRoadDepot,           CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_ROAD_DEPOT
	DEF_CMD(CmdConvertRoad,                                    0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_CONVERT_ROAD

	DEF_CMD(CmdBuildAirport,             CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_AIRPORT
	DEF_CMD(CmdBuildDock,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_DOCK
	DEF_CMD(CmdBuildShipDepot,                          CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_SHIP_DEPOT
	DEF_CMD(CmdBuildBuoy,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_BUOY
	DEF_CMD(CmdPlantTree,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_PLANT_TREE

	DEF_CMD(CmdBuildVehicle,                       CMD_CLIENT_ID, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_BUILD_VEHICLE
	DEF_CMD(CmdSellVehicle,                        CMD_CLIENT_ID, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_SELL_VEHICLE
	DEF_CMD(CmdRefitVehicle,                                   0, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_REFIT_VEHICLE
	DEF_CMD(CmdSendVehicleToDepot,                             0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_SEND_VEHICLE_TO_DEPOT
	DEF_CMD(CmdSetVehicleVisibility,                           0, CMDT_COMPANY_SETTING       ), // CMD_SET_VEHICLE_VISIBILITY

	DEF_CMD(CmdMoveRailVehicle,                                0, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_MOVE_RAIL_VEHICLE
	DEF_CMD(CmdForceTrainProceed,                              0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_FORCE_TRAIN_PROCEED
	DEF_CMD(CmdReverseTrainDirection,                          0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_REVERSE_TRAIN_DIRECTION

	DEF_CMD(CmdClearOrderBackup,                   CMD_CLIENT_ID, CMDT_SERVER_SETTING        ), // CMD_CLEAR_ORDER_BACKUP
	DEF_CMD(CmdModifyOrder,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_MODIFY_ORDER
	DEF_CMD(CmdSkipToOrder,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SKIP_TO_ORDER
	DEF_CMD(CmdDeleteOrder,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_DELETE_ORDER
	DEF_CMD(CmdInsertOrder,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_INSERT_ORDER
	DEF_CMD(CmdDuplicateOrder,                                 0, CMDT_ROUTE_MANAGEMENT      ), // CMD_DUPLICATE_ORDER
	DEF_CMD(CmdMassChangeOrder,                                0, CMDT_ROUTE_MANAGEMENT      ), // CMD_MASS_CHANGE_ORDER

	DEF_CMD(CmdChangeServiceInt,                               0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_CHANGE_SERVICE_INT

	DEF_CMD(CmdBuildIndustry,                          CMD_DEITY, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_INDUSTRY
	DEF_CMD(CmdIndustrySetFlags,        CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_INDUSTRY_SET_FLAGS
	DEF_CMD(CmdIndustrySetExclusivity,  CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_INDUSTRY_SET_EXCLUSIVITY
	DEF_CMD(CmdIndustrySetText,         CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_INDUSTRY_SET_TEXT
	DEF_CMD(CmdIndustrySetProduction,                  CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_INDUSTRY_SET_PRODUCTION

	DEF_CMD(CmdSetCompanyManagerFace,                          0, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_COMPANY_MANAGER_FACE
	DEF_CMD(CmdSetCompanyColour,                               0, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_COMPANY_COLOUR

	DEF_CMD(CmdIncreaseLoan,                                   0, CMDT_MONEY_MANAGEMENT      ), // CMD_INCREASE_LOAN
	DEF_CMD(CmdDecreaseLoan,                                   0, CMDT_MONEY_MANAGEMENT      ), // CMD_DECREASE_LOAN
	DEF_CMD(CmdSetCompanyMaxLoan,                      CMD_DEITY, CMDT_MONEY_MANAGEMENT      ), // CMD_SET_COMPANY_MAX_LOAN

	DEF_CMD(CmdWantEnginePreview,                              0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_WANT_ENGINE_PREVIEW
	DEF_CMD(CmdEngineCtrl,                             CMD_DEITY, CMDT_VEHICLE_MANAGEMENT    ), // CMD_ENGINE_CTRL

	DEF_CMD(CmdRenameVehicle,                                  0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_VEHICLE
	DEF_CMD(CmdRenameEngine,                          CMD_SERVER, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_ENGINE

	DEF_CMD(CmdRenameCompany,                                  0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_COMPANY
	DEF_CMD(CmdRenamePresident,                                0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_PRESIDENT

	DEF_CMD(CmdRenameStation,                                  0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_STATION
	DEF_CMD(CmdRenameDepot,                                    0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_DEPOT

	DEF_CMD(CmdExchangeStationNames,                           0, CMDT_OTHER_MANAGEMENT      ), // CMD_EXCHANGE_STATION_NAMES
	DEF_CMD(CmdSetStationCargoAllowedSupply,                   0, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_STATION_CARGO_ALLOWED_SUPPLY

	DEF_CMD(CmdPlaceSign,                CMD_LOG_AUX | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_PLACE_SIGN
	DEF_CMD(CmdRenameSign,               CMD_LOG_AUX | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_SIGN

	DEF_CMD(CmdTurnRoadVeh,                                    0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_TURN_ROADVEH

	DEF_CMD(CmdPause,                    CMD_SERVER | CMD_NO_EST, CMDT_SERVER_SETTING        ), // CMD_PAUSE

	DEF_CMD(CmdBuyShareInCompany,                              0, CMDT_MONEY_MANAGEMENT      ), // CMD_BUY_SHARE_IN_COMPANY
	DEF_CMD(CmdSellShareInCompany,                             0, CMDT_MONEY_MANAGEMENT      ), // CMD_SELL_SHARE_IN_COMPANY
	DEF_CMD(CmdBuyCompany,                                     0, CMDT_MONEY_MANAGEMENT      ), // CMD_BUY_COMPANY
	DEF_CMD(CmdDeclineBuyCompany,                              0, CMDT_SERVER_SETTING        ), // CMD_DECLINE_BUY_COMPANY

	DEF_CMD(CmdFoundTown,                CMD_DEITY | CMD_NO_TEST, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_FOUND_TOWN; founding random town can fail only in exec run
	DEF_CMD(CmdRenameTown,                CMD_DEITY | CMD_SERVER, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_TOWN
	DEF_CMD(CmdRenameTownNonAdmin,                             0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_TOWN_NON_ADMIN
	DEF_CMD(CmdDoTownAction,                                   0, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_DO_TOWN_ACTION
	DEF_CMD(CmdOverrideTownSetting,       CMD_DEITY | CMD_SERVER, CMDT_OTHER_MANAGEMENT      ), // CMD_TOWN_SETTING_OVERRIDE
	DEF_CMD(CmdOverrideTownSettingNonAdmin,                    0, CMDT_OTHER_MANAGEMENT      ), // CMD_TOWN_SETTING_OVERRIDE_NON_ADMIN
	DEF_CMD(CmdTownCargoGoal,            CMD_LOG_AUX | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_TOWN_CARGO_GOAL
	DEF_CMD(CmdTownGrowthRate,           CMD_LOG_AUX | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_TOWN_GROWTH_RATE
	DEF_CMD(CmdTownRating,               CMD_LOG_AUX | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_TOWN_RATING
	DEF_CMD(CmdTownSetText,    CMD_LOG_AUX | CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT ), // CMD_TOWN_SET_TEXT
	DEF_CMD(CmdExpandTown,                             CMD_DEITY, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_EXPAND_TOWN
	DEF_CMD(CmdDeleteTown,                           CMD_OFFLINE, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_DELETE_TOWN

	DEF_CMD(CmdOrderRefit,                                     0, CMDT_ROUTE_MANAGEMENT      ), // CMD_ORDER_REFIT
	DEF_CMD(CmdCloneOrder,                                     0, CMDT_ROUTE_MANAGEMENT      ), // CMD_CLONE_ORDER

	DEF_CMD(CmdClearArea,                            CMD_NO_TEST, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_CLEAR_AREA; destroying multi-tile houses makes town rating differ between test and execution

	DEF_CMD(CmdMoneyCheat,                                     0, CMDT_CHEAT                 ), // CMD_MONEY_CHEAT
	DEF_CMD(CmdMoneyCheatAdmin,                    CMD_SERVER_NS, CMDT_CHEAT                 ), // CMD_MONEY_CHEAT_ADMIN
	DEF_CMD(CmdChangeBankBalance,                      CMD_DEITY, CMDT_MONEY_MANAGEMENT      ), // CMD_CHANGE_BANK_BALANCE
	DEF_CMD(CmdCheatSetting,                          CMD_SERVER, CMDT_CHEAT                 ), // CMD_CHEAT_SETTING
	DEF_CMD(CmdBuildCanal,                  CMD_DEITY | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_CANAL
	DEF_CMD(CmdCreateSubsidy,                          CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_SUBSIDY
	DEF_CMD(CmdCompanyCtrl, CMD_SPECTATOR | CMD_CLIENT_ID | CMD_NO_EST, CMDT_SERVER_SETTING  ), // CMD_COMPANY_CTRL
	DEF_CMD(CmdCustomNewsItem,          CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_CUSTOM_NEWS_ITEM
	DEF_CMD(CmdCreateGoal,              CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_GOAL
	DEF_CMD(CmdRemoveGoal,                             CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_REMOVE_GOAL
	DEF_CMD(CmdSetGoalDestination,                     CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_GOAL_DESTINATION
	DEF_CMD(CmdSetGoalText,             CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_GOAL_TEXT
	DEF_CMD(CmdSetGoalProgress,         CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_GOAL_PROGRESS
	DEF_CMD(CmdSetGoalCompleted,        CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_GOAL_COMPLETED
	DEF_CMD(CmdGoalQuestion,            CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_GOAL_QUESTION
	DEF_CMD(CmdGoalQuestionAnswer,                     CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_GOAL_QUESTION_ANSWER
	DEF_CMD(CmdCreateStoryPage,         CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_STORY_PAGE
	DEF_CMD(CmdCreateStoryPageElement,  CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_STORY_PAGE_ELEMENT
	DEF_CMD(CmdUpdateStoryPageElement,  CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_UPDATE_STORY_PAGE_ELEMENT
	DEF_CMD(CmdSetStoryPageTitle,       CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_STORY_PAGE_TITLE
	DEF_CMD(CmdSetStoryPageDate,                       CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_SET_STORY_PAGE_DATE
	DEF_CMD(CmdShowStoryPage,                          CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_SHOW_STORY_PAGE
	DEF_CMD(CmdRemoveStoryPage,                        CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_REMOVE_STORY_PAGE
	DEF_CMD(CmdRemoveStoryPageElement,                 CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_REMOVE_STORY_ELEMENT_PAGE
	DEF_CMD(CmdScrollViewport,                         CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_SCROLL_VIEWPORT
	DEF_CMD(CmdStoryPageButton,                        CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT      ), // CMD_STORY_PAGE_BUTTON

	DEF_CMD(CmdLevelLand, CMD_ALL_TILES | CMD_NO_TEST | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_LEVEL_LAND; test run might clear tiles multiple times, in execution that only happens once

	DEF_CMD(CmdBuildLock,                               CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_LOCK

	DEF_CMD(CmdBuildSignalTrack,                        CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_BUILD_SIGNAL_TRACK
	DEF_CMD(CmdRemoveSignalTrack,                       CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION), // CMD_REMOVE_SIGNAL_TRACK

	DEF_CMD(CmdGiveMoney,                                      0, CMDT_MONEY_MANAGEMENT      ), // CMD_GIVE_MONEY
	DEF_CMD(CmdChangeSetting,                         CMD_SERVER, CMDT_SERVER_SETTING        ), // CMD_CHANGE_SETTING
	DEF_CMD(CmdChangeCompanySetting,                           0, CMDT_COMPANY_SETTING       ), // CMD_CHANGE_COMPANY_SETTING
	DEF_CMD(CmdSetAutoReplace,                                 0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_SET_AUTOREPLACE

	DEF_CMD(CmdToggleReuseDepotVehicles,           CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    ), // CMD_TOGGLE_REUSE_DEPOT_VEHICLES
	DEF_CMD(CmdToggleKeepRemainingVehicles,        CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    ), // CMD_TOGGLE_KEEP_REMAINING_VEHICLES
	DEF_CMD(CmdSetRefitAsTemplate,                 CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    ), // CMD_SET_REFIT_AS_TEMPLATE
	DEF_CMD(CmdToggleTemplateReplaceOldOnly,       CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    ), // CMD_TOGGLE_TMPL_REPLACE_OLD_ONLY
	DEF_CMD(CmdRenameTemplateReplace,              CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    ), // CMD_RENAME_TMPL_REPLACE

	DEF_CMD(CmdVirtualTrainFromTemplateVehicle,   CMD_CLIENT_ID | CMD_NO_TEST | CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT), // CMD_VIRTUAL_TRAIN_FROM_TEMPLATE_VEHICLE
	DEF_CMD(CmdVirtualTrainFromTrain,             CMD_CLIENT_ID | CMD_NO_TEST | CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT), // CMD_VIRTUAL_TRAIN_FROM_TRAIN
	DEF_CMD(CmdDeleteVirtualTrain,                                              CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT), // CMD_DELETE_VIRTUAL_TRAIN
	DEF_CMD(CmdBuildVirtualRailVehicle,           CMD_CLIENT_ID | CMD_NO_TEST | CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT), // CMD_BUILD_VIRTUAL_RAIL_VEHICLE
	DEF_CMD(CmdReplaceTemplateVehicle,                                          CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT), // CMD_REPLACE_TEMPLATE_VEHICLE
	DEF_CMD(CmdMoveVirtualRailVehicle,                                          CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT), // CMD_MOVE_VIRTUAL_RAIL_VEHICLE
	DEF_CMD(CmdSellVirtualVehicle,                              CMD_CLIENT_ID | CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT), // CMD_SELL_VIRTUAL_VEHICLE

	DEF_CMD(CmdTemplateVehicleFromTrain,           CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    ), // CMD_CLONE_TEMPLATE_VEHICLE_FROM_TRAIN
	DEF_CMD(CmdDeleteTemplateVehicle,              CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    ), // CMD_DELETE_TEMPLATE_VEHICLE

	DEF_CMD(CmdIssueTemplateReplacement,           CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    ), // CMD_ISSUE_TEMPLATE_REPLACEMENT
	DEF_CMD(CmdDeleteTemplateReplacement,          CMD_ALL_TILES, CMDT_VEHICLE_MANAGEMENT    ), // CMD_DELETE_TEMPLATE_REPLACEMENT

	DEF_CMD(CmdCloneVehicle,                         CMD_NO_TEST, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_CLONE_VEHICLE; NewGRF callbacks influence building and refitting making it impossible to correctly estimate the cost
	DEF_CMD(CmdCloneVehicleFromTemplate,             CMD_NO_TEST, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_CLONE_VEHICLE_FROM_TEMPLATE; NewGRF callbacks influence building and refitting making it impossible to correctly estimate the cost
	DEF_CMD(CmdStartStopVehicle,                               0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_START_STOP_VEHICLE
	DEF_CMD(CmdMassStartStopVehicle,                           0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_MASS_START_STOP
	DEF_CMD(CmdAutoreplaceVehicle,                             0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_AUTOREPLACE_VEHICLE
	DEF_CMD(CmdTemplateReplaceVehicle,               CMD_NO_TEST, CMDT_VEHICLE_MANAGEMENT    ), // CMD_TEMPLATE_REPLACE_VEHICLE
	DEF_CMD(CmdDepotSellAllVehicles,                           0, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_DEPOT_SELL_ALL_VEHICLES
	DEF_CMD(CmdDepotMassAutoReplace,                 CMD_NO_TEST, CMDT_VEHICLE_CONSTRUCTION  ), // CMD_DEPOT_MASS_AUTOREPLACE
	DEF_CMD(CmdSetTrainSpeedRestriction,                       0, CMDT_VEHICLE_MANAGEMENT    ), // CMD_SET_TRAIN_SPEED_RESTRICTION
	DEF_CMD(CmdCreateGroup,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_CREATE_GROUP
	DEF_CMD(CmdDeleteGroup,                                    0, CMDT_ROUTE_MANAGEMENT      ), // CMD_DELETE_GROUP
	DEF_CMD(CmdAlterGroup,                                     0, CMDT_OTHER_MANAGEMENT      ), // CMD_ALTER_GROUP
	DEF_CMD(CmdCreateGroupFromList,                            0, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_GROUP_FROM_LIST
	DEF_CMD(CmdAddVehicleGroup,                                0, CMDT_ROUTE_MANAGEMENT      ), // CMD_ADD_VEHICLE_GROUP
	DEF_CMD(CmdAddSharedVehicleGroup,                          0, CMDT_ROUTE_MANAGEMENT      ), // CMD_ADD_SHARE_VEHICLE_GROUP
	DEF_CMD(CmdRemoveAllVehiclesGroup,                         0, CMDT_ROUTE_MANAGEMENT      ), // CMD_REMOVE_ALL_VEHICLES_GROUP
	DEF_CMD(CmdSetGroupFlag,                                   0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SET_GROUP_FLAG
	DEF_CMD(CmdSetGroupLivery,                                 0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SET_GROUP_LIVERY
	DEF_CMD(CmdMoveOrder,                                      0, CMDT_ROUTE_MANAGEMENT      ), // CMD_MOVE_ORDER
	DEF_CMD(CmdReverseOrderList,                               0, CMDT_ROUTE_MANAGEMENT      ), // CMD_REVERSE_ORDER_LIST
	DEF_CMD(CmdChangeTimetable,                                0, CMDT_ROUTE_MANAGEMENT      ), // CMD_CHANGE_TIMETABLE
	DEF_CMD(CmdBulkChangeTimetable,                            0, CMDT_ROUTE_MANAGEMENT      ), // CMD_BULK_CHANGE_TIMETABLE
	DEF_CMD(CmdSetVehicleOnTime,                               0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SET_VEHICLE_ON_TIME
	DEF_CMD(CmdAutofillTimetable,                              0, CMDT_ROUTE_MANAGEMENT      ), // CMD_AUTOFILL_TIMETABLE
	DEF_CMD(CmdAutomateTimetable,                              0, CMDT_ROUTE_MANAGEMENT      ), // CMD_AUTOMATE_TIMETABLE
	DEF_CMD(CmdTimetableSeparation,                            0, CMDT_ROUTE_MANAGEMENT      ), // CMD_TIMETABLE_SEPARATION
	DEF_CMD(CmdSetTimetableStart,                              0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SET_TIMETABLE_START

	DEF_CMD(CmdOpenCloseAirport,                               0, CMDT_ROUTE_MANAGEMENT      ), // CMD_OPEN_CLOSE_AIRPORT

	DEF_CMD(CmdCreateLeagueTable,                             CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_LEAGUE_TABLE
	DEF_CMD(CmdCreateLeagueTableElement,                      CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_LEAGUE_TABLE_ELEMENT
	DEF_CMD(CmdUpdateLeagueTableElementData,                  CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_UPDATE_LEAGUE_TABLE_ELEMENT_DATA
	DEF_CMD(CmdUpdateLeagueTableElementScore,                 CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_UPDATE_LEAGUE_TABLE_ELEMENT_SCORE
	DEF_CMD(CmdRemoveLeagueTableElement,                                     CMD_DEITY, CMDT_OTHER_MANAGEMENT      ), // CMD_REMOVE_LEAGUE_TABLE_ELEMENT

	DEF_CMD(CmdProgramSignalTraceRestrict,                     0, CMDT_OTHER_MANAGEMENT      ), // CMD_PROGRAM_TRACERESTRICT_SIGNAL
	DEF_CMD(CmdCreateTraceRestrictSlot,                        0, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_TRACERESTRICT_SLOT
	DEF_CMD(CmdAlterTraceRestrictSlot,                         0, CMDT_OTHER_MANAGEMENT      ), // CMD_ALTER_TRACERESTRICT_SLOT
	DEF_CMD(CmdDeleteTraceRestrictSlot,                        0, CMDT_OTHER_MANAGEMENT      ), // CMD_DELETE_TRACERESTRICT_SLOT
	DEF_CMD(CmdAddVehicleTraceRestrictSlot,                    0, CMDT_OTHER_MANAGEMENT      ), // CMD_ADD_VEHICLE_TRACERESTRICT_SLOT
	DEF_CMD(CmdRemoveVehicleTraceRestrictSlot,                 0, CMDT_OTHER_MANAGEMENT      ), // CMD_REMOVE_VEHICLE_TRACERESTRICT_SLOT
	DEF_CMD(CmdCreateTraceRestrictCounter,                     0, CMDT_OTHER_MANAGEMENT      ), // CMD_CREATE_TRACERESTRICT_COUNTER
	DEF_CMD(CmdAlterTraceRestrictCounter,                      0, CMDT_OTHER_MANAGEMENT      ), // CMD_ALTER_TRACERESTRICT_COUNTER
	DEF_CMD(CmdDeleteTraceRestrictCounter,                     0, CMDT_OTHER_MANAGEMENT      ), // CMD_DELETE_TRACERESTRICT_COUNTER

	DEF_CMD(CmdInsertSignalInstruction,                        0, CMDT_OTHER_MANAGEMENT      ), // CMD_INSERT_SIGNAL_INSTRUCTION
	DEF_CMD(CmdModifySignalInstruction,                        0, CMDT_OTHER_MANAGEMENT      ), // CMD_MODIFY_SIGNAL_INSTRUCTION
	DEF_CMD(CmdRemoveSignalInstruction,                        0, CMDT_OTHER_MANAGEMENT      ), // CMD_REMOVE_SIGNAL_INSTRUCTION
	DEF_CMD(CmdSignalProgramMgmt,                              0, CMDT_OTHER_MANAGEMENT      ), // CMD_SIGNAL_PROGRAM_MGMT

	DEF_CMD(CmdScheduledDispatch,                              0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH
	DEF_CMD(CmdScheduledDispatchAdd,                           0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_ADD
	DEF_CMD(CmdScheduledDispatchRemove,                        0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_REMOVE
	DEF_CMD(CmdScheduledDispatchSetDuration,                   0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_SET_DURATION
	DEF_CMD(CmdScheduledDispatchSetStartDate,                  0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_SET_START_DATE
	DEF_CMD(CmdScheduledDispatchSetDelay,                      0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_SET_DELAY
	DEF_CMD(CmdScheduledDispatchSetReuseSlots,                 0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_SET_REUSE_SLOTS
	DEF_CMD(CmdScheduledDispatchResetLastDispatch,             0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_RESET_LAST_DISPATCH
	DEF_CMD(CmdScheduledDispatchClear,                         0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_CLEAR
	DEF_CMD(CmdScheduledDispatchAddNewSchedule,                0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_ADD_NEW_SCHEDULE
	DEF_CMD(CmdScheduledDispatchRemoveSchedule,                0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_REMOVE_SCHEDULE
	DEF_CMD(CmdScheduledDispatchRenameSchedule,                0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_RENAME_SCHEDULE
	DEF_CMD(CmdScheduledDispatchDuplicateSchedule,             0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_DUPLICATE_SCHEDULE
	DEF_CMD(CmdScheduledDispatchAppendVehicleSchedules,        0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_APPEND_VEHICLE_SCHEDULE
	DEF_CMD(CmdScheduledDispatchAdjust,                        0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_ADJUST
	DEF_CMD(CmdScheduledDispatchSwapSchedules,                 0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_SWAP_SCHEDULES
	DEF_CMD(CmdScheduledDispatchSetSlotFlags,                  0, CMDT_ROUTE_MANAGEMENT      ), // CMD_SCHEDULED_DISPATCH_SET_SLOT_FLAGS

	DEF_CMD(CmdAddPlan,                                        0, CMDT_OTHER_MANAGEMENT      ), // CMD_ADD_PLAN
	DEF_CMD(CmdAddPlanLine,                          CMD_NO_TEST, CMDT_OTHER_MANAGEMENT      ), // CMD_ADD_PLAN_LINE
	DEF_CMD(CmdRemovePlan,                                     0, CMDT_OTHER_MANAGEMENT      ), // CMD_REMOVE_PLAN
	DEF_CMD(CmdRemovePlanLine,                                 0, CMDT_OTHER_MANAGEMENT      ), // CMD_REMOVE_PLAN_LINE
	DEF_CMD(CmdChangePlanVisibility,                           0, CMDT_OTHER_MANAGEMENT      ), // CMD_CHANGE_PLAN_VISIBILITY
	DEF_CMD(CmdChangePlanColour,                               0, CMDT_OTHER_MANAGEMENT      ), // CMD_CHANGE_PLAN_COLOUR
	DEF_CMD(CmdRenamePlan,                                     0, CMDT_OTHER_MANAGEMENT      ), // CMD_RENAME_PLAN
	DEF_CMD(CmdAcquireUnownedPlan,                 CMD_SERVER_NS, CMDT_OTHER_MANAGEMENT      ), // CMD_ACQUIRE_UNOWNED_PLAN

	DEF_CMD(CmdDesyncCheck,                           CMD_SERVER, CMDT_SERVER_SETTING        ), // CMD_DESYNC_CHECK
};
static_assert(lengthof(_command_proc_table) == CMD_END);

ClientID _cmd_client_id = INVALID_CLIENT_ID;

/**
 * List of flags for a command log entry
 */
enum CommandLogEntryFlag : uint16_t {
	CLEF_NONE                =  0x00, ///< no flag is set
	CLEF_CMD_FAILED          =  0x01, ///< command failed
	CLEF_GENERATING_WORLD    =  0x02, ///< generating world
	CLEF_TEXT                =  0x04, ///< have command text
	CLEF_ESTIMATE_ONLY       =  0x08, ///< estimate only
	CLEF_ONLY_SENDING        =  0x10, ///< only sending
	CLEF_MY_CMD              =  0x20, ///< locally generated command
	CLEF_AUX_DATA            =  0x40, ///< have auxiliary data
	CLEF_SCRIPT              =  0x80, ///< command run by AI/game script
	CLEF_TWICE               = 0x100, ///< command logged twice (only sending and execution)
	CLEF_RANDOM              = 0x200, ///< command changed random seed
	CLEF_ORDER_BACKUP        = 0x400, ///< command changed order backups
	CLEF_SCRIPT_ASYNC        = 0x800, ///< command run by AI/game script - asynchronous
};
DECLARE_ENUM_AS_BIT_SET(CommandLogEntryFlag)

extern uint32_t _frame_counter;

struct CommandLogEntry {
	std::string text;
	TileIndex tile;
	uint32_t p1;
	uint32_t p2;
	uint32_t cmd;
	uint64_t p3;
	EconTime::Date date;
	EconTime::DateFract date_fract;
	uint8_t tick_skip_counter;
	CompanyID current_company;
	CompanyID local_company;
	CommandLogEntryFlag log_flags;
	ClientID client_id;
	uint32_t frame_counter;

	CommandLogEntry() { }

	CommandLogEntry(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd, CommandLogEntryFlag log_flags, std::string text)
			: text(text), tile(tile), p1(p1), p2(p2), cmd(cmd), p3(p3), date(EconTime::CurDate()), date_fract(EconTime::CurDateFract()), tick_skip_counter(TickSkipCounter()),
			current_company(_current_company), local_company(_local_company), log_flags(log_flags), client_id(_cmd_client_id), frame_counter(_frame_counter) { }
};

struct CommandLog {
	std::array<CommandLogEntry, 256> log;
	unsigned int count = 0;
	unsigned int next = 0;

	void Reset()
	{
		this->count = 0;
		this->next = 0;
	}
};

static CommandLog _command_log;
static CommandLog _command_log_aux;

struct CommandQueueItem {
	CommandContainer cmd;
	CompanyID company;
};
static ring_buffer<CommandQueueItem> _command_queue;

void ClearCommandLog()
{
	_command_log.Reset();
	_command_log_aux.Reset();
}

static void DumpSubCommandLogEntry(char *&buffer, const char *last, const CommandLogEntry &entry)
{
		auto fc = [&](CommandLogEntryFlag flag, char c) -> char {
			return entry.log_flags & flag ? c : '-';
		};

		auto script_fc = [&]() -> char {
			if (!(entry.log_flags & CLEF_SCRIPT)) return '-';
			return (entry.log_flags & CLEF_SCRIPT_ASYNC) ? 'A' : 'a';
		};

		EconTime::YearMonthDay ymd = EconTime::ConvertDateToYMD(entry.date);
		buffer += seprintf(buffer, last, "%4i-%02i-%02i, %2i, %3i", ymd.year.base(), ymd.month + 1, ymd.day, entry.date_fract, entry.tick_skip_counter);
		if (_networking) {
			buffer += seprintf(buffer, last, ", %08X", entry.frame_counter);
		}
		buffer += seprintf(buffer, last, " | %c%c%c%c%c%c%c%c%c%c%c | ",
				fc(CLEF_ORDER_BACKUP, 'o'), fc(CLEF_RANDOM, 'r'), fc(CLEF_TWICE, '2'),
				script_fc(), fc(CLEF_AUX_DATA, 'b'), fc(CLEF_MY_CMD, 'm'), fc(CLEF_ONLY_SENDING, 's'),
				fc(CLEF_ESTIMATE_ONLY, 'e'), fc(CLEF_TEXT, 't'), fc(CLEF_GENERATING_WORLD, 'g'), fc(CLEF_CMD_FAILED, 'f'));
		buffer += seprintf(buffer, last, " %7d x %7d, p1: 0x%08X, p2: 0x%08X, ",
				TileX(entry.tile), TileY(entry.tile), entry.p1, entry.p2);
		if (entry.p3 != 0) {
			buffer += seprintf(buffer, last, "p3: 0x" OTTD_PRINTFHEX64PAD ", ", entry.p3);
		}
		buffer += seprintf(buffer, last, "cc: %3u, lc: %3u, ", (uint) entry.current_company, (uint) entry.local_company);
		if (_network_server) {
			buffer += seprintf(buffer, last, "client: %4u, ", entry.client_id);
		}
		buffer += seprintf(buffer, last, "cmd: 0x%08X (%s)", entry.cmd, GetCommandName(entry.cmd));

		switch (entry.cmd & CMD_ID_MASK) {
			case CMD_CHANGE_SETTING:
			case CMD_CHANGE_COMPANY_SETTING:
				buffer += seprintf(buffer, last, " [%s]", entry.text.c_str());
				break;
		}
}

static void DumpSubCommandLog(char *&buffer, const char *last, const CommandLog &cmd_log, const unsigned int count, std::function<char *(char *)> &flush)
{
	unsigned int log_index = cmd_log.next;
	for (unsigned int i = 0 ; i < count; i++) {
		if (log_index > 0) {
			log_index--;
		} else {
			log_index = (uint)cmd_log.log.size() - 1;
		}

		buffer += seprintf(buffer, last, " %3u | ", i);

		const CommandLogEntry &entry = cmd_log.log[log_index];
		DumpSubCommandLogEntry(buffer, last, entry);

		buffer += seprintf(buffer, last, "\n");
		if (flush) buffer = flush(buffer);
	}
}

char *DumpCommandLog(char *buffer, const char *last, std::function<char *(char *)> flush)
{
	const unsigned int count = std::min<unsigned int>(_command_log.count, 256);
	buffer += seprintf(buffer, last, "Command Log:\n Showing most recent %u of %u commands\n", count, _command_log.count);
	DumpSubCommandLog(buffer, last, _command_log, count, flush);

	if (_command_log_aux.count > 0) {
		const unsigned int aux_count = std::min<unsigned int>(_command_log_aux.count, 32);
		buffer += seprintf(buffer, last, "\n Showing most recent %u of %u commands (aux log)\n", aux_count, _command_log_aux.count);
		DumpSubCommandLog(buffer, last, _command_log_aux, aux_count, flush);
	}
	if (flush) buffer = flush(buffer);
	return buffer;
}

/**
 * This function range-checks a cmd, and checks if the cmd is not nullptr
 *
 * @param cmd The integer value of a command
 * @return true if the command is valid (and got a CommandProc function)
 */
bool IsValidCommand(uint32_t cmd)
{
	cmd &= CMD_ID_MASK;

	return cmd < lengthof(_command_proc_table) && _command_proc_table[cmd].proc != nullptr;
}

/**
 * This function mask the parameter with CMD_ID_MASK and returns
 * the flags which belongs to the given command.
 *
 * @param cmd The integer value of the command
 * @return The flags for this command
 */
CommandFlags GetCommandFlags(uint32_t cmd)
{
	assert(IsValidCommand(cmd));

	return _command_proc_table[cmd & CMD_ID_MASK].flags;
}

/**
 * This function mask the parameter with CMD_ID_MASK and returns
 * the name which belongs to the given command.
 *
 * @param cmd The integer value of the command
 * @return The name for this command
 */
const char *GetCommandName(uint32_t cmd)
{
	assert(IsValidCommand(cmd));

	return _command_proc_table[cmd & CMD_ID_MASK].name;
}

/**
 * Returns whether the command is allowed while the game is paused.
 * @param cmd The command to check.
 * @return True if the command is allowed while paused, false otherwise.
 */
bool IsCommandAllowedWhilePaused(uint32_t cmd)
{
	/* Lookup table for the command types that are allowed for a given pause level setting. */
	static const int command_type_lookup[] = {
		CMDPL_ALL_ACTIONS,     ///< CMDT_LANDSCAPE_CONSTRUCTION
		CMDPL_NO_LANDSCAPING,  ///< CMDT_VEHICLE_CONSTRUCTION
		CMDPL_NO_LANDSCAPING,  ///< CMDT_MONEY_MANAGEMENT
		CMDPL_NO_CONSTRUCTION, ///< CMDT_VEHICLE_MANAGEMENT
		CMDPL_NO_CONSTRUCTION, ///< CMDT_ROUTE_MANAGEMENT
		CMDPL_NO_CONSTRUCTION, ///< CMDT_OTHER_MANAGEMENT
		CMDPL_NO_CONSTRUCTION, ///< CMDT_COMPANY_SETTING
		CMDPL_NO_ACTIONS,      ///< CMDT_SERVER_SETTING
		CMDPL_NO_ACTIONS,      ///< CMDT_CHEAT
	};
	static_assert(lengthof(command_type_lookup) == CMDT_END);

	assert(IsValidCommand(cmd));
	return _game_mode == GM_EDITOR || command_type_lookup[_command_proc_table[cmd & CMD_ID_MASK].type] <= _settings_game.construction.command_pause_level;
}


static int _docommand_recursive = 0;

struct cmd_text_info_dumper {
	const char *CommandTextInfo(const char *text, const CommandAuxiliaryBase *aux_data)
	{
		char *b = this->buffer;
		const char *last = lastof(this->buffer);
		if (text) {
			b += seprintf(b, last, ", text: length: %u", (uint) strlen(text));
		}
		if (aux_data) {
			b += seprintf(b, last, ", aux data");
		}
		return this->buffer;
	}

private:
	char buffer[64];
};

/**
 * This function executes a given command with the parameters from the #CommandProc parameter list.
 * Depending on the flags parameter it execute or test a command.
 *
 * @param tile The tile to apply the command on (for the #CommandProc)
 * @param p1 Additional data for the command (for the #CommandProc)
 * @param p2 Additional data for the command (for the #CommandProc)
 * @param flags Flags for the command and how to execute the command
 * @param cmd The command-id to execute (a value of the CMD_* enums)
 * @param text The text to pass
 * @param binary_length The length of binary data in text
 * @see CommandProc
 * @return the cost
 */
CommandCost DoCommandEx(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, DoCommandFlag flags, uint32_t cmd, const char *text, const CommandAuxiliaryBase *aux_data)
{
	SCOPE_INFO_FMT([=], "DoCommand: tile: %X (%d x %d), p1: 0x%X, p2: 0x%X, p3: " OTTD_PRINTFHEX64 ", flags: 0x%X, company: %s, cmd: 0x%X (%s)%s",
			tile, TileX(tile), TileY(tile), p1, p2, p3, flags, scope_dumper().CompanyInfo(_current_company), cmd, GetCommandName(cmd), cmd_text_info_dumper().CommandTextInfo(text, aux_data));

	CommandCost res;

	/* Do not even think about executing out-of-bounds tile-commands */
	if (tile != 0 && (tile >= MapSize() || (!IsValidTile(tile) && (flags & DC_ALL_TILES) == 0))) return CMD_ERROR;

	/* Chop of any CMD_MSG or other flags; we don't need those here */
	const Command &command = _command_proc_table[cmd & CMD_ID_MASK];

	_docommand_recursive++;

	/* only execute the test call if it's toplevel, or we're not execing. */
	if (_docommand_recursive == 1 || !(flags & DC_EXEC) ) {
		if (_docommand_recursive == 1) _cleared_object_areas.clear();
		SetTownRatingTestMode(true);
		res = command.Execute(tile, flags & ~DC_EXEC, p1, p2, p3, text, aux_data);
		SetTownRatingTestMode(false);
		if (res.Failed()) {
			goto error;
		}

		if (_docommand_recursive == 1 &&
				!(flags & DC_QUERY_COST) &&
				!(flags & DC_BANKRUPT) &&
				!CheckCompanyHasMoney(res)) { // CheckCompanyHasMoney() modifies 'res' to an error if it fails.
			goto error;
		}

		if (!(flags & DC_EXEC)) {
			_docommand_recursive--;
			return res;
		}
	}

	/* Execute the command here. All cost-relevant functions set the expenses type
	 * themselves to the cost object at some point */
	if (_docommand_recursive == 1) _cleared_object_areas.clear();
	res = command.Execute(tile, flags, p1, p2, p3, text, aux_data);
	if (res.Failed()) {
error:
		_docommand_recursive--;
		return res;
	}

	/* if toplevel, subtract the money. */
	if (--_docommand_recursive == 0 && !(flags & DC_BANKRUPT)) {
		SubtractMoneyFromCompany(res);
	}

	return res;
}

static void DebugLogCommandLogEntry(const CommandLogEntry &entry)
{
	if (_debug_command_level <= 0) return;

	char buffer[256];
	char *b = buffer;
	DumpSubCommandLogEntry(b, lastof(buffer), entry);
	debug_print("command", 0, buffer);
}

static void AppendCommandLogEntry(const CommandCost &res, TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd, CommandLogEntryFlag log_flags, const char *text)
{
	if (res.Failed()) log_flags |= CLEF_CMD_FAILED;
	if (_generating_world) log_flags |= CLEF_GENERATING_WORLD;

	CommandLog &cmd_log = (GetCommandFlags(cmd) & CMD_LOG_AUX) ? _command_log_aux : _command_log;

	if (_networking && cmd_log.count > 0) {
		CommandLogEntry &current = cmd_log.log[(cmd_log.next - 1) % cmd_log.log.size()];
		if (current.log_flags & CLEF_ONLY_SENDING && ((current.log_flags ^ log_flags) & ~(CLEF_SCRIPT | CLEF_MY_CMD)) == CLEF_ONLY_SENDING &&
				current.tile == tile && current.p1 == p1 && current.p2 == p2 && current.p3 == p3 && ((current.cmd ^ cmd) & ~CMD_NETWORK_COMMAND) == 0 &&
				current.date == EconTime::CurDate() && current.date_fract == EconTime::CurDateFract() &&
				current.tick_skip_counter == TickSkipCounter() &&
				current.frame_counter == _frame_counter &&
				current.current_company == _current_company && current.local_company == _local_company) {
			current.log_flags |= log_flags | CLEF_TWICE;
			current.log_flags &= ~CLEF_ONLY_SENDING;
			DebugLogCommandLogEntry(current);
			return;
		}
	}

	std::string str;
	switch (cmd & CMD_ID_MASK) {
		case CMD_CHANGE_SETTING:
		case CMD_CHANGE_COMPANY_SETTING:
			if (text != nullptr) str.assign(text);
			break;
	}

	cmd_log.log[cmd_log.next] = CommandLogEntry(tile, p1, p2, p3, cmd, log_flags, std::move(str));
	DebugLogCommandLogEntry(cmd_log.log[cmd_log.next]);
	cmd_log.next = (cmd_log.next + 1) % cmd_log.log.size();
	cmd_log.count++;
}

/**
 * Toplevel network safe docommand function for the current company. Must not be called recursively.
 * The callback is called when the command succeeded or failed. The parameters
 * \a tile, \a p1, and \a p2 are from the #CommandProc function. The parameter \a cmd is the command to execute.
 * The parameter \a my_cmd is used to indicate if the command is from a company or the server.
 *
 * @param tile The tile to perform a command on (see #CommandProc)
 * @param p1 Additional data for the command (see #CommandProc)
 * @param p2 Additional data for the command (see #CommandProc)
 * @param p3 Additional data for the command (see #CommandProc)
 * @param cmd The command to execute (a CMD_* value)
 * @param callback A callback function to call after the command is finished
 * @param text The text to pass
 * @param my_cmd indicator if the command is from a company or server (to display error messages for a user)
 * @param binary_length The length of binary data in text
 * @return \c true if the command succeeded, else \c false.
 */
bool DoCommandPEx(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd, CommandCallback *callback, const char *text, const CommandAuxiliaryBase *aux_data, bool my_cmd)
{
	SCOPE_INFO_FMT([=], "DoCommandP: tile: %X (%d x %d), p1: 0x%X, p2: 0x%X, p3: 0x" OTTD_PRINTFHEX64 ", company: %s, cmd: 0x%X (%s), my_cmd: %d%s",
			tile, TileX(tile), TileY(tile), p1, p2, p3, scope_dumper().CompanyInfo(_current_company), cmd, GetCommandName(cmd), my_cmd, cmd_text_info_dumper().CommandTextInfo(text, aux_data));

	/* Cost estimation is generally only done when the
	 * local user presses shift while doing something.
	 * However, in case of incoming network commands,
	 * map generation or the pause button we do want
	 * to execute. */
	bool estimate_only = _shift_pressed && IsLocalCompany() &&
			!_generating_world &&
			!(cmd & CMD_NETWORK_COMMAND) &&
			!(cmd & CMD_NO_SHIFT_ESTIMATE) &&
			!(GetCommandFlags(cmd) & CMD_NO_EST);

	/* We're only sending the command, so don't do
	 * fancy things for 'success'. */
	bool only_sending = _networking && !(cmd & CMD_NETWORK_COMMAND);

	/* Where to show the message? */
	TileIndex msg_tile = ((GetCommandFlags(cmd) & CMD_P1_TILE) && IsValidTile(p1)) ? p1 : tile;
	int x = TileX(msg_tile) * TILE_SIZE;
	int y = TileY(msg_tile) * TILE_SIZE;

	if (_pause_mode != PM_UNPAUSED && !IsCommandAllowedWhilePaused(cmd) && !estimate_only) {
		ShowErrorMessage(GB(cmd, 16, 16), STR_ERROR_NOT_ALLOWED_WHILE_PAUSED, WL_INFO, x, y);
		return false;
	}

	/* Only set p2 when the command does not come from the network. */
	if (!(cmd & CMD_NETWORK_COMMAND) && GetCommandFlags(cmd) & CMD_CLIENT_ID && p2 == 0) p2 = CLIENT_ID_SERVER;

	GameRandomSeedChecker random_state;
	uint order_backup_update_counter = OrderBackup::GetUpdateCounter();

	CommandCost res = DoCommandPInternal(tile, p1, p2, p3, cmd, callback, text, my_cmd, estimate_only, aux_data);

	CommandLogEntryFlag log_flags;
	log_flags = CLEF_NONE;
	if (!StrEmpty(text)) log_flags |= CLEF_TEXT;
	if (estimate_only) log_flags |= CLEF_ESTIMATE_ONLY;
	if (only_sending) log_flags |= CLEF_ONLY_SENDING;
	if (my_cmd) log_flags |= CLEF_MY_CMD;
	if (aux_data != nullptr) log_flags |= CLEF_AUX_DATA;
	if (!random_state.Check()) log_flags |= CLEF_RANDOM;
	if (order_backup_update_counter != OrderBackup::GetUpdateCounter()) log_flags |= CLEF_ORDER_BACKUP;
	AppendCommandLogEntry(res, tile, p1, p2, p3, cmd, log_flags, text);

	if (unlikely(HasChickenBit(DCBF_DESYNC_CHECK_POST_COMMAND)) && !(GetCommandFlags(cmd) & CMD_LOG_AUX)) {
		CheckCachesFlags flags = CHECK_CACHE_ALL | CHECK_CACHE_EMIT_LOG;
		if (HasChickenBit(DCBF_DESYNC_CHECK_NO_GENERAL)) flags &= ~CHECK_CACHE_GENERAL;
		CheckCaches(true, nullptr, flags);
	}

	if (res.Failed()) {
		/* Only show the error when it's for us. */
		StringID error_part1 = GB(cmd, 16, 16);
		if (estimate_only || (IsLocalCompany() && error_part1 != 0 && my_cmd)) {
			ShowErrorMessage(error_part1, res.GetErrorMessage(), WL_INFO, x, y, res.GetTextRefStackGRF(), res.GetTextRefStackSize(), res.GetTextRefStack(), res.GetExtraErrorMessage());
		}
	} else if (estimate_only) {
		ShowEstimatedCostOrIncome(res.GetCost(), x, y);
	} else if (!only_sending && tile != 0 && IsLocalCompany() && _game_mode != GM_EDITOR && HasBit(_extra_display_opt, XDO_SHOW_MONEY_TEXT_EFFECTS)) {
		/* Only show the cost animation when we did actually
		 * execute the command, i.e. we're not sending it to
		 * the server, when it has cost the local company
		 * something. Furthermore in the editor there is no
		 * concept of cost, so don't show it there either. */
		ShowCostOrIncomeAnimation(x, y, GetSlopePixelZ(x, y), res.GetCost());
	}

	if (!estimate_only && !only_sending && callback != nullptr) {
		callback(res, tile, p1, p2, p3, cmd);
	}

	return res.Succeeded();
}

CommandCost DoCommandPScript(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd, CommandCallback *callback, const char *text, bool my_cmd, bool estimate_only, bool asynchronous, const CommandAuxiliaryBase *aux_data)
{
	GameRandomSeedChecker random_state;
	uint order_backup_update_counter = OrderBackup::GetUpdateCounter();

	CommandCost res = DoCommandPInternal(tile, p1, p2, p3, cmd, callback, text, my_cmd, estimate_only, aux_data);

	CommandLogEntryFlag log_flags;
	log_flags = CLEF_SCRIPT;
	if (asynchronous) log_flags |= CLEF_SCRIPT_ASYNC;
	if (!StrEmpty(text)) log_flags |= CLEF_TEXT;
	if (estimate_only) log_flags |= CLEF_ESTIMATE_ONLY;
	if (_networking && !(cmd & CMD_NETWORK_COMMAND)) log_flags |= CLEF_ONLY_SENDING;
	if (my_cmd) log_flags |= CLEF_MY_CMD;
	if (aux_data != nullptr) log_flags |= CLEF_AUX_DATA;
	if (!random_state.Check()) log_flags |= CLEF_RANDOM;
	if (order_backup_update_counter != OrderBackup::GetUpdateCounter()) log_flags |= CLEF_ORDER_BACKUP;
	AppendCommandLogEntry(res, tile, p1, p2, p3, cmd, log_flags, text);

	if (unlikely(HasChickenBit(DCBF_DESYNC_CHECK_POST_COMMAND)) && !(GetCommandFlags(cmd) & CMD_LOG_AUX)) {
		CheckCachesFlags flags = CHECK_CACHE_ALL | CHECK_CACHE_EMIT_LOG;
		if (HasChickenBit(DCBF_DESYNC_CHECK_NO_GENERAL)) flags &= ~CHECK_CACHE_GENERAL;
		CheckCaches(true, nullptr, flags);
	}

	return res;
}

void ExecuteCommandQueue()
{
	while (!_command_queue.empty()) {
		Backup<CompanyID> cur_company(_current_company, FILE_LINE);
		cur_company.Change(_command_queue.front().company);
		DoCommandP(&_command_queue.front().cmd);
		cur_company.Restore();
		_command_queue.pop_front();
	}
}

void ClearCommandQueue()
{
	_command_queue.clear();
}

void EnqueueDoCommandP(CommandContainer cmd)
{
	if (_docommand_recursive == 0) {
		DoCommandP(&cmd);
	} else {
		CommandQueueItem &item = _command_queue.emplace_back();
		item.cmd = std::move(cmd);
		item.company = _current_company;
	}
}


/**
 * Helper to deduplicate the code for returning.
 * @param cmd   the command cost to return.
 */
#define return_dcpi(cmd) { _docommand_recursive = 0; return cmd; }

/**
 * Helper function for the toplevel network safe docommand function for the current company.
 *
 * @param tile The tile to perform a command on (see #CommandProc)
 * @param p1 Additional data for the command (see #CommandProc)
 * @param p2 Additional data for the command (see #CommandProc)
 * @param p3 Additional data for the command (see #CommandProc)
 * @param cmd The command to execute (a CMD_* value)
 * @param callback A callback function to call after the command is finished
 * @param text The text to pass
 * @param my_cmd indicator if the command is from a company or server (to display error messages for a user)
 * @param estimate_only whether to give only the estimate or also execute the command
 * @return the command cost of this function.
 */
CommandCost DoCommandPInternal(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd, CommandCallback *callback, const char *text, bool my_cmd, bool estimate_only, const CommandAuxiliaryBase *aux_data)
{
	/* Prevent recursion; it gives a mess over the network */
	assert(_docommand_recursive == 0);
	_docommand_recursive = 1;

	/* Reset the state. */
	_additional_cash_required = 0;

	/* Get pointer to command handler */
	byte cmd_id = cmd & CMD_ID_MASK;
	assert(cmd_id < lengthof(_command_proc_table));

	const Command &command = _command_proc_table[cmd_id];
	/* Shouldn't happen, but you never know when someone adds
	 * NULLs to the _command_proc_table. */
	assert(command.proc != nullptr);

	/* Command flags are used internally */
	CommandFlags cmd_flags = GetCommandFlags(cmd);
	/* Flags get send to the DoCommand */
	DoCommandFlag flags = CommandFlagsToDCFlags(cmd_flags);

	/* Make sure p2 is properly set to a ClientID. */
	assert(!(cmd_flags & CMD_CLIENT_ID) || p2 != 0);

	/* Do not even think about executing out-of-bounds tile-commands */
	if (tile != 0 && (tile >= MapSize() || (!IsValidTile(tile) && (cmd_flags & CMD_ALL_TILES) == 0))) return_dcpi(CMD_ERROR);

	/* Always execute server and spectator commands as spectator */
	bool exec_as_spectator = (cmd_flags & (CMD_SPECTATOR | CMD_SERVER)) != 0;

	/* If the company isn't valid it may only do server command or start a new company!
	 * The server will ditch any server commands a client sends to it, so effectively
	 * this guards the server from executing functions for an invalid company. */
	if (_game_mode == GM_NORMAL && !exec_as_spectator && !Company::IsValidID(_current_company) && !(_current_company == OWNER_DEITY && (cmd_flags & CMD_DEITY) != 0)) {
		return_dcpi(CMD_ERROR);
	}

	Backup<CompanyID> cur_company(_current_company, FILE_LINE);
	if (exec_as_spectator) cur_company.Change(COMPANY_SPECTATOR);

	bool test_and_exec_can_differ = ((cmd_flags & CMD_NO_TEST) != 0) || HasChickenBit(DCBF_CMD_NO_TEST_ALL);

	GameRandomSeedChecker random_state;

	/* Test the command. */
	_cleared_object_areas.clear();
	SetTownRatingTestMode(true);
	BasePersistentStorageArray::SwitchMode(PSM_ENTER_TESTMODE);
	CommandCost res = command.Execute(tile, flags, p1, p2, p3, text, aux_data);
	BasePersistentStorageArray::SwitchMode(PSM_LEAVE_TESTMODE);
	SetTownRatingTestMode(false);

	if (!random_state.Check()) {
		std::string msg = stdstr_fmt("Random seed changed in test command: company: %02x; tile: %06x (%u x %u); p1: %08x; p2: %08x; p3: " OTTD_PRINTFHEX64PAD "; cmd: %08x; \"%s\"%s (%s)",
				(int)_current_company, tile, TileX(tile), TileY(tile), p1, p2, p3, cmd & ~CMD_NETWORK_COMMAND, text, aux_data != nullptr ? ", aux data present" : "", GetCommandName(cmd));
		DEBUG(desync, 0, "msg: %s; %s", debug_date_dumper().HexDate(), msg.c_str());
		LogDesyncMsg(std::move(msg));
	}

	/* Make sure we're not messing things up here. */
	assert(exec_as_spectator ? _current_company == COMPANY_SPECTATOR : cur_company.Verify());

	auto log_desync_cmd = [&](const char *prefix) {
		if (_debug_desync_level >= 1) {
			std::string aux_str;
			if (aux_data != nullptr) {
				std::vector<byte> buffer;
				CommandSerialisationBuffer serialiser(buffer, SHRT_MAX);
				aux_data->Serialise(serialiser);
				aux_str = FormatArrayAsHex(buffer);
			}
			std::string text_buf;
			if (text != nullptr) {
				nlohmann::json j_string = text;
				text_buf = j_string.dump(-1, ' ', true);
			} else {
				text_buf = "\"\"";
			}

			/* Use stdstr_fmt and debug_print to avoid truncation limits of DEBUG, text/aux_data may be very large */
			std::string dbg_info = stdstr_fmt("%s: %s; company: %02x; tile: %06x (%u x %u); p1: %08x; p2: %08x; p3: " OTTD_PRINTFHEX64PAD "; cmd: %08x; %s <%s> (%s)",
					prefix, debug_date_dumper().HexDate(), (int)_current_company, tile, TileX(tile), TileY(tile), p1, p2, p3,
					cmd & ~CMD_NETWORK_COMMAND, text_buf.c_str(), aux_str.c_str(), GetCommandName(cmd));
			debug_print("desync", 1, dbg_info.c_str());
		}
	};

	/* If the command fails, we're doing an estimate
	 * or the player does not have enough money
	 * (unless it's a command where the test and
	 * execution phase might return different costs)
	 * we bail out here. */
	if (res.Failed() || estimate_only ||
			(!test_and_exec_can_differ && !CheckCompanyHasMoney(res))) {
		if (!_networking || _generating_world || (cmd & CMD_NETWORK_COMMAND) != 0) {
			/* Log the failed command as well. Just to be able to be find
			 * causes of desyncs due to bad command test implementations. */
			log_desync_cmd("cmdf");
		}
		cur_company.Restore();
		return_dcpi(res);
	}

	/*
	 * If we are in network, and the command is not from the network
	 * send it to the command-queue and abort execution
	 */
	if (_networking && !_generating_world && !(cmd & CMD_NETWORK_COMMAND)) {
		NetworkSendCommand(tile, p1, p2, p3, cmd & ~CMD_FLAGS_MASK, callback, text, _current_company, aux_data);
		cur_company.Restore();

		/* Don't return anything special here; no error, no costs.
		 * This way it's not handled by DoCommand and only the
		 * actual execution of the command causes messages. Also
		 * reset the storages as we've not executed the command. */
		return_dcpi(CommandCost());
	}
	log_desync_cmd("cmd");

	/* Actually try and execute the command. If no cost-type is given
	 * use the construction one */
	_cleared_object_areas.clear();
	BasePersistentStorageArray::SwitchMode(PSM_ENTER_COMMAND);
	CommandCost res2 = command.Execute(tile, flags | DC_EXEC, p1, p2, p3, text, aux_data);
	BasePersistentStorageArray::SwitchMode(PSM_LEAVE_COMMAND);

	if (cmd_id == CMD_COMPANY_CTRL) {
		cur_company.Trash();
		/* We are a new company                  -> Switch to new local company.
		 * We were closed down                   -> Switch to spectator
		 * Some other company opened/closed down -> The outside function will switch back */
		_current_company = _local_company;
	} else {
		/* Make sure nothing bad happened, like changing the current company. */
		assert(exec_as_spectator ? _current_company == COMPANY_SPECTATOR : cur_company.Verify());
		cur_company.Restore();
	}

	/* If the test and execution can differ we have to check the
	 * return of the command. Otherwise we can check whether the
	 * test and execution have yielded the same result,
	 * i.e. cost and error state are the same. */
	if (!test_and_exec_can_differ) {
		assert_msg(res.GetCost() == res2.GetCost() && res.Failed() == res2.Failed(),
				"Command: cmd: 0x%X (%s), Test: %s, Exec: %s", cmd, GetCommandName(cmd),
				res.SummaryMessage(GB(cmd, 16, 16)).c_str(), res2.SummaryMessage(GB(cmd, 16, 16)).c_str()); // sanity check
	} else if (res2.Failed()) {
		return_dcpi(res2);
	}

	/* If we're needing more money and we haven't done
	 * anything yet, ask for the money! */
	if (_additional_cash_required != 0 && res2.GetCost() == 0) {
		/* It could happen we removed rail, thus gained money, and deleted something else.
		 * So make sure the signal buffer is empty even in this case */
		UpdateSignalsInBuffer();
		if (_extra_aspects > 0) FlushDeferredAspectUpdates();
		SetDParam(0, _additional_cash_required);
		return_dcpi(CommandCost(STR_ERROR_NOT_ENOUGH_CASH_REQUIRES_CURRENCY));
	}

	/* update last build coordinate of company. */
	if (tile != 0) {
		Company *c = Company::GetIfValid(_current_company);
		if (c != nullptr) c->last_build_coordinate = tile;
	}

	SubtractMoneyFromCompany(res2);
	if (_networking) UpdateStateChecksum(res2.GetCost());

	/* update signals if needed */
	UpdateSignalsInBuffer();
	if (_extra_aspects > 0) FlushDeferredAspectUpdates();

	/* Record if there was a command issues during pause; ignore pause/other setting related changes. */
	if (_pause_mode != PM_UNPAUSED && command.type != CMDT_SERVER_SETTING) _pause_mode |= PM_COMMAND_DURING_PAUSE;

	return_dcpi(res2);
}
#undef return_dcpi

CommandCost::CommandCost(const CommandCost &other)
{
	*this = other;
}

CommandCost &CommandCost::operator=(const CommandCost &other)
{
	this->cost = other.cost;
	this->expense_type = other.expense_type;
	this->flags = other.flags;
	this->message = other.message;
	this->inl = other.inl;
	if (other.aux_data) {
		this->aux_data.reset(new CommandCostAuxiliaryData(*other.aux_data));
	}
	return *this;
}

/**
 * Adds the cost of the given command return value to this cost.
 * Also takes a possible error message when it is set.
 * @param ret The command to add the cost of.
 */
void CommandCost::AddCost(const CommandCost &ret)
{
	this->AddCost(ret.cost);
	if (this->Succeeded() && !ret.Succeeded()) {
		this->message = ret.message;
		this->flags &= ~CCIF_SUCCESS;
	}
}

/**
 * Activate usage of the NewGRF #TextRefStack for the error message.
 * @param grffile NewGRF that provides the #TextRefStack
 * @param num_registers number of entries to copy from the temporary NewGRF registers
 */
void CommandCost::UseTextRefStack(const GRFFile *grffile, uint num_registers)
{
	extern TemporaryStorageArray<int32_t, 0x110> _temp_store;

	if (!this->aux_data) {
		this->AllocAuxData();
	}

	assert(num_registers < lengthof(this->aux_data->textref_stack));
	this->aux_data->textref_stack_grffile = grffile;
	this->aux_data->textref_stack_size = num_registers;
	for (uint i = 0; i < num_registers; i++) {
		this->aux_data->textref_stack[i] = _temp_store.GetValue(0x100 + i);
	}
}

std::string CommandCost::SummaryMessage(StringID cmd_msg) const
{
	if (this->Succeeded()) {
		return stdstr_fmt("Success: cost: " OTTD_PRINTF64, (int64_t) this->GetCost());
	} else {
		const uint textref_stack_size = this->GetTextRefStackSize();
		if (textref_stack_size > 0) StartTextRefStackUsage(this->GetTextRefStackGRF(), textref_stack_size, this->GetTextRefStack());

		std::string buf = stdstr_fmt("Failed: cost: " OTTD_PRINTF64, (int64_t) this->GetCost());
		if (cmd_msg != 0) {
			buf += ' ';
			GetString(StringBuilder(buf), cmd_msg);
		}
		if (this->message != INVALID_STRING_ID) {
			buf += ' ';
			GetString(StringBuilder(buf), this->message);
		}

		if (textref_stack_size > 0) StopTextRefStackUsage();

		return buf;
	}
}

void CommandCost::AllocAuxData()
{
	this->aux_data.reset(new CommandCostAuxiliaryData());
	if (this->flags & CCIF_INLINE_EXTRA_MSG) {
		this->aux_data->extra_message = this->inl.extra_message;
		this->flags &= ~CCIF_INLINE_EXTRA_MSG;
	} else if (this->flags & CCIF_INLINE_TILE) {
		this->aux_data->tile = this->inl.tile;
		this->flags &= ~CCIF_INLINE_TILE;
	} else if (this->flags & CCIF_INLINE_RESULT) {
		this->aux_data->result = this->inl.result;
		this->flags &= ~CCIF_INLINE_RESULT;
	}
}

bool CommandCost::AddInlineData(CommandCostIntlFlags inline_flag)
{
	if (this->aux_data) return true;
	if (this->flags & inline_flag) {
		return false;
	}
	if (this->flags & ~CCIF_SUCCESS) {
		this->AllocAuxData();
		return true;
	}
	this->flags |= inline_flag;
	return false;
}

void CommandCost::SetTile(TileIndex tile)
{
	if (tile == this->GetTile()) return;

	if (this->AddInlineData(CCIF_INLINE_TILE)) {
		this->aux_data->tile = tile;
	} else {
		this->inl.tile = tile;
	}
}

void CommandCost::SetResultData(uint32_t result)
{
	this->flags |= CCIF_VALID_RESULT;

	if (result == this->GetResultData()) return;

	if (this->AddInlineData(CCIF_INLINE_RESULT)) {
		this->aux_data->result = result;
	} else {
		this->inl.result = result;
	}
}
