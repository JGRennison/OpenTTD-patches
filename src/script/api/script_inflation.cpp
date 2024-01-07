/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_inflation.cpp Implementation of ScriptInflation. */

#include "../../stdafx.h"
#include "script_inflation.hpp"
#include "script_error.hpp"
#include "../../economy_func.h"
#include "../../cheat_type.h"
#include "../../command_type.h"

#include "../../safeguards.h"

/* static */ int64_t ScriptInflation::GetPriceFactor()
{
	return _economy.inflation_prices;
}

/* static */ int64_t ScriptInflation::GetPaymentFactor()
{
	return _economy.inflation_payment;
}

/* static */ bool ScriptInflation::SetPriceFactor(int64_t factor)
{
	EnforcePrecondition(false, factor >= 1 << 16 && factor <= (int64_t)MAX_INFLATION);
	if ((uint64_t)factor == _economy.inflation_prices) return true;
	return ScriptObject::DoCommand(0, CHT_INFLATION_COST, (uint32_t)factor, CMD_CHEAT_SETTING);
}

/* static */ bool ScriptInflation::SetPaymentFactor(int64_t factor)
{
	EnforcePrecondition(false, factor >= 1 << 16 && factor <= (int64_t)MAX_INFLATION);
	if ((uint64_t)factor == _economy.inflation_payment) return true;
	return ScriptObject::DoCommand(0, CHT_INFLATION_INCOME, (uint32_t)factor, CMD_CHEAT_SETTING);
}
