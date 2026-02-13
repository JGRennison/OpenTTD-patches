/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file programmable_signals_cmd.h Programmable Pre-Signal Commands */

#ifndef PROGRAMMABLE_SIGNALS_CMD_H
#define PROGRAMMABLE_SIGNALS_CMD_H

#include "command_type.h"
#include "programmable_signals.h"

enum ProgPresigModifyCommandType : uint8_t {
	PPMCT_SIGNAL_STATE,                      ///< Change set signal state value
	PPMCT_CONDITION_CODE,                    ///< Change condition code
	PPMCT_COMPARATOR,                        ///< Change comparator (value from SignalComparator enum)
	PPMCT_VALUE,                             ///< Change value
	PPMCT_SLOT,                              ///< Change slot ID
	PPMCT_COUNTER,                           ///< Change counter ID
	PPMCT_SIGNAL_LOCATION,                   ///< Change signal state condition location
};

enum ProgPresigMgmtCommandType : uint8_t {
	PPMGMTCT_REMOVE,                         ///< Remove program
	PPMGMTCT_CLONE,                          ///< Clone program
};

DEF_CMD_TUPLE(CMD_PROGPRESIG_INSERT_INSTRUCTION, CmdProgPresigInsertInstruction, {}, CommandType::OtherManagement, CmdDataT<Track, uint32_t, SignalOpcode>)
DEF_CMD_TUPLE(CMD_PROGPRESIG_MODIFY_INSTRUCTION, CmdProgPresigModifyInstruction, {}, CommandType::OtherManagement, CmdDataT<Track, uint32_t, ProgPresigModifyCommandType, uint32_t, Trackdir>)
DEF_CMD_TUPLE(CMD_PROGPRESIG_REMOVE_INSTRUCTION, CmdProgPresigRemoveInstruction, {}, CommandType::OtherManagement, CmdDataT<Track, uint32_t>)
DEF_CMD_TUPLE(CMD_PROGPRESIG_PROGRAM_MGMT,       CmdProgPresigProgramMgmt,       {}, CommandType::OtherManagement, CmdDataT<Track, ProgPresigMgmtCommandType, TileIndex, Track>)

#endif /* PROGRAMMABLE_SIGNALS_CMD_H */
