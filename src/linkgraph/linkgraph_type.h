/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph_type.h Declaration of link graph types used for cargo distribution. */

#ifndef LINKGRAPH_TYPE_H
#define LINKGRAPH_TYPE_H

#include "../core/pool_id_type.hpp"

struct LinkGraphIDTag : public PoolIDTraits<uint16_t, 0xFFFF, 0xFFFF> {};
using LinkGraphID = PoolID<LinkGraphIDTag>;

struct LinkGraphJobIDTag : public PoolIDTraits<uint16_t, 0xFFFF, 0xFFFF> {};
using LinkGraphJobID = PoolID<LinkGraphJobIDTag>;

typedef uint16_t NodeID;
static const NodeID INVALID_NODE = UINT16_MAX;

enum DistributionType : uint8_t {
	DT_MANUAL = 0,           ///< Manual distribution. No link graph calculations are run.
	DT_ASYMMETRIC = 1,       ///< Asymmetric distribution. Usually cargo will only travel in one direction.
	DT_SYMMETRIC = 2,        ///< Symmetric distribution. The same amount of cargo travels in each direction between each pair of nodes.

	DT_ASYMMETRIC_EQ = 20,   ///< Asymmetric distribution (equal). Usually cargo will only travel in one direction. Attempt to distribute the same amount of cargo to each sink.
	DT_ASYMMETRIC_NEAR = 21, ///< Asymmetric distribution (nearest). Usually cargo will only travel in one direction. Attempt to distribute cargo to the nearest sink.

	DT_PER_CARGO_DEFAULT = 128, ///< Per cargo: Use default value
};

/**
 * Special modes for updating links. 'Restricted' means that vehicles with
 * 'no loading' orders are serving the link. If a link is only served by
 * such vehicles it's 'fully restricted'. This means the link can be used
 * by cargo arriving in such vehicles, but not by cargo generated or
 * transferring at the source station of the link. In order to find out
 * about this condition we keep two update timestamps in each link, one for
 * the restricted and one for the unrestricted part of it. If either one
 * times out while the other is still valid the link becomes fully
 * restricted or fully unrestricted, respectively.
 * Refreshing a link makes just sure a minimum capacity is kept. Increasing
 * actually adds the given capacity.
 */
enum class EdgeUpdateMode : uint8_t {
	Increase, ///< Increase capacity.
	Refresh, ///< Refresh capacity.
	Restricted, ///< Use restricted link.
	Unrestricted, ///< Use unrestricted link.
	Aircraft, ///< Capacity is an aircraft link.
};

using EdgeUpdateModes = EnumBitSet<EdgeUpdateMode, uint8_t>;

#endif /* LINKGRAPH_TYPE_H */
