/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file string_extra.cpp Extra string functions. */

#include "stdafx.h"
#include "string_func.h"
#include "3rdparty/md5/md5.h"

/**
 * Convert the md5sum to a hexadecimal string representation
 * @param buf buffer to put the md5sum into
 * @param last last character of buffer (usually lastof(buf))
 * @param md5sum the md5sum itself
 * @return a pointer to the next character after the md5sum
 * @return the string representation of the md5sum.
 */
char *md5sumToString(char *buf, const char *last, const MD5Hash &md5sum)
{
	char *p = buf;

	for (size_t i = 0; i < md5sum.size(); i++) {
		p += seprintf(p, last, "%02X", md5sum[i]);
	}

	return p;
}
