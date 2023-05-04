/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file misc_osx.cpp OS X misc functionality */

#include "../../stdafx.h"

#include "../../safeguards.h"

#include <variant>

/* See: https://stackoverflow.com/questions/52310835/xcode-10-call-to-unavailable-function-stdvisit/53868971#53868971 */
const char* std::bad_variant_access::what() const noexcept {
	return "bad_variant_access";
}
