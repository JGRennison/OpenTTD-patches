/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file goal_cmd.h Command definitions related to goals. */

#ifndef GOAL_CMD_H
#define GOAL_CMD_H

#include "command_type.h"
#include "company_type.h"
#include "goal_type.h"

DEF_CMD_TUPLE_NT(CMD_CREATE_GOAL,          CmdCreateGoal,              CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<CompanyID, GoalType, GoalTypeID, std::string>)
DEF_CMD_TUPLE_NT(CMD_REMOVE_GOAL,          CmdRemoveGoal,                             CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<GoalID>)
DEF_CMD_TUPLE_NT(CMD_SET_GOAL_DESTINATION, CmdSetGoalDestination,                     CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<GoalID, GoalType, GoalTypeID>)
DEF_CMD_TUPLE_NT(CMD_SET_GOAL_TEXT,        CmdSetGoalText,             CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<GoalID, std::string>)
DEF_CMD_TUPLE_NT(CMD_SET_GOAL_PROGRESS,    CmdSetGoalProgress,         CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<GoalID, std::string>)
DEF_CMD_TUPLE_NT(CMD_SET_GOAL_COMPLETED,   CmdSetGoalCompleted,        CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<GoalID, bool>)
DEF_CMD_TUPLE_NT(CMD_GOAL_QUESTION,        CmdGoalQuestion,            CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<uint16_t, uint32_t, bool, uint32_t, GoalQuestionType, std::string>)
DEF_CMD_TUPLE_NT(CMD_GOAL_QUESTION_ANSWER, CmdGoalQuestionAnswer,                     CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<uint16_t, uint8_t>)

#endif /* GOAL_CMD_H */
