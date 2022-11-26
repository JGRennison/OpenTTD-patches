/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_league.cpp Implementation of ScriptLeagueTable. */

#include "../../stdafx.h"

#include "script_league.hpp"

#include "../script_instance.hpp"
#include "script_error.hpp"
#include "../../league_base.h"
#include "../../string_func.h"

#include "../../safeguards.h"


/* static */ bool ScriptLeagueTable::IsValidLeagueTable(LeagueTableID table_id)
{
	return ::LeagueTable::IsValidID(table_id);
}

/* static */ ScriptLeagueTable::LeagueTableID ScriptLeagueTable::New(Text *title, Text *header, Text *footer)
{
	CCountedPtr<Text> title_counter(title);
	CCountedPtr<Text> header_counter(header);
	CCountedPtr<Text> footer_counter(footer);

	EnforcePrecondition(LEAGUE_TABLE_INVALID, ScriptObject::GetCompany() == OWNER_DEITY);
	EnforcePrecondition(LEAGUE_TABLE_INVALID, title != nullptr);
	const char *encoded_title = title->GetEncodedText();
	EnforcePreconditionEncodedText(LEAGUE_TABLE_INVALID, encoded_title);

	std::string cmd_text = encoded_title;
	cmd_text.push_back(0x1F);
	if (header != nullptr) cmd_text += header->GetEncodedText();
	cmd_text.push_back(0x1F);
	if (footer != nullptr) cmd_text += footer->GetEncodedText();

	if (!ScriptObject::DoCommand(0, 0, 0, CMD_CREATE_LEAGUE_TABLE, cmd_text.c_str(), &ScriptInstance::DoCommandReturnLeagueTableID)) return LEAGUE_TABLE_INVALID;

	/* In case of test-mode, we return LeagueTableID 0 */
	return (ScriptLeagueTable::LeagueTableID)0;
}

/* static */ bool ScriptLeagueTable::IsValidLeagueTableElement(LeagueTableElementID element_id)
{
	return ::LeagueTableElement::IsValidID(element_id);
}

/* static */ ScriptLeagueTable::LeagueTableElementID ScriptLeagueTable::NewElement(ScriptLeagueTable::LeagueTableID table, int64 rating, ScriptCompany::CompanyID company, Text *text, Text *score, LinkType link_type, uint32 link_target)
{
	CCountedPtr<Text> text_counter(text);
	CCountedPtr<Text> score_counter(score);

	EnforcePrecondition(LEAGUE_TABLE_ELEMENT_INVALID, ScriptObject::GetCompany() == OWNER_DEITY);

	EnforcePrecondition(LEAGUE_TABLE_ELEMENT_INVALID, IsValidLeagueTable(table));

	EnforcePrecondition(LEAGUE_TABLE_ELEMENT_INVALID, company == ScriptCompany::COMPANY_INVALID || ScriptCompany::ResolveCompanyID(company) != ScriptCompany::COMPANY_INVALID);
	CompanyID c = (::CompanyID)company;
	if (company == ScriptCompany::COMPANY_INVALID) c = INVALID_COMPANY;

	EnforcePrecondition(LEAGUE_TABLE_ELEMENT_INVALID, text != nullptr);
	const char *encoded_text_ptr = text->GetEncodedText();
	EnforcePreconditionEncodedText(LEAGUE_TABLE_ELEMENT_INVALID, encoded_text_ptr);
	std::string encoded_text = encoded_text_ptr;  // save into string so GetEncodedText can reuse the internal buffer

	EnforcePrecondition(LEAGUE_TABLE_ELEMENT_INVALID, score != nullptr);
	const char *encoded_score = score->GetEncodedText();
	EnforcePreconditionEncodedText(LEAGUE_TABLE_ELEMENT_INVALID, encoded_score);

	EnforcePrecondition(LEAGUE_TABLE_ELEMENT_INVALID, IsValidLink(Link((::LinkType)link_type, link_target)));

	std::string cmd_text = std::move(encoded_text);
	cmd_text.push_back(0x1F);
	cmd_text += encoded_score;
	if (!ScriptObject::DoCommandEx(0, table | (c << 8) | (link_type << 16), link_target, rating, CMD_CREATE_LEAGUE_TABLE_ELEMENT, cmd_text.c_str(), 0, &ScriptInstance::DoCommandReturnLeagueTableElementID)) return LEAGUE_TABLE_ELEMENT_INVALID;

	/* In case of test-mode, we return LeagueTableElementID 0 */
	return (ScriptLeagueTable::LeagueTableElementID)0;
}

/* static */ bool ScriptLeagueTable::UpdateElementData(LeagueTableElementID element, ScriptCompany::CompanyID company, Text *text, LinkType link_type, LinkTargetID link_target)
{
	CCountedPtr<Text> text_counter(text);

	EnforcePrecondition(false, ScriptObject::GetCompany() == OWNER_DEITY);
	EnforcePrecondition(false, IsValidLeagueTableElement(element));

	EnforcePrecondition(false, company == ScriptCompany::COMPANY_INVALID || ScriptCompany::ResolveCompanyID(company) != ScriptCompany::COMPANY_INVALID);
	CompanyID c = (::CompanyID)company;
	if (company == ScriptCompany::COMPANY_INVALID) c = INVALID_COMPANY;

	EnforcePrecondition(false, text != nullptr);
	const char *encoded_text = text->GetEncodedText();
	EnforcePreconditionEncodedText(false, encoded_text);

	EnforcePrecondition(false, IsValidLink(Link((::LinkType)link_type, link_target)));

	return ScriptObject::DoCommand(0, element | (c << 16) | (link_type << 24), link_target, CMD_UPDATE_LEAGUE_TABLE_ELEMENT_DATA, encoded_text);
}

/* static */ bool ScriptLeagueTable::UpdateElementScore(LeagueTableElementID element, int64 rating, Text *score)
{
	CCountedPtr<Text> score_counter(score);

	EnforcePrecondition(false, ScriptObject::GetCompany() == OWNER_DEITY);
	EnforcePrecondition(false, IsValidLeagueTableElement(element));

	EnforcePrecondition(false, score != nullptr);
	const char *encoded_score = score->GetEncodedText();
	EnforcePreconditionEncodedText(false, encoded_score);

	return ScriptObject::DoCommandEx(0, element, 0, rating, CMD_UPDATE_LEAGUE_TABLE_ELEMENT_SCORE, encoded_score);
}

/* static */ bool ScriptLeagueTable::RemoveElement(LeagueTableElementID element)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() == OWNER_DEITY);
	EnforcePrecondition(false, IsValidLeagueTableElement(element));

	return ScriptObject::DoCommand(0, element, 0, CMD_REMOVE_LEAGUE_TABLE_ELEMENT);
}
