/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file string_extra.cpp Extra string functions. */

#include "stdafx.h"
#include "string_func.h"
#include "core/format.hpp"
#include "3rdparty/md5/md5.h"

void MD5Hash::fmt_format_value(fmt_formattable_output &output) const
{
	for (size_t i = 0; i < this->size(); i++) {
		output.format("{:02X}", (*this)[i]);
	}
}

/**
 * Convert the md5sum to a hexadecimal string representation
 * @param md5sum the md5sum itself
 * @return the string representation of the md5sum.
 */
std::string md5sumToString(const MD5Hash &md5sum)
{
	return fmt::format("{}", md5sum);
}
