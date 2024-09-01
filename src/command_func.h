/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_func.h Functions related to commands. */

#ifndef COMMAND_FUNC_H
#define COMMAND_FUNC_H

#include "command_type.h"
#include "company_type.h"

/**
 * Define a default return value for a failed command.
 *
 * This variable contains a CommandCost object with is declared as "failed".
 * Other functions just need to return this error if there is an error,
 * which doesn't need to specific by a StringID.
 */
static const CommandCost CMD_ERROR = CommandCost(INVALID_STRING_ID);

/**
 * Returns from a function with a specific StringID as error.
 *
 * This macro is used to return from a function. The parameter contains the
 * StringID which will be returned.
 *
 * @param errcode The StringID to return
 */
#define return_cmd_error(errcode) return CommandCost(errcode);

CommandCost DoCommandEx(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, DoCommandFlag flags, uint32_t cmd, const char *text = nullptr, const CommandAuxiliaryBase *aux_data = nullptr);

inline CommandCost DoCommand(TileIndex tile, uint32_t p1, uint32_t p2, DoCommandFlag flags, uint32_t cmd, const char *text = nullptr)
{
	return DoCommandEx(tile, p1, p2, 0, flags, cmd, text, nullptr);
}

inline CommandCost DoCommandAux(TileIndex tile, const CommandAuxiliaryBase *aux_data, DoCommandFlag flags, uint32_t cmd)
{
	return DoCommandEx(tile, 0, 0, 0, flags, cmd, nullptr, aux_data);
}

inline CommandCost DoCommand(const CommandContainer *container, DoCommandFlag flags)
{
	return DoCommandEx(container->tile, container->p1, container->p2, container->p3, flags, container->cmd & CMD_ID_MASK, container->text.c_str(), container->aux_data.get());
}

bool DoCommandPEx(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd, CommandCallback *callback = nullptr, const char *text = nullptr, const CommandAuxiliaryBase *aux_data = nullptr, bool my_cmd = true);

inline bool DoCommandP(TileIndex tile, uint32_t p1, uint32_t p2, uint32_t cmd, CommandCallback *callback = nullptr, const char *text = nullptr, bool my_cmd = true)
{
	return DoCommandPEx(tile, p1, p2, 0, cmd, callback, text, nullptr, my_cmd);
}

inline bool DoCommandPAux(TileIndex tile, const CommandAuxiliaryBase *aux_data, uint32_t cmd, CommandCallback *callback = nullptr, bool my_cmd = true)
{
	return DoCommandPEx(tile, 0, 0, 0, cmd, callback, nullptr, aux_data, my_cmd);
}

inline bool DoCommandP(const CommandContainer *container, bool my_cmd = true)
{
	return DoCommandPEx(container->tile, container->p1, container->p2, container->p3, container->cmd, container->callback, container->text.c_str(), container->aux_data.get(), my_cmd);
}

CommandCost DoCommandPScript(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd, CommandCallback *callback, const char *text, bool my_cmd, bool estimate_only, bool asynchronous, const CommandAuxiliaryBase *aux_data);
CommandCost DoCommandPInternal(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd, CommandCallback *callback, const char *text, bool my_cmd, bool estimate_only, const CommandAuxiliaryBase *aux_data);

void NetworkSendCommand(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd, CommandCallback *callback, const char *text, CompanyID company, const CommandAuxiliaryBase *aux_data);

extern Money _additional_cash_required;

bool IsValidCommand(uint32_t cmd);
CommandFlags GetCommandFlags(uint32_t cmd);
const char *GetCommandName(uint32_t cmd);
bool IsCommandAllowedWhilePaused(uint32_t cmd);

/**
 * Extracts the DC flags needed for DoCommand from the flags returned by GetCommandFlags
 * @param cmd_flags Flags from GetCommandFlags
 * @return flags for DoCommand
 */
inline DoCommandFlag CommandFlagsToDCFlags(CommandFlags cmd_flags)
{
	DoCommandFlag flags = DC_NONE;
	if (cmd_flags & CMD_NO_WATER) flags |= DC_NO_WATER;
	if (cmd_flags & CMD_AUTO) flags |= DC_AUTO;
	if (cmd_flags & CMD_ALL_TILES) flags |= DC_ALL_TILES;
	return flags;
}

void ExecuteCommandQueue();
void ClearCommandQueue();
void EnqueueDoCommandP(CommandContainer cmd);

/*** All command callbacks that exist ***/

/* ai/ai_instance.cpp */
CommandCallback CcAI;

/* airport_gui.cpp */
CommandCallback CcBuildAirport;

/* bridge_gui.cpp */
CommandCallback CcBuildBridge;

/* dock_gui.cpp */
CommandCallback CcBuildDocks;
CommandCallback CcPlaySound_CONSTRUCTION_WATER;

/* depot_gui.cpp */
CommandCallback CcCloneVehicle;

/* game/game_instance.cpp */
CommandCallback CcGame;

/* group_gui.cpp */
CommandCallback CcCreateGroup;
CommandCallback CcAddVehicleNewGroup;

/* industry_gui.cpp */
CommandCallback CcBuildIndustry;

/* main_gui.cpp */
CommandCallback CcPlaySound_EXPLOSION;
CommandCallback CcPlaceSign;
CommandCallback CcTerraform;
CommandCallback CcGiveMoney;

/* plans_gui.cpp */
CommandCallback CcAddPlan;

/* rail_gui.cpp */
CommandCallback CcPlaySound_CONSTRUCTION_RAIL;
CommandCallback CcRailDepot;
CommandCallback CcStation;
CommandCallback CcBuildRailTunnel;

/* road_gui.cpp */
CommandCallback CcPlaySound_CONSTRUCTION_OTHER;
CommandCallback CcBuildRoadTunnel;
CommandCallback CcRoadDepot;
CommandCallback CcRoadStop;

/* train_gui.cpp */
CommandCallback CcBuildWagon;

/* town_gui.cpp */
CommandCallback CcFoundTown;
CommandCallback CcFoundRandomTown;

/* vehicle_gui.cpp */
CommandCallback CcBuildPrimaryVehicle;
CommandCallback CcStartStopVehicle;

/* tbtr_template_gui_create.cpp */
CommandCallback CcSetVirtualTrain;
CommandCallback CcVirtualTrainWagonsMoved;
CommandCallback CcDeleteVirtualTrain;

/* build_vehicle_gui.cpp */
CommandCallback CcAddVirtualEngine;
CommandCallback CcMoveNewVirtualEngine;

/* schdispatch_gui.cpp */
CommandCallback CcAddNewSchDispatchSchedule;
CommandCallback CcSwapSchDispatchSchedules;

#endif /* COMMAND_FUNC_H */
