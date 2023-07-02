/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph.cpp Definition of link graph classes used for cargo distribution. */

#include "../stdafx.h"
#include "../core/pool_func.hpp"
#include "linkgraph.h"

#include "../safeguards.h"

/* Initialize the link-graph-pool */
LinkGraphPool _link_graph_pool("LinkGraph");
INSTANTIATE_POOL_METHODS(LinkGraph)

/**
 * Create a node or clear it.
 * @param xy Location of the associated station.
 * @param st ID of the associated station.
 * @param demand Demand for cargo at the station.
 */
inline void LinkGraph::BaseNode::Init(TileIndex xy, StationID st, uint demand)
{
	this->xy = xy;
	this->supply = 0;
	this->demand = demand;
	this->station = st;
	this->last_update = INVALID_DATE;
}

/**
 * Shift all dates by given interval.
 * This is useful if the date has been modified with the cheat menu.
 * @param interval Number of days to be added or subtracted.
 */
void LinkGraph::ShiftDates(int interval)
{
	for (NodeID node1 = 0; node1 < this->Size(); ++node1) {
		BaseNode &source = this->nodes[node1];
		if (source.last_update != INVALID_DATE) source.last_update += interval;
	}
	for (auto &it : this->edges) {
		BaseEdge &edge = it.second;
		if (edge.last_unrestricted_update != INVALID_DATE) edge.last_unrestricted_update += interval;
		if (edge.last_restricted_update != INVALID_DATE) edge.last_restricted_update += interval;
		if (edge.last_aircraft_update != INVALID_DATE) edge.last_aircraft_update += interval;
	}
}

void LinkGraph::Compress()
{
	this->last_compression = (_scaled_date_ticks + this->last_compression) / 2;
	for (NodeID node1 = 0; node1 < this->Size(); ++node1) {
		this->nodes[node1].supply /= 2;
	}
	for (auto &it : this->edges) {
		BaseEdge &edge = it.second;
		if (edge.capacity > 0) {
			uint new_capacity = std::max(1U, edge.capacity / 2);
			if (edge.capacity < (1 << 16)) {
				edge.travel_time_sum = edge.travel_time_sum * new_capacity / edge.capacity;
			} else if (edge.travel_time_sum != 0) {
				edge.travel_time_sum = std::max<uint64>(1, edge.travel_time_sum / 2);
			}
			edge.capacity = new_capacity;
			edge.usage /= 2;
		}
	}
}

/**
 * Merge a link graph with another one.
 * @param other LinkGraph to be merged into this one.
 */
void LinkGraph::Merge(LinkGraph *other)
{
	uint32 age = ClampTo<uint32>(CeilDivT<DateTicksScaled>(_scaled_date_ticks - this->last_compression + 1, DAY_TICKS));
	uint32 other_age = ClampTo<uint32>(CeilDivT<DateTicksScaled>(_scaled_date_ticks - other->last_compression + 1, DAY_TICKS));
	NodeID first = this->Size();
	this->nodes.reserve(first + other->Size());
	for (NodeID node1 = 0; node1 < other->Size(); ++node1) {
		Station *st = Station::Get(other->nodes[node1].station);
		NodeID new_node = this->AddNode(st);
		this->nodes[new_node].supply = LinkGraph::Scale(other->nodes[node1].supply, age, other_age);
		st->goods[this->cargo].link_graph = this->index;
		st->goods[this->cargo].node = new_node;
	}
	for (const auto &iter : other->edges) {
		std::pair<NodeID, NodeID> key = std::make_pair(iter.first.first + first, iter.first.second + first);
		BaseEdge edge = iter.second;
		if (key.first != key.second) {
			edge.capacity = LinkGraph::Scale(edge.capacity, age, other_age);
			edge.usage = LinkGraph::Scale(edge.usage, age, other_age);
			edge.travel_time_sum = LinkGraph::Scale(edge.travel_time_sum, age, other_age);
		}
		this->edges[key] = edge;
	}
	delete other;
}

/**
 * Remove a node from the link graph by overwriting it with the last node.
 * @param id ID of the node to be removed.
 */
void LinkGraph::RemoveNode(NodeID id)
{
	assert(id < this->Size());

	std::vector<std::pair<std::pair<NodeID, NodeID>, BaseEdge>> saved_nodes;

	NodeID last_node = this->Size() - 1;

	for (auto iter = this->edges.begin(); iter != this->edges.end();) {
		if (iter->first.first == id || iter->first.second == id) {
			/* Erase this node */
			iter = this->edges.erase(iter);
		} else if (iter->first.first == last_node || iter->first.second == last_node) {
			/* The edge refers to the last node, remove and save to be re-added later with the updated id */
			saved_nodes.push_back(std::make_pair(std::make_pair(iter->first.first == last_node ? id : iter->first.first, iter->first.second == last_node ? id : iter->first.second), iter->second));
			iter = this->edges.erase(iter);
		} else {
			++iter;
		}
	}
	for (const auto &it : saved_nodes) {
		this->edges.insert(it);
	}

	Station::Get(this->nodes[last_node].station)->goods[this->cargo].node = id;
	/* Erase node by swapping with the last element. Node index is referenced
	 * directly from station goods entries so the order and position must remain. */
	this->nodes[id] = this->nodes.back();
	this->nodes.pop_back();
}

/**
 * Add a node to the component and create empty edges associated with it. Set
 * the station's last_component to this component. Calculate the distances to all
 * other nodes. The distances to _all_ nodes are important as the demand
 * calculator relies on their availability.
 * @param st New node's station.
 * @return New node's ID.
 */
NodeID LinkGraph::AddNode(const Station *st)
{
	const GoodsEntry &good = st->goods[this->cargo];

	NodeID new_node = this->Size();
	this->nodes.emplace_back();

	this->nodes[new_node].Init(st->xy, st->index,
			HasBit(good.status, GoodsEntry::GES_ACCEPTANCE));

	return new_node;
}

/**
 * Fill an edge with values from a link. Set the restricted or unrestricted
 * update timestamp according to the given update mode.
 * @param edge Edge to fill.
 * @param capacity Capacity of the link.
 * @param usage Usage to be added.
 * @param mode Update mode to be used.
 */
static void AddEdge(LinkGraph::BaseEdge &edge, uint capacity, uint usage, uint32 travel_time, EdgeUpdateMode mode)
{
	edge.capacity = capacity;
	edge.usage = usage;
	edge.travel_time_sum = static_cast<uint64>(travel_time) * capacity;
	if (mode & EUM_UNRESTRICTED)  edge.last_unrestricted_update = _date;
	if (mode & EUM_RESTRICTED) edge.last_restricted_update = _date;
	if (mode & EUM_AIRCRAFT) edge.last_aircraft_update = _date;
}

/**
 * Creates an edge if none exists yet or updates an existing edge.
 * @param from Source node.
 * @param to Target node.
 * @param capacity Capacity of the link.
 * @param usage Usage to be added.
 * @param mode Update mode to be used.
 */
void LinkGraph::UpdateEdge(NodeID from, NodeID to, uint capacity, uint usage, uint32 travel_time, EdgeUpdateMode mode)
{
	assert(capacity > 0);
	assert(usage <= capacity);
	BaseEdge &edge = this->edges[std::make_pair(from, to)];
	if (edge.capacity == 0) {
		assert(from != to);
		AddEdge(edge, capacity, usage, travel_time, mode);
	} else {
		Edge(edge).Update(capacity, usage, travel_time, mode);
	}
}

/**
 * Remove an outgoing edge from this node.
 * @param from ID of source node.
 * @param to ID of destination node.
 */
void LinkGraph::RemoveEdge(NodeID from, NodeID to)
{
	if (from == to) return;
	this->edges.erase(std::make_pair(from, to));
}

/**
 * Update an edge. If mode contains UM_REFRESH refresh the edge to have at
 * least the given capacity and usage, otherwise add the capacity, usage and travel time.
 * In any case set the respective update timestamp(s), according to the given
 * mode.
 * @param capacity Capacity to be added/updated.
 * @param usage Usage to be added.
 * @param travel_time Travel time to be added, in ticks.
 * @param mode Update mode to be applied.
 */
void LinkGraph::Edge::Update(uint capacity, uint usage, uint32 travel_time, EdgeUpdateMode mode)
{
	BaseEdge &edge = *(this->edge);
	assert(edge.capacity > 0);
	assert(capacity >= usage);

	if (mode & EUM_INCREASE) {
		if (edge.travel_time_sum == 0) {
			edge.travel_time_sum = static_cast<uint64>(edge.capacity + capacity) * travel_time;
		} else if (travel_time == 0) {
			edge.travel_time_sum += (edge.travel_time_sum / edge.capacity) * capacity;
		} else {
			edge.travel_time_sum += static_cast<uint64>(travel_time) * capacity;
		}
		edge.capacity += capacity;
		edge.usage += usage;
	} else if (mode & EUM_REFRESH) {
		/* If travel time is not provided, we scale the stored time based on
		 * the capacity increase. */
		if (capacity > edge.capacity) {
			if (travel_time == 0) {
				edge.travel_time_sum = static_cast<uint64>(edge.travel_time_sum / edge.capacity) * capacity;
			} else {
				edge.travel_time_sum += static_cast<uint64>(capacity - edge.capacity) * travel_time;
			}
			edge.capacity = capacity;
		} else if (edge.travel_time_sum == 0) {
			edge.travel_time_sum = static_cast<uint64>(travel_time) * edge.capacity;
		}
		edge.usage = std::max(edge.usage, usage);
	}
	if (mode & EUM_UNRESTRICTED) edge.last_unrestricted_update = _date;
	if (mode & EUM_RESTRICTED) edge.last_restricted_update = _date;
	if (mode & EUM_AIRCRAFT) edge.last_aircraft_update = _date;
}

/**
 * Resize the component and fill it with empty nodes and edges. Used when
 * loading from save games. The component is expected to be empty before.
 * @param size New size of the component.
 */
void LinkGraph::Init(uint size)
{
	assert(this->Size() == 0);
	this->nodes.resize(size);
}

void AdjustLinkGraphScaledTickBase(int64 delta)
{
	for (LinkGraph *lg : LinkGraph::Iterate()) lg->last_compression += delta;
}

void LinkGraphFixupLastCompressionAfterLoad()
{
	/* last_compression was previously a Date, change it to a DateTicksScaled */
	for (LinkGraph *lg : LinkGraph::Iterate()) lg->last_compression = DateToScaledDateTicks((Date)lg->last_compression);
}
