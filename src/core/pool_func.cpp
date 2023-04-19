/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file pool_func.cpp Implementation of PoolBase methods. */

#include "../stdafx.h"
#include "../error_func.h"
#include "pool_type.hpp"
#include "format.hpp"

#include "../safeguards.h"

/**
 * Destructor removes this object from the pool vector and
 * deletes the vector itself if this was the last item removed.
 */
/* virtual */ PoolBase::~PoolBase()
{
	PoolVector *pools = PoolBase::GetPools();
	pools->erase(std::find(pools->begin(), pools->end(), this));
	if (pools->empty()) delete pools;
}

/**
 * Clean all pools of given type.
 * @param pt pool types to clean.
 */
/* static */ void PoolBase::Clean(PoolType pt)
{
	for (PoolBase *pool : *PoolBase::GetPools()) {
		if (pool->type & pt) pool->CleanPool();
	}
}

/* These are here to avoid needing formatting includes in pool_func */
[[noreturn]] void PoolNoMoreFreeItemsError(const char *name)
{
	FatalError("{}: no more free items", name);
}

[[noreturn]] void PoolOutOfRangeError(const char *name, size_t index, size_t max_size)
{
	[[noreturn]] extern void SlErrorCorrupt(std::string msg);
	SlErrorCorrupt(fmt::format("{} index {} out of range ({})", name, index, max_size));
}

[[noreturn]] void PoolIndexAlreadyInUseError(const char *name, size_t index)
{
	[[noreturn]] extern void SlErrorCorrupt(std::string msg);
	SlErrorCorrupt(fmt::format("{} index {} already in use", name, index));
}
