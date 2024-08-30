/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file league_cmd.h Command definitions related to league tables. */

#ifndef LEAGUE_CMD_H
#define LEAGUE_CMD_H

#include "command_aux.h"

struct LeagueTableCmdData : public CommandAuxiliarySerialisable<LeagueTableCmdData> {
	std::string title;
	std::string header;
	std::string footer;

	virtual void Serialise(CommandSerialisationBuffer &buffer) const override
	{
		buffer.Send_string(this->title);
		buffer.Send_string(this->header);
		buffer.Send_string(this->footer);
	}

	CommandCost Deserialise(CommandDeserialisationBuffer &buffer)
	{
		buffer.Recv_string(this->title,  SVS_ALLOW_CONTROL_CODE | SVS_REPLACE_WITH_QUESTION_MARK);
		buffer.Recv_string(this->header, SVS_ALLOW_CONTROL_CODE | SVS_REPLACE_WITH_QUESTION_MARK);
		buffer.Recv_string(this->footer, SVS_ALLOW_CONTROL_CODE | SVS_REPLACE_WITH_QUESTION_MARK);
		return CommandCost();
	}
};

struct LeagueTableElementCmdData : public CommandAuxiliarySerialisable<LeagueTableElementCmdData> {
	LeagueTableID table;
	int64_t rating;
	CompanyID company;
	LinkType link_type;
	LinkTargetID link_target;
	std::string text_str;
	std::string score;

	virtual void Serialise(CommandSerialisationBuffer &buffer) const override
	{
		buffer.Send_uint8(this->table);
		buffer.Send_uint64(this->rating);
		buffer.Send_uint8(this->company);
		buffer.Send_uint8(this->link_type);
		buffer.Send_uint32(this->link_target);
		buffer.Send_string(this->text_str);
		buffer.Send_string(this->score);
	}

	CommandCost Deserialise(CommandDeserialisationBuffer &buffer)
	{
		this->table = buffer.Recv_uint8();
		this->rating = buffer.Recv_uint64();
		this->company = (CompanyID)buffer.Recv_uint8();
		this->link_type = (LinkType)buffer.Recv_uint8();
		this->link_target = (LinkTargetID)buffer.Recv_uint32();
		buffer.Recv_string(this->text_str, SVS_ALLOW_CONTROL_CODE | SVS_REPLACE_WITH_QUESTION_MARK);
		buffer.Recv_string(this->score,    SVS_ALLOW_CONTROL_CODE | SVS_REPLACE_WITH_QUESTION_MARK);
		return CommandCost();
	}
};

#endif /* LEAGUE_CMD_H */
