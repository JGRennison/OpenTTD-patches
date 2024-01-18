/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file hash_func.hpp Functions related to hashing operations. */

#ifndef HASH_FUNC_HPP
#define HASH_FUNC_HPP

/**
 * Simple 32 bit to 32 bit hash
 * From MurmurHash3
 */
inline uint32_t SimpleHash32(uint32_t h)
{
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
}

/**
 * Simple 64 bit to 64 bit hash
 * From MurmurHash3
 */
inline uint64_t SimpleHash64(uint64_t h)
{
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdULL;
	h ^= h >> 33;
	h *= 0xc4ceb9fe1a85ec53ULL;
	h ^= h >> 33;

	return h;
}

#endif /* HASH_FUNC_HPP */
