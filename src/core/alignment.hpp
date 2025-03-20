/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file alignment.hpp Definition of various alignment-related macros. */

#ifndef ALIGNMENT_HPP
#define ALIGNMENT_HPP

#if defined(ARM) || defined(__arm__) || defined(__alpha__)
	/** The architecture requires aligned access. */
#	define OTTD_ALIGNMENT 1
#else
	/** The architecture does not require aligned access. */
#	define OTTD_ALIGNMENT 0
#endif

#if !defined(NO_TAGGED_PTRS) && (defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64))
	/** Tagged pointers (upper bits) may be used on this architecture */
#	define OTTD_UPPER_TAGGED_PTR 1
#else
	/** Tagged pointers (upper bits) may not be used on this architecture */
#	define OTTD_UPPER_TAGGED_PTR 0
#endif

#endif /* ALIGNMENT_HPP */
