/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strings_func.cpp Test functionality from strings_func. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../strings_func.h"
#include "../core/format.hpp"


TEST_CASE("AppendStringWithArgsInPlace - format_to_fixed_base")
{
	format_buffer buf;
	buf.append("Name: ");
	AppendStringInPlace(buf, SPECSTR_TOWNNAME_START + 15, 0x8854AB3D);
	CHECK((std::string_view)buf == "Name: Prasice nad Labem");

	format_buffer_fixed<100> fixed_big;
	fixed_big.append("Name: ");
	AppendStringInPlace(fixed_big, SPECSTR_TOWNNAME_START + 15, 0x8854AB3D);
	CHECK((std::string_view)fixed_big == "Name: Prasice nad Labem");

	format_buffer_fixed<10> fixed_small;
	fixed_small.append("Name: ");
	AppendStringInPlace(fixed_small, SPECSTR_TOWNNAME_START + 15, 0x8854AB3D);
	CHECK((std::string_view)fixed_small == "Name: Pras");
}
