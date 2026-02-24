/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file company_cmd.h Command definitions related to companies. */

#ifndef COMPANY_CMD_H
#define COMPANY_CMD_H

#include "command_type.h"
#include "company_type.h"
#include "livery.h"

enum Colours : uint8_t;

struct CmdCompanyCtrlInnerData {
	CompanyCtrlAction cca;
	CompanyID company_id;
	CompanyRemoveReason reason;
	ClientID client_id;
	CompanyID to_merge_id;

	/* This must include all fields */
	using Self = CmdCompanyCtrlInnerData;
	static constexpr auto GetTupleFields() { return std::make_tuple(&Self::cca, &Self::company_id, &Self::reason, &Self::client_id, &Self::to_merge_id); }
};
struct CmdCompanyCtrlData final : public TupleRefCmdData<CmdCompanyCtrlData, CmdCompanyCtrlInnerData> {
	void FormatDebugSummary(struct format_target &) const;
};

DEF_CMD_TUPLE_NT (CMD_COMPANY_CTRL,             CmdCompanyCtrl,           CMD_SPECTATOR | CMD_CLIENT_ID | CMD_NO_EST, CommandType::ServerSetting,   CmdCompanyCtrlData)
DEF_CMD_TUPLE_NT (CMD_COMPANY_ALLOW_LIST_CTRL,  CmdCompanyAllowListCtrl,  CMD_NO_EST,                                 CommandType::OtherManagement, CmdDataT<CompanyAllowListCtrlAction, std::string>)
DEF_CMD_TUPLE_NT (CMD_GIVE_MONEY,               CmdGiveMoney,             {},                                         CommandType::MoneyManagement, CmdDataT<Money, CompanyID>)
DEF_CMD_TUPLE_NT (CMD_RENAME_COMPANY,           CmdRenameCompany,         {},                                         CommandType::CompanySetting,  CmdDataT<std::string>)
DEF_CMD_TUPLE_NT (CMD_RENAME_PRESIDENT,         CmdRenamePresident,       {},                                         CommandType::CompanySetting,  CmdDataT<std::string>)
DEF_CMD_TUPLE_NT (CMD_SET_COMPANY_MANAGER_FACE, CmdSetCompanyManagerFace, {},                                         CommandType::CompanySetting,  CmdDataT<uint, uint32_t>)
DEF_CMD_TUPLE_NT (CMD_SET_COMPANY_COLOUR,       CmdSetCompanyColour,      {},                                         CommandType::CompanySetting,  CmdDataT<LiveryScheme, bool, Colours>)
DEF_CMD_TUPLE_NT (CMD_BUY_SHARE_IN_COMPANY,     CmdBuyShareInCompany,     {},                                         CommandType::MoneyManagement, CmdDataT<CompanyID>)
DEF_CMD_TUPLE_NT (CMD_SELL_SHARE_IN_COMPANY,    CmdSellShareInCompany,    {},                                         CommandType::MoneyManagement, CmdDataT<CompanyID>)
DEF_CMD_TUPLE_NT (CMD_BUY_COMPANY,              CmdBuyCompany,            {},                                         CommandType::MoneyManagement, CmdDataT<CompanyID, bool>)
DEF_CMD_TUPLE_NT (CMD_DECLINE_BUY_COMPANY,      CmdDeclineBuyCompany,     CMD_NO_EST,                                 CommandType::ServerSetting,   CmdDataT<CompanyID>)

#endif /* COMPANY_CMD_H */
