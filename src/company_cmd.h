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
	auto GetRefTuple() { return std::tie(this->cca, this->company_id, this->reason, this->client_id, this->to_merge_id); }
};
struct CmdCompanyCtrlData final : public TupleRefCmdData<CmdCompanyCtrlData, CmdCompanyCtrlInnerData> {
	void SetClientID(ClientID client_id) override;
	void FormatDebugSummary(struct format_target &) const override;
};

DEF_CMD_TUPLE_NT (CMD_COMPANY_CTRL,             CmdCompanyCtrl,           CMD_SPECTATOR | CMD_CLIENT_ID | CMD_NO_EST, CMDT_SERVER_SETTING,   CmdCompanyCtrlData)
DEF_CMD_TUPLE_NT (CMD_COMPANY_ALLOW_LIST_CTRL,  CmdCompanyAllowListCtrl,  CMD_NO_EST,                                 CMDT_OTHER_MANAGEMENT, CmdDataT<CompanyAllowListCtrlAction, std::string>)

DEF_CMD_TUPLE_NT (CMD_GIVE_MONEY,               CmdGiveMoney,             {},                                         CMDT_MONEY_MANAGEMENT, CmdDataT<Money, CompanyID>)
DEF_CMD_TUPLE_NT (CMD_RENAME_COMPANY,           CmdRenameCompany,         {},                                         CMDT_COMPANY_SETTING,  CmdDataT<std::string>)
DEF_CMD_TUPLE_NT (CMD_RENAME_PRESIDENT,         CmdRenamePresident,       {},                                         CMDT_COMPANY_SETTING,  CmdDataT<std::string>)
DEF_CMD_TUPLE_NT (CMD_SET_COMPANY_MANAGER_FACE, CmdSetCompanyManagerFace, {},                                         CMDT_COMPANY_SETTING,  CmdDataT<CompanyManagerFace>)
DEF_CMD_TUPLE_NT (CMD_SET_COMPANY_COLOUR,       CmdSetCompanyColour,      {},                                         CMDT_COMPANY_SETTING,  CmdDataT<LiveryScheme, bool, Colours>)
DEF_CMD_TUPLE_NT (CMD_BUY_SHARE_IN_COMPANY,     CmdBuyShareInCompany,     {},                                         CMDT_MONEY_MANAGEMENT, CmdDataT<CompanyID>)
DEF_CMD_TUPLE_NT (CMD_SELL_SHARE_IN_COMPANY,    CmdSellShareInCompany,    {},                                         CMDT_MONEY_MANAGEMENT, CmdDataT<CompanyID>)

#endif /* COMPANY_CMD_H */
