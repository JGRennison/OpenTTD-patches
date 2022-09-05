/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_inflation.hpp Inflation related code. */

#ifndef SCRIPT_INFLATION_HPP
#define SCRIPT_INFLATION_HPP

#include "script_object.hpp"

/**
 * Class that handles inflation related functions.
 * @api ai game
 *
 */
class ScriptInflation : public ScriptObject {
public:
	/**
	 * Get the inflation factor for prices.
	 * @return Inflation factor, 16 bit fixed point.
	 */
	static int64 GetPriceFactor();

	/**
	 * Get the inflation factor for payments.
	 * @return Inflation factor, 16 bit fixed point.
	 */
	static int64 GetPaymentFactor();

	/**
	 * Set the inflation factor for prices.
	 * @param factor Inflation factor, 16 bit fixed point.
	 * @return True, if the inflation factor was changed.
	 * @api -ai
	 */
	static bool SetPriceFactor(int64 factor);

	/**
	 * Set the inflation factor for payments.
	 * @param factor Inflation factor, 16 bit fixed point.
	 * @return True, if the inflation factor was changed.
	 * @api -ai
	 */
	static bool SetPaymentFactor(int64 factor);
};

#endif /* SCRIPT_INFLATION_HPP */
