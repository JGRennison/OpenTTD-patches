/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file misc_cmd.h Miscellaneous command definitions. */

#ifndef MISC_CMD_H
#define MISC_CMD_H

#include "command_type.h"
#include "economy_type.h"
#include "openttd.h"

/** Different ways to determine the amount to loan/repay. */
enum class LoanCommand : uint8_t {
	Interval, ///< Loan/repay LOAN_INTERVAL.
	Max, ///< Loan/repay the maximum amount permitting money/settings.
	Amount, ///< Loan/repay the given amount.
};

DEF_CMD_TUPLE_NT (Commands::MoneyCheat,          CmdMoneyCheat,        CMD_NO_EST,                 CommandType::Cheat,           CmdDataT<Money>)
DEF_CMD_TUPLE_NT (Commands::MoneyCheatAdmin,     CmdMoneyCheatAdmin,   CMD_SERVER_NS | CMD_NO_EST, CommandType::Cheat,           CmdDataT<Money>)
DEF_CMD_TUPLE    (Commands::ChangeBankBalance,   CmdChangeBankBalance, CMD_DEITY,                  CommandType::MoneyManagement, CmdDataT<Money, CompanyID, ExpensesType>)
DEF_CMD_TUPLE_NT (Commands::IncreaseLoan,        CmdIncreaseLoan,      {},                         CommandType::MoneyManagement, CmdDataT<LoanCommand, Money>)
DEF_CMD_TUPLE_NT (Commands::DecreaseLoan,        CmdDecreaseLoan,      {},                         CommandType::MoneyManagement, CmdDataT<LoanCommand, Money>)
DEF_CMD_TUPLE_NT (Commands::SetCompanyMaxLoan,   CmdSetCompanyMaxLoan, CMD_DEITY,                  CommandType::MoneyManagement, CmdDataT<CompanyID, Money>)
DEF_CMD_TUPLE_NT (Commands::Pause,               CmdPause,             CMD_SERVER | CMD_NO_EST,    CommandType::ServerSetting,   CmdDataT<PauseMode, bool>)
DEF_CMD_TUPLE_NT (Commands::DesyncCheck,         CmdDesyncCheck,       CMD_SERVER,                 CommandType::ServerSetting,   EmptyCmdData)

#endif /* MISC_CMD_H */
