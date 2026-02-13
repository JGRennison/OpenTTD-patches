/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file town_cmd.h Command definitions related to towns. */

#ifndef TOWN_CMD_H
#define TOWN_CMD_H

#include "command_type.h"
#include "company_type.h"
#include "town.h"
#include "town_type.h"

enum TownAcceptanceEffect : uint8_t;
enum TownSettingOverrideFlags : uint8_t;
using HouseID = uint16_t;

struct HouseIDCmdVector {
	static constexpr bool command_payload_as_ref = true;
	static constexpr size_t MAX_HOUSE_IDS = 1024;

	std::vector<HouseID> ids;

	template <typename T>
	void Serialise(T &&buffer) const
	{
		buffer.Send_generic_integer(this->ids.size());
		for (HouseID id : this->ids) {
			buffer.Send_generic_integer(id);
		}
	}

	template <typename T>
	bool Deserialise(T &buffer, StringValidationSettings default_string_validation)
	{
		size_t size{};
		buffer.Recv_generic_integer(size);
		if (size > MAX_HOUSE_IDS) return false;
		this->ids.resize(size);
		for (HouseID &id : this->ids) {
			buffer.Recv_generic_integer(id);
		}
		return true;
	}

	void fmt_format_value(struct format_target &) const;
};

DEF_CMD_TUPLE   (CMD_FOUND_TOWN,                      CmdFoundTown,                    CMD_DEITY | CMD_NO_TEST, CommandType::LandscapeConstruction, CmdDataT<TownSize, bool, TownLayout, bool, uint32_t, std::string>) // founding random town can fail only in exec run
DEF_CMD_TUPLE_NT(CMD_RENAME_TOWN,                     CmdRenameTown,                    CMD_DEITY | CMD_SERVER, CommandType::OtherManagement,       CmdDataT<TownID, std::string>)
DEF_CMD_TUPLE_NT(CMD_RENAME_TOWN_NON_ADMIN,           CmdRenameTownNonAdmin,                                {}, CommandType::OtherManagement,       CmdDataT<TownID, std::string>)
DEF_CMD_TUPLE_LT(CMD_DO_TOWN_ACTION,                  CmdDoTownAction,                                      {}, CommandType::LandscapeConstruction, CmdDataT<TownID, TownAction>)
DEF_CMD_TUPLE_NT(CMD_TOWN_CARGO_GOAL,                 CmdTownCargoGoal,                CMD_LOG_AUX | CMD_DEITY, CommandType::OtherManagement,       CmdDataT<TownID, TownAcceptanceEffect, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_TOWN_GROWTH_RATE,                CmdTownGrowthRate,               CMD_LOG_AUX | CMD_DEITY, CommandType::OtherManagement,       CmdDataT<TownID, uint16_t>)
DEF_CMD_TUPLE_NT(CMD_TOWN_RATING,                     CmdTownRating,                   CMD_LOG_AUX | CMD_DEITY, CommandType::OtherManagement,       CmdDataT<TownID, CompanyID, int16_t>)
DEF_CMD_TUPLE_NT(CMD_TOWN_SET_TEXT,                   CmdTownSetText,   CMD_LOG_AUX | CMD_STR_CTRL | CMD_DEITY, CommandType::OtherManagement,       CmdDataT<TownID, EncodedString>)
DEF_CMD_TUPLE_NT(CMD_EXPAND_TOWN,                     CmdExpandTown,                                 CMD_DEITY, CommandType::LandscapeConstruction, CmdDataT<TownID, uint32_t, TownExpandModes>)
DEF_CMD_TUPLE_NT(CMD_DELETE_TOWN,                     CmdDeleteTown,                               CMD_OFFLINE, CommandType::LandscapeConstruction, CmdDataT<TownID>)
DEF_CMD_TUPLE   (CMD_PLACE_HOUSE,                     CmdPlaceHouse,                                 CMD_DEITY, CommandType::OtherManagement,       CmdDataT<HouseID, bool, TownID, bool>)
DEF_CMD_TUPLE   (CMD_PLACE_HOUSE_AREA,                CmdPlaceHouseArea,                             CMD_DEITY, CommandType::OtherManagement,       CmdDataT<TileIndex, HouseIDCmdVector, bool, TownID, bool, bool>)
DEF_CMD_TUPLE_NT(CMD_TOWN_SETTING_OVERRIDE,           CmdOverrideTownSetting,           CMD_DEITY | CMD_SERVER, CommandType::OtherManagement,       CmdDataT<TownID, TownSettingOverrideFlags, bool, uint8_t>)
DEF_CMD_TUPLE_NT(CMD_TOWN_SETTING_OVERRIDE_NON_ADMIN, CmdOverrideTownSettingNonAdmin,                       {}, CommandType::OtherManagement,       CmdDataT<TownID, TownSettingOverrideFlags, bool, uint8_t>)

#endif /* TOWN_CMD_H */
