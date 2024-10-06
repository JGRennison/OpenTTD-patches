/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file debug_dbg_assert.h Macros for normally-off extended debug asserts. */

#ifndef DEBUG_DBG_ASSERT_H
#define DEBUG_DBG_ASSERT_H

#ifdef WITH_FULL_ASSERTS
#	include "debug.h"
#	define dbg_assert_msg(expression, ...) assert_msg(expression, __VA_ARGS__)
#	define dbg_assert_msg_tile(expression, tile, ...) assert_msg_tile(expression, tile, __VA_ARGS__)
#else
#	define dbg_assert_msg(expression, ...)
#	define dbg_assert_msg_tile(expression, tile, ...)
#endif

#endif /* DEBUG_DBG_ASSERT_H */
