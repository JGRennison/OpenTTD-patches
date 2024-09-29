/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file debug_desync.h Desync debugging. */

#ifndef DEBUG_DESYNC_H
#define DEBUG_DESYNC_H

#include <functional>

enum CheckCachesFlags : uint32_t {
	CHECK_CACHE_NONE               =       0,
	CHECK_CACHE_GENERAL            = 1 <<  0,
	CHECK_CACHE_INFRA_TOTALS       = 1 <<  1,
	CHECK_CACHE_WATER_REGIONS      = 1 <<  2,
	CHECK_CACHE_ALL                = UINT16_MAX,
	CHECK_CACHE_EMIT_LOG           = 1 << 16,
};
DECLARE_ENUM_AS_BIT_SET(CheckCachesFlags)

extern void CheckCaches(bool force_check, std::function<void(std::string_view)> log = nullptr, CheckCachesFlags flags = CHECK_CACHE_ALL);

#endif /* DEBUG_DESYNC_H */
