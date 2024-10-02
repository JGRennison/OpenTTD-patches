/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file fmt_target.cpp Test functionality of struct format_target and subclasses. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../core/format.hpp"

TEST_CASE("Test format_to_fixed")
{
	char buffer[16];

	format_to_fixed buf(buffer, sizeof(buffer));

	CHECK((std::string_view)buf == "");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 0);
	CHECK(!buf.has_overflowed());

	buf.format("{:x}: ", 0xdadcafe);
	CHECK((std::string_view)buf == "dadcafe: ");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 9);
	CHECK(!buf.has_overflowed());

	buf.append("Lorem ipsum dolor sit amet, consectetur adipiscing elit");
	CHECK((std::string_view)buf == "dadcafe: Lorem i");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 16);
	CHECK(buf.has_overflowed());

	buf.append("sed do eiusmod tempor incididunt ut labore et dolore magna aliqua");
	CHECK((std::string_view)buf == "dadcafe: Lorem i");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 16);
	CHECK(buf.has_overflowed());

	buf.restore_size(12);
	CHECK((std::string_view)buf == "dadcafe: Lor");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 12);
	CHECK(!buf.has_overflowed());

	buf.format("{}", M_PI);
	CHECK((std::string_view)buf == "dadcafe: Lor3.14");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 16);
	CHECK(buf.has_overflowed());
}

TEST_CASE("Test format_to_fixed_z")
{
	char buffer[16];

	format_to_fixed_z buf(buffer, lastof(buffer));

	CHECK((std::string_view)buf == "");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 0);
	CHECK(!buf.has_overflowed());
	char *end = buf.finalise();
	CHECK(end == buffer);
	CHECK(*end == '\0');

	buf.format("{:x}: ", 0xdadcafe);
	CHECK((std::string_view)buf == "dadcafe: ");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 9);
	CHECK(!buf.has_overflowed());
	end = buf.finalise();
	CHECK(end == buffer + 9);
	CHECK(*end == '\0');

	buf.append("Lorem ipsum dolor sit amet, consectetur adipiscing elit");
	CHECK((std::string_view)buf == "dadcafe: Lorem ");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 15);
	CHECK(buf.has_overflowed());
	end = buf.finalise();
	CHECK(end == buffer + 15);
	CHECK(*end == '\0');

	buf.append("sed do eiusmod tempor incididunt ut labore et dolore magna aliqua");
	CHECK((std::string_view)buf == "dadcafe: Lorem ");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 15);
	CHECK(buf.has_overflowed());
	end = buf.finalise();
	CHECK(end == buffer + 15);
	CHECK(*end == '\0');

	buf.restore_size(12);
	CHECK((std::string_view)buf == "dadcafe: Lor");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 12);
	CHECK(!buf.has_overflowed());
	end = buf.finalise();
	CHECK(end == buffer + 12);
	CHECK(*end == '\0');

	buf.format("{}", M_PI);
	CHECK((std::string_view)buf == "dadcafe: Lor3.1");
	CHECK(buf.data() == buffer);
	CHECK(buf.size() == 15);
	CHECK(buf.has_overflowed());
	end = buf.finalise();
	CHECK(end == buffer + 15);
	CHECK(*end == '\0');
}
