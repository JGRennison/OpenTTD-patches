/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file story_cmd.h Command definitions related to stories. */

#ifndef STORY_CMD_H
#define STORY_CMD_H

#include "command_type.h"
#include "company_type.h"
#include "story_type.h"
#include "vehicle_type.h"

DEF_CMD_TUPLE_NT(Commands::CreateStoryPage,         CmdCreateStoryPage,         CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CommandType::OtherManagement, CmdDataT<CompanyID, EncodedString>)
DEF_CMD_TUPLE_NT(Commands::CreateStoryPageElement,  CmdCreateStoryPageElement,  CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CommandType::OtherManagement, CmdDataT<TileIndex, StoryPageID, StoryPageElementType, uint32_t, EncodedString>)
DEF_CMD_TUPLE_NT(Commands::UpdateStoryPageElement,  CmdUpdateStoryPageElement,  CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CommandType::OtherManagement, CmdDataT<TileIndex, StoryPageElementID, uint32_t, EncodedString>)
DEF_CMD_TUPLE_NT(Commands::SetStoryPageTitle,       CmdSetStoryPageTitle,       CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CommandType::OtherManagement, CmdDataT<StoryPageID, EncodedString>)
DEF_CMD_TUPLE_NT(Commands::SetStoryPageDate,        CmdSetStoryPageDate,                       CMD_DEITY | CMD_LOG_AUX, CommandType::OtherManagement, CmdDataT<StoryPageID, CalTime::Date>)
DEF_CMD_TUPLE_NT(Commands::ShowStoryPage,           CmdShowStoryPage,                          CMD_DEITY | CMD_LOG_AUX, CommandType::OtherManagement, CmdDataT<StoryPageID>)
DEF_CMD_TUPLE_NT(Commands::RemoveStoryPage,         CmdRemoveStoryPage,                        CMD_DEITY | CMD_LOG_AUX, CommandType::OtherManagement, CmdDataT<StoryPageID>)
DEF_CMD_TUPLE_NT(Commands::RemoveStoryPageElement,  CmdRemoveStoryPageElement,                 CMD_DEITY | CMD_LOG_AUX, CommandType::OtherManagement, CmdDataT<StoryPageElementID>)
DEF_CMD_TUPLE   (Commands::StoryPageButton,         CmdStoryPageButton,                        CMD_DEITY | CMD_LOG_AUX, CommandType::OtherManagement, CmdDataT<StoryPageElementID, VehicleID>)

#endif /* STORY_CMD_H */
