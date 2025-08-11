/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file misc_cmd.h Miscellaneous command definitions. */

#ifndef MISC_CMD_H
#define MISC_CMD_H

#include "command_type.h"
#include "economy_type.h"
#include "openttd.h"

enum class LoanCommand : uint8_t {
	Interval,
	Max,
	Amount,
};

DEF_CMD_TUPLE_NT (CMD_MONEY_CHEAT,          CmdMoneyCheat,        CMD_NO_EST,                 CMDT_CHEAT,            CmdDataT<Money>)
DEF_CMD_TUPLE_NT (CMD_MONEY_CHEAT_ADMIN,    CmdMoneyCheatAdmin,   CMD_SERVER_NS | CMD_NO_EST, CMDT_CHEAT,            CmdDataT<Money>)
DEF_CMD_TUPLE    (CMD_CHANGE_BANK_BALANCE,  CmdChangeBankBalance, CMD_DEITY,                  CMDT_MONEY_MANAGEMENT, CmdDataT<Money, CompanyID, ExpensesType>)
DEF_CMD_TUPLE_NT (CMD_INCREASE_LOAN,        CmdIncreaseLoan,      {},                         CMDT_MONEY_MANAGEMENT, CmdDataT<LoanCommand, Money>)
DEF_CMD_TUPLE_NT (CMD_DECREASE_LOAN,        CmdDecreaseLoan,      {},                         CMDT_MONEY_MANAGEMENT, CmdDataT<LoanCommand, Money>)
DEF_CMD_TUPLE_NT (CMD_SET_COMPANY_MAX_LOAN, CmdSetCompanyMaxLoan, CMD_DEITY,                  CMDT_MONEY_MANAGEMENT, CmdDataT<CompanyID, Money>)
DEF_CMD_TUPLE_NT (CMD_PAUSE,                CmdPause,             CMD_SERVER | CMD_NO_EST,    CMDT_SERVER_SETTING,   CmdDataT<PauseMode, bool>)
DEF_CMD_TUPLE_NT (CMD_DESYNC_CHECK,         CmdDesyncCheck,       CMD_SERVER,                 CMDT_SERVER_SETTING,   EmptyCmdData)

#endif /* MISC_CMD_H */
