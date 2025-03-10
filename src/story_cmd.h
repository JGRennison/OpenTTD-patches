/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file story_cmd.h Command definitions related to stories. */

#ifndef STORY_CMD_H
#define STORY_CMD_H

#include "command_type.h"
#include "company_type.h"
#include "story_type.h"
#include "vehicle_type.h"

DEF_CMD_TUPLE_NT(CMD_CREATE_STORY_PAGE,         CmdCreateStoryPage,         CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<CompanyID, std::string>)
DEF_CMD_TUPLE_NT(CMD_CREATE_STORY_PAGE_ELEMENT, CmdCreateStoryPageElement,  CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<TileIndex, StoryPageID, StoryPageElementType, uint32_t, std::string>)
DEF_CMD_TUPLE_NT(CMD_UPDATE_STORY_PAGE_ELEMENT, CmdUpdateStoryPageElement,  CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<TileIndex, StoryPageElementID, uint32_t, std::string>)
DEF_CMD_TUPLE_NT(CMD_SET_STORY_PAGE_TITLE,      CmdSetStoryPageTitle,       CMD_STR_CTRL | CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<StoryPageID, std::string>)
DEF_CMD_TUPLE_NT(CMD_SET_STORY_PAGE_DATE,       CmdSetStoryPageDate,                       CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<StoryPageID, CalTime::Date>)
DEF_CMD_TUPLE_NT(CMD_SHOW_STORY_PAGE,           CmdShowStoryPage,                          CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<StoryPageID>)
DEF_CMD_TUPLE_NT(CMD_REMOVE_STORY_PAGE,         CmdRemoveStoryPage,                        CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<StoryPageID>)
DEF_CMD_TUPLE_NT(CMD_REMOVE_STORY_PAGE_ELEMENT, CmdRemoveStoryPageElement,                 CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<StoryPageElementID>)
DEF_CMD_TUPLE   (CMD_STORY_PAGE_BUTTON,         CmdStoryPageButton,                        CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<StoryPageElementID, VehicleID>)

#endif /* STORY_CMD_H */
