/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph.h Declaration of link graph classes used for cargo distribution. */

#ifndef LINKGRAPH_H
#define LINKGRAPH_H

#include "../core/pool_type.hpp"
#include "../core/bitmath_func.hpp"
#include "../station_base.h"
#include "../cargotype.h"
#include "../date_func.h"
#include "../sl/saveload_common.h"
#include "linkgraph_type.h"
#include "../3rdparty/cpp-btree/btree_map.h"
#include <utility>
#include <vector>

class LinkGraph;

/**
 * Type of the pool for link graph components. Each station can be in at up to
 * 32 link graphs. So we allow for plenty of them to be created.
 */
typedef Pool<LinkGraph, LinkGraphID, 32, 0xFFFF> LinkGraphPool;
/** The actual pool with link graphs. */
extern LinkGraphPool _link_graph_pool;

namespace upstream_sl {
	SaveLoadTable GetLinkGraphDesc();
	SaveLoadTable GetLinkGraphJobDesc();
	class SlLinkgraphNode;
	class SlLinkgraphEdge;
}

/**
 * A connected component of a link graph. Contains a complete set of stations
 * connected by links as nodes and edges. Each component also holds a copy of
 * the link graph settings at the time of its creation. The global settings
 * might change between the creation and join time so we can't rely on them.
 */
class LinkGraph : public LinkGraphPool::PoolItem<&_link_graph_pool> {
public:

	/**
	 * Node of the link graph. contains all relevant information from the associated
	 * station. It's copied so that the link graph job can work on its own data set
	 * in a separate thread.
	 */
	struct BaseNode {
		uint supply;             ///< Supply at the station.
		uint demand;             ///< Acceptance at the station.
		StationID station;       ///< Station ID.
		TileIndex xy;            ///< Location of the station referred to by the node.
		EconTime::Date last_update; ///< When the supply was last updated.
		void Init(TileIndex xy = INVALID_TILE, StationID st = INVALID_STATION, uint demand = 0);
	};

	/**
	 * An edge in the link graph. Corresponds to a link between two stations or at
	 * least the distance between them. Edges from one node to itself contain the
	 * ID of the opposite Node of the first active edge (i.e. not just distance) in
	 * the column as next_edge.
	 */
	struct BaseEdge {
		uint capacity;                 ///< Capacity of the link.
		uint usage;                    ///< Usage of the link.
		uint64_t travel_time_sum;      ///< Sum of the travel times of the link, in ticks.
		EconTime::Date last_unrestricted_update; ///< When the unrestricted part of the link was last updated.
		EconTime::Date last_restricted_update;   ///< When the restricted part of the link was last updated.
		EconTime::Date last_aircraft_update;     ///< When aircraft capacity of the link was last updated.

		void Init()
		{
			this->capacity = 0;
			this->usage = 0;
			this->travel_time_sum = 0;
			this->last_unrestricted_update = EconTime::INVALID_DATE;
			this->last_restricted_update = EconTime::INVALID_DATE;
			this->last_aircraft_update = EconTime::INVALID_DATE;
		}

		BaseEdge() { this->Init(); }
	};

	typedef std::vector<BaseNode> NodeVector;
	typedef btree::btree_map<std::pair<NodeID, NodeID>, BaseEdge> EdgeMatrix;

	/**
	 * Wrapper for an edge (const or not) allowing retrieval, but no modification.
	 * @tparam Tedge Actual edge class, may be "const BaseEdge" or just "BaseEdge".
	 */
	template <typename Tedge>
	class EdgeWrapper {
	protected:
		Tedge *edge; ///< Actual edge to be used.

	public:

		/**
		 * Wrap a an edge.
		 * @param edge Edge to be wrapped.
		 */
		EdgeWrapper (Tedge &edge) : edge(&edge) {}

		/**
		 * Get edge's capacity.
		 * @return Capacity.
		 */
		uint Capacity() const { return this->edge->capacity; }

		/**
		 * Get edge's usage.
		 * @return Usage.
		 */
		uint Usage() const { return this->edge->usage; }

		/**
		 * Get edge's average travel time.
		 * @return Travel time, in ticks.
		 */
		uint32_t TravelTime() const { return this->edge->travel_time_sum / this->edge->capacity; }

		/**
		 * Get the date of the last update to the edge's unrestricted capacity.
		 * @return Last update.
		 */
		EconTime::Date LastUnrestrictedUpdate() const { return this->edge->last_unrestricted_update; }

		/**
		 * Get the date of the last update to the edge's restricted capacity.
		 * @return Last update.
		 */
		EconTime::Date LastRestrictedUpdate() const { return this->edge->last_restricted_update; }

		/**
		 * Get the date of the last update to the edge's aircraft capacity.
		 * @return Last update.
		 */
		EconTime::Date LastAircraftUpdate() const { return this->edge->last_aircraft_update; }

		/**
		 * Get the date of the last update to any part of the edge's capacity.
		 * @return Last update.
		 */
		EconTime::Date LastUpdate() const { return std::max(this->edge->last_unrestricted_update, this->edge->last_restricted_update); }
	};

	/**
	 * Wrapper for a node (const or not) allowing retrieval, but no modification.
	 * @tparam Tedge Actual node class, may be "const BaseNode" or just "BaseNode".
	 */
	template <typename Tnode>
	class NodeWrapper {
	protected:
		Tnode &node;          ///< Node being wrapped.
		NodeID index;         ///< ID of wrapped node.

	public:

		/**
		 * Wrap a node.
		 * @param node Node to be wrapped.
		 * @param index ID of node to be wrapped.
		 */
		NodeWrapper(Tnode &node, NodeID index) : node(node), index(index) {}

		/**
		 * Get supply of wrapped node.
		 * @return Supply.
		 */
		uint Supply() const { return this->node.supply; }

		/**
		 * Get demand of wrapped node.
		 * @return Demand.
		 */
		uint Demand() const { return this->node.demand; }

		/**
		 * Get ID of station belonging to wrapped node.
		 * @return ID of node's station.
		 */
		StationID Station() const { return this->node.station; }

		/**
		 * Get node's last update.
		 * @return Last update.
		 */
		EconTime::Date LastUpdate() const { return this->node.last_update; }

		/**
		 * Get the location of the station associated with the node.
		 * @return Location of the station.
		 */
		TileIndex XY() const { return this->node.xy; }

		NodeID GetNodeID() const { return this->index; }
	};

	/**
	 * A constant edge class.
	 */
	typedef EdgeWrapper<const BaseEdge> ConstEdge;

	/**
	 * An updatable edge class.
	 */
	class Edge : public EdgeWrapper<BaseEdge> {
	public:
		/**
		 * Constructor
		 * @param edge Edge to be wrapped.
		 */
		Edge(BaseEdge &edge) : EdgeWrapper<BaseEdge>(edge) {}
		void Update(uint capacity, uint usage, uint32_t time, EdgeUpdateMode mode);
		void Restrict() { this->edge->last_unrestricted_update = EconTime::INVALID_DATE; }
		void Release() { this->edge->last_restricted_update = EconTime::INVALID_DATE; }
		void ClearAircraft() { this->edge->last_aircraft_update = EconTime::INVALID_DATE; }
	};

	/**
	 * Constant node class. Only retrieval operations are allowed on both the
	 * node itself and its edges.
	 */
	class ConstNode : public NodeWrapper<const BaseNode> {
	public:
		/**
		 * Constructor.
		 * @param lg LinkGraph to get the node from.
		 * @param node ID of the node.
		 */
		ConstNode(const LinkGraph *lg, NodeID node) :
			NodeWrapper<const BaseNode>(lg->nodes[node], node)
		{}
	};

	/**
	 * Updatable node class. The node itself as well as its edges can be modified.
	 */
	class Node : public NodeWrapper<BaseNode> {
	public:
		/**
		 * Constructor.
		 * @param lg LinkGraph to get the node from.
		 * @param node ID of the node.
		 */
		Node(LinkGraph *lg, NodeID node) :
			NodeWrapper<BaseNode>(lg->nodes[node], node)
		{}

		/**
		 * Update the node's supply and set last_update to the current date.
		 * @param supply Supply to be added.
		 */
		void UpdateSupply(uint supply)
		{
			this->node.supply += supply;
			this->node.last_update = EconTime::CurDate();
		}

		/**
		 * Update the node's location on the map.
		 * @param xy New location.
		 */
		void UpdateLocation(TileIndex xy)
		{
			this->node.xy = xy;
		}

		/**
		 * Set the node's demand.
		 * @param demand New demand for the node.
		 */
		void SetDemand(uint demand)
		{
			this->node.demand = demand;
		}
	};

	/** Minimum effective distance for timeout calculation. */
	static const uint MIN_TIMEOUT_DISTANCE = 32;

	/** Number of days before deleting links served only by vehicles stopped in depot. */
	static constexpr EconTime::DateDelta STALE_LINK_DEPOT_TIMEOUT{1024};

	/** Minimum number of ticks between subsequent compressions of a LG. */
	static constexpr ScaledTickCounter COMPRESSION_INTERVAL = 256 * DAY_TICKS;

	/**
	 * Scale a value from a link graph of age orig_age for usage in one of age
	 * target_age. Make sure that the value stays > 0 if it was > 0 before.
	 * @param val Value to be scaled.
	 * @param target_age Age of the target link graph.
	 * @param orig_age Age of the original link graph.
	 * @return scaled value.
	 */
	inline static uint Scale(uint val, uint target_age, uint orig_age)
	{
		return val > 0 ? std::max(1U, val * target_age / orig_age) : 0;
	}

	/** Bare constructor, only for save/load. */
	LinkGraph() : cargo(INVALID_CARGO), last_compression(0) {}
	/**
	 * Real constructor.
	 * @param cargo Cargo the link graph is about.
	 */
	LinkGraph(CargoType cargo) : cargo(cargo), last_compression(_scaled_tick_counter) {}

	void Init(uint size);
	void ShiftDates(EconTime::DateDelta interval);
	void Compress();
	void Merge(LinkGraph *other);

	/* Splitting link graphs is intentionally not implemented.
	 * The overhead in determining connectedness would probably outweigh the
	 * benefit of having to deal with smaller graphs. In real world examples
	 * networks generally grow. Only rarely a network is permanently split.
	 * Reacting to temporary splits here would obviously create performance
	 * problems and detecting the temporary or permanent nature of splits isn't
	 * trivial. */

	/**
	 * Get a node with the specified id.
	 * @param num ID of the node.
	 * @return the Requested node.
	 */
	inline Node operator[](NodeID num) { return Node(this, num); }

	/**
	 * Get a const reference to a node with the specified id.
	 * @param num ID of the node.
	 * @return the Requested node.
	 */
	inline ConstNode operator[](NodeID num) const { return ConstNode(this, num); }

	/**
	 * Get the current size of the component.
	 * @return Size.
	 */
	inline NodeID Size() const { return (NodeID)this->nodes.size(); }

	/**
	 * Get date of last compression.
	 * @return Date of last compression.
	 */
	inline ScaledTickCounter LastCompression() const { return this->last_compression; }

	/**
	 * Get the cargo type this component's link graph refers to.
	 * @return Cargo type.
	 */
	inline CargoType Cargo() const { return this->cargo; }

	/**
	 * Scale a value to its monthly equivalent, based on last compression.
	 * @param base Value to be scaled.
	 * @return Scaled value.
	 */
	inline uint Monthly(uint base) const
	{
		return (uint)((static_cast<uint64_t>(base) * 30 * DAY_TICKS * DayLengthFactor()) / std::max<uint64_t>(_scaled_tick_counter - this->last_compression, DAY_TICKS));
	}

	NodeID AddNode(const Station *st);
	void RemoveNode(NodeID id);

	void UpdateEdge(NodeID from, NodeID to, uint capacity, uint usage, uint32_t time, EdgeUpdateMode mode);
	void RemoveEdge(NodeID from, NodeID to);

	inline uint32_t CalculateCostEstimate() const {
		return (uint32_t)this->Size() * (uint32_t)this->Size();
	}

protected:
	friend class LinkGraph::ConstNode;
	friend class LinkGraph::Node;
	friend struct LinkGraphNodeStructHandler;
	friend struct LinkGraphNonTableHelper;
	friend NamedSaveLoadTable GetLinkGraphDesc();
	friend NamedSaveLoadTable GetLinkGraphJobDesc();

	friend upstream_sl::SaveLoadTable upstream_sl::GetLinkGraphDesc();
	friend upstream_sl::SaveLoadTable upstream_sl::GetLinkGraphJobDesc();
	friend upstream_sl::SlLinkgraphNode;
	friend upstream_sl::SlLinkgraphEdge;

	friend void LinkGraphFixupAfterLoad(bool compression_was_date);

	CargoType cargo;         ///< Cargo of this component's link graph.
	ScaledTickCounter last_compression; ///< Last time the capacities and supplies were compressed.
	NodeVector nodes;      ///< Nodes in the component.
	EdgeMatrix edges;      ///< Edges in the component.

public:
	const EdgeMatrix &GetEdges() const { return this->edges; }

	const BaseEdge &GetBaseEdge(NodeID from, NodeID to) const
	{
		auto iter = this->edges.find(std::make_pair(from, to));
		if (iter != this->edges.end()) return iter->second;

		static LinkGraph::BaseEdge empty_edge = {};
		return empty_edge;
	}

	ConstEdge GetConstEdge(NodeID from, NodeID to) const { return ConstEdge(this->GetBaseEdge(from, to)); }

	template <typename F>
	void IterateEdgesFromNode(NodeID from_id, F proc) const
	{
		auto iter = this->edges.lower_bound(std::make_pair(from_id, (NodeID)0));
		while (iter != this->edges.end()) {
			NodeID from = iter->first.first;
			NodeID to = iter->first.second;
			if (from != from_id) return;
			if (from != to) {
				proc(from, to, ConstEdge(iter->second));
			}
			++iter;
		}
	}

	enum class EdgeIterationResult {
		None,
		EraseEdge,
	};

	struct EdgeIterationHelper {
		EdgeMatrix &edges;
		EdgeMatrix::iterator &iter;
		const NodeID from_id;
		const NodeID to_id;
		size_t expected_size;

		EdgeIterationHelper(EdgeMatrix &edges, EdgeMatrix::iterator &iter, NodeID from_id, NodeID to_id) :
				edges(edges), iter(iter), from_id(from_id), to_id(to_id), expected_size(0) {}

		Edge GetEdge() { return Edge(this->iter->second); }

		void RecordSize() { this->expected_size = this->edges.size(); }

		bool RefreshIterationIfSizeChanged()
		{
			if (this->expected_size != this->edges.size()) {
				/* Edges container has resized, our iterator is now invalid, so find it again */
				this->iter = this->edges.find(std::make_pair(this->from_id, this->to_id));
				return true;
			} else {
				return false;
			}
		}
	};

	template <typename F>
	void MutableIterateEdgesFromNode(NodeID from_id, F proc)
	{
		EdgeMatrix::iterator iter = this->edges.lower_bound(std::make_pair(from_id, (NodeID)0));
		while (iter != this->edges.end()) {
			NodeID from = iter->first.first;
			NodeID to = iter->first.second;
			if (from != from_id) return;
			EdgeIterationResult result = EdgeIterationResult::None;
			if (from != to) {
				result = proc(EdgeIterationHelper(this->edges, iter, from, to));
			}
			switch (result) {
				case EdgeIterationResult::None:
					++iter;
					break;
				case EdgeIterationResult::EraseEdge:
					iter = this->edges.erase(iter);
					break;
			}
		}
	}
};

#endif /* LINKGRAPH_H */
