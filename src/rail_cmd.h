/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file rail_cmd.h Command definitions for rail. */

#ifndef RAIL_CMD_H
#define RAIL_CMD_H

#include "command_type.h"
#include "track_type.h"
#include "rail_type.h"
#include "signal_type.h"

enum class BuildRailTrackFlags : uint8_t {
	None                  = 0,         ///< No flag set.
	NoCustomBridgeHeads   = (1U << 0), ///< Disable custom bridge heads.
	AutoRemoveSignals     = (1U << 1), ///< Auto-remove signals.
	NoDualRailType        = (1U << 2), ///< Disable dual rail types.
};
DECLARE_ENUM_AS_BIT_SET(BuildRailTrackFlags)

enum class BuildSignalFlags : uint8_t {
	None                   = 0,         ///< No flag set.
	Convert                = (1U << 0), ///< Convert the present signal type and variant.
	CtrlPressed            = (1U << 1), ///< Override signal/semaphore, or pre/exit/combo signal or toggle variant (CTRL-toggle)
	SkipExisting           = (1U << 2), ///< Don't modify an existing signal but don't fail either. Otherewise always set new signal type.
	PermitBidiTunnelBridge = (1U << 3), ///< Permit creation of/conversion to bidirectionally signalled bridges/tunnels.
};
DECLARE_ENUM_AS_BIT_SET(BuildSignalFlags)

enum class RemoveSignalFlags : uint8_t {
	None                  = 0,         ///< No flag set.
	NoRemoveRestricted    = (1U << 0), ///< Do not remove restricted signals.
};
DECLARE_ENUM_AS_BIT_SET(RemoveSignalFlags)

enum class SignalDragFlags : uint8_t {
	None                  = 0,         ///< No flag set.
	Autofill              = (1U << 0), ///< Fill beyond selected stretch.
	SkipOverStations      = (1U << 1), ///< Skip over rail stations/waypoints, otherwise stop at rail stations/waypoints.
	MinimiseGaps          = (1U << 2), ///< True = minimise gaps between signals. False = keep fixed distance.
};
DECLARE_ENUM_AS_BIT_SET(SignalDragFlags)

struct BuildSingleSignalCmdData final : public AutoFmtTupleCmdData<BuildSingleSignalCmdData, TCDF_NONE,
		Track, SignalType, SignalVariant, uint8_t, uint8_t, BuildSignalFlags, SignalCycleGroups, uint8_t, uint8_t> {
	static inline constexpr const char fmt_str[] = "t: {}, st: {}, sv: {}, style: {}, sp: {}, bf: {:X}, cycle: ({}, {}), copy: {}";
};

struct BuildSignalTrackCmdData final : public AutoFmtTupleCmdData<BuildSignalTrackCmdData, TCDF_NONE,
		TileIndex, Track, SignalType, SignalVariant, uint8_t, bool, SignalDragFlags, uint8_t> {
	static inline constexpr const char fmt_str[] = "end: {}, t: {}, st: {}, sv: {}, style: {}, mode: {}, df: {:X}, sp: {}";
};

DEF_CMD_TUPLE(CMD_BUILD_RAILROAD_TRACK,  CmdBuildRailroadTrack,       CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, RailType, Track, BuildRailTrackFlags, bool>)
DEF_CMD_TUPLE(CMD_REMOVE_RAILROAD_TRACK, CmdRemoveRailroadTrack,                     CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, Track>)
DEF_CMD_TUPLE(CMD_BUILD_SINGLE_RAIL,     CmdBuildSingleRail,          CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<RailType, Track, BuildRailTrackFlags>)
DEF_CMD_TUPLE(CMD_REMOVE_SINGLE_RAIL,    CmdRemoveSingleRail,                        CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<Track>)
DEF_CMD_TUPLE(CMD_BUILD_TRAIN_DEPOT,     CmdBuildTrainDepot,          CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<RailType, DiagDirection>)
DEF_CMD_TUPLE(CMD_BUILD_SINGLE_SIGNAL,   CmdBuildSingleSignal,                       CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, BuildSingleSignalCmdData)
DEF_CMD_TUPLE(CMD_REMOVE_SINGLE_SIGNAL,  CmdRemoveSingleSignal,                      CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<Track, RemoveSignalFlags>)
DEF_CMD_TUPLE(CMD_CONVERT_RAIL,          CmdConvertRail,                                   {}, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, RailType, bool>)
DEF_CMD_TUPLE(CMD_CONVERT_RAIL_TRACK,    CmdConvertRailTrack,                              {}, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, Track, RailType>)
DEF_CMD_TUPLE(CMD_BUILD_SIGNAL_TRACK,    CmdBuildSignalTrack,                        CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, BuildSignalTrackCmdData)
DEF_CMD_TUPLE(CMD_REMOVE_SIGNAL_TRACK,   CmdRemoveSignalTrack,                       CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, Track, SignalDragFlags, RemoveSignalFlags>)

#endif /* RAIL_CMD_H */
