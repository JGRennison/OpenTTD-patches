/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraphjob.h Declaration of link graph job classes used for cargo distribution. */

#ifndef LINKGRAPHJOB_H
#define LINKGRAPHJOB_H

#include "../thread.h"
#include "../core/dyn_arena_alloc.hpp"
#include "linkgraph.h"
#include <vector>
#include <memory>
#include <atomic>

class LinkGraphJob;
class Path;
class LinkGraphJobGroup;
typedef std::vector<Path *> PathList;

/** Type of the pool for link graph jobs. */
typedef Pool<LinkGraphJob, LinkGraphJobID, 32, 0xFFFF> LinkGraphJobPool;
/** The actual pool with link graph jobs. */
extern LinkGraphJobPool _link_graph_job_pool;

/**
 * Class for calculation jobs to be run on link graphs.
 */
class LinkGraphJob : public LinkGraphJobPool::PoolItem<&_link_graph_job_pool>{
public:
	/**
	 * Annotation for a link graph demand edge.
	 */
	struct DemandAnnotation {
		NodeID dest;                 ///< Target node
		uint demand = 0;             ///< Transport demand between the nodes.
		uint unsatisfied_demand = 0; ///< Demand over this edge that hasn't been satisfied yet.
	};

	/**
	 * Annotation for a link graph flow edge.
	 */
	struct Edge {
	private:
		NodeID from;             ///< From Node.
		NodeID to;               ///< To Node.
		uint capacity;           ///< Capacity of the link.
		uint distance_anno;      ///< Pre-computed distance annotation.

		uint flow;               ///< Planned flow over this edge.

	public:
		/**
		 * Get edge's from node.
		 * @return from NodeID.
		 */
		NodeID From() const { return this->from; }

		/**
		 * Get edge's to node.
		 * @return to NodeID.
		 */
		NodeID To() const { return this->to; }

		/**
		 * Get edge's capacity.
		 * @return Capacity.
		 */
		uint Capacity() const { return this->capacity; }

		/**
		 * Get edge's distance annotation.
		 * @return Distance annotation.
		 */
		uint DistanceAnno() const { return this->distance_anno; }

		/**
		 * Get the total flow on the edge.
		 * @return Flow.
		 */
		uint Flow() const { return this->flow; }

		/**
		 * Add some flow.
		 * @param flow Flow to be added.
		 */
		void AddFlow(uint flow) { this->flow += flow; }

		/**
		 * Remove some flow.
		 * @param flow Flow to be removed.
		 */
		void RemoveFlow(uint flow)
		{
			dbg_assert(flow <= this->flow);
			this->flow -= flow;
		}

		void InitEdge(NodeID from, NodeID to, uint capacity, uint distance_anno)
		{
			this->from = from;
			this->to = to;
			this->capacity = capacity;
			this->distance_anno = distance_anno;
			this->flow = 0;
		}
	};

	typedef std::vector<Edge> EdgeAnnotationVector;

private:
	/**
	 * Annotation for a link graph node.
	 */
	struct NodeAnnotation {
		uint undelivered_supply; ///< Amount of supply that hasn't been distributed yet.
		uint received_demand;    ///< Received demand towards this node.
		PathList paths;          ///< Paths through this node, sorted so that those with flow == 0 are in the back.
		FlowStatMap flows;       ///< Planned flows to other nodes.
		span<DemandAnnotation> demands; ///< Demand annotations belonging to this node.
		span<Edge> edges;        ///< Edges with annotations belonging to this node.
		void Init(uint supply);
	};

	typedef std::vector<NodeAnnotation> NodeAnnotationVector;

	friend SaveLoadTable GetLinkGraphJobDesc();
	friend upstream_sl::SaveLoadTable upstream_sl::GetLinkGraphJobDesc();
	friend void GetLinkGraphJobDayLengthScaleAfterLoad(LinkGraphJob *lgj);
	friend class LinkGraphSchedule;
	friend class LinkGraphJobGroup;

protected:
	const LinkGraph link_graph;       ///< Link graph to by analyzed. Is copied when job is started and mustn't be modified later.

	std::shared_ptr<LinkGraphJobGroup> group; ///< Job group thread the job is running in or nullptr if it's running in the main thread.
	const LinkGraphSettings settings; ///< Copy of _settings_game.linkgraph at spawn time.
	DateTicks join_date_ticks;        ///< Date when the job is to be joined.
	DateTicks start_date_ticks;       ///< Date when the job was started.
	NodeAnnotationVector nodes;       ///< Extra node data necessary for link graph calculation.
	EdgeAnnotationVector edges;       ///< Edge data necessary for link graph calculation.
	std::atomic<bool> job_completed;  ///< Is the job still running. This is accessed by multiple threads and reads may be stale.
	std::atomic<bool> job_aborted;    ///< Has the job been aborted. This is accessed by multiple threads and reads may be stale.

	void EraseFlows(NodeID from);
	void JoinThread();
	void SetJobGroup(std::shared_ptr<LinkGraphJobGroup> group);

public:

	btree::btree_map<std::pair<NodeID, NodeID>, uint> demand_map; ///< Demand map.
	std::vector<DemandAnnotation> demand_annotation_store;        ///< Demand annotation store.

	DynUniformArenaAllocator path_allocator; ///< Arena allocator used for paths

	/**
	 * Link graph job node. Wraps a constant link graph node and a modifiable
	 * node annotation.
	 */
	class Node : public LinkGraph::ConstNode {
	private:
		NodeAnnotation &node_anno;  ///< Annotation being wrapped.
	public:

		/**
		 * Constructor.
		 * @param lgj Job to take the node from.
		 * @param node ID of the node.
		 */
		Node (LinkGraphJob *lgj, NodeID node) :
			LinkGraph::ConstNode(&lgj->link_graph, node),
			node_anno(lgj->nodes[node])
		{}

		/**
		 * Get amount of supply that hasn't been delivered, yet.
		 * @return Undelivered supply.
		 */
		uint UndeliveredSupply() const { return this->node_anno.undelivered_supply; }

		/**
		 * Get amount of supply that hasn't been delivered, yet.
		 * @return Undelivered supply.
		 */
		uint ReceivedDemand() const { return this->node_anno.received_demand; }

		/**
		 * Get the flows running through this node.
		 * @return Flows.
		 */
		FlowStatMap &Flows() { return this->node_anno.flows; }

		/**
		 * Get a constant version of the flows running through this node.
		 * @return Flows.
		 */
		const FlowStatMap &Flows() const { return this->node_anno.flows; }

		/**
		 * Get the paths this node is part of. Paths are always expected to be
		 * sorted so that those with flow == 0 are in the back of the list.
		 * @return Paths.
		 */
		PathList &Paths() { return this->node_anno.paths; }

		/**
		 * Get a constant version of the paths this node is part of.
		 * @return Paths.
		 */
		const PathList &Paths() const { return this->node_anno.paths; }

		/**
		 * Deliver some supply, adding demand to the respective edge.
		 * @param amount Amount of supply to be delivered.
		 */
		void DeliverSupply(uint amount)
		{
			this->node_anno.undelivered_supply -= amount;
		}

		/**
		 * Receive some demand, adding demand to the respective edge.
		 * @param amount Amount of demand to be received.
		 */
		void ReceiveDemand(uint amount)
		{
			this->node_anno.received_demand += amount;
		}

		span<DemandAnnotation> GetDemandAnnotations() const
		{
			return this->node_anno.demands;
		}

		void SetDemandAnnotations(span<DemandAnnotation> demands)
		{
			this->node_anno.demands = demands;
		}

		Edge &GetEdgeTo(NodeID to)
		{
			for (Edge &edge : this->node_anno.edges) {
				if (edge.To() == to) return edge;
			}

			static Edge empty_edge = {};
			return empty_edge;
		}

		span<Edge> GetEdges()
		{
			return this->node_anno.edges;
		}
	};

	/**
	 * Bare constructor, only for save/load. link_graph, join_date and actually
	 * settings have to be brutally const-casted in order to populate them.
	 */
	LinkGraphJob() : settings(_settings_game.linkgraph),
			join_date_ticks(INVALID_DATE), start_date_ticks(INVALID_DATE), job_completed(false), job_aborted(false) {}

	LinkGraphJob(const LinkGraph &orig, uint duration_multiplier);
	~LinkGraphJob();

	void Init();
	void FinaliseJob();

	/**
	 * Check if job has actually finished.
	 * This is allowed to spuriously return an incorrect value.
	 * @return True if job has actually finished.
	 */
	inline bool IsJobCompleted() const { return this->job_completed.load(std::memory_order_acquire); }

	/**
	 * Check if job has been aborted.
	 * This is allowed to spuriously return false incorrectly, but is not allowed to incorrectly return true.
	 * @return True if job has been aborted.
	 */
	inline bool IsJobAborted() const { return this->job_aborted.load(std::memory_order_acquire); }

	/**
	 * Abort job.
	 * The job may exit early at the next available opportunity.
	 * After this method has been called the state of the job is undefined, and the only valid operation
	 * is to join the thread and discard the job data.
	 */
	inline void AbortJob() { this->job_aborted.store(true, std::memory_order_release); }

	/**
	 * Check if job is supposed to be finished.
	 * @param tick_offset Optional number of ticks to add to the current date
	 * @return True if job should be finished by now, false if not.
	 */
	inline bool IsScheduledToBeJoined(int tick_offset = 0) const { return this->join_date_ticks <= (_date * DAY_TICKS) + _date_fract + tick_offset; }

	/**
	 * Get the date when the job should be finished.
	 * @return Join date.
	 */
	inline DateTicks JoinDateTicks() const { return join_date_ticks; }

	/**
	 * Get the date when the job was started.
	 * @return Start date.
	 */
	inline DateTicks StartDateTicks() const { return start_date_ticks; }

	/**
	 * Change the join date on date cheating.
	 * @param interval Number of days to add.
	 */
	inline void ShiftJoinDate(int interval) { this->join_date_ticks += interval * DAY_TICKS; }

	/**
	 * Get the link graph settings for this component.
	 * @return Settings.
	 */
	inline const LinkGraphSettings &Settings() const { return this->settings; }

	/**
	 * Get a node abstraction with the specified id.
	 * @param num ID of the node.
	 * @return the Requested node.
	 */
	inline Node operator[](NodeID num) { return Node(this, num); }

	/**
	 * Get the size of the underlying link graph.
	 * @return Size.
	 */
	inline NodeID Size() const { return this->link_graph.Size(); }

	/**
	 * Get the cargo of the underlying link graph.
	 * @return Cargo.
	 */
	inline CargoID Cargo() const { return this->link_graph.Cargo(); }

	/**
	 * Get the date when the underlying link graph was last compressed.
	 * @return Compression date.
	 */
	inline DateTicksScaled LastCompression() const { return this->link_graph.LastCompression(); }

	/**
	 * Get the ID of the underlying link graph.
	 * @return Link graph ID.
	 */
	inline LinkGraphID LinkGraphIndex() const { return this->link_graph.index; }

	/**
	 * Get a reference to the underlying link graph. Only use this for save/load.
	 * @return Link graph.
	 */
	inline const LinkGraph &Graph() const { return this->link_graph; }
};

/**
 * A leg of a path in the link graph. Paths can form trees by being "forked".
 */
class Path {
public:
	static Path *invalid_path;

	Path(NodeID n, bool source = false);
	virtual ~Path() = default;

	/** Get the node this leg passes. */
	inline NodeID GetNode() const { return this->node; }

	/** Get the overall origin of the path. */
	inline NodeID GetOrigin() const { return this->origin; }

	/** Get the parent leg of this one. */
	inline Path *GetParent() { return reinterpret_cast<Path *>(this->parent_storage & ~1); }

	/** Get the overall capacity of the path. */
	inline uint GetCapacity() const { return this->capacity; }

	/** Get the free capacity of the path. */
	inline int GetFreeCapacity() const { return this->free_capacity; }

	/**
	 * Get ratio of free * 16 (so that we get fewer 0) /
	 * max(total capacity, 1) (so that we don't divide by 0).
	 * @param free Free capacity.
	 * @param total Total capacity.
	 * @return free * 16 / max(total, 1).
	 */
	inline static int GetCapacityRatio(int free, uint total)
	{
		return Clamp(free, PATH_CAP_MIN_FREE, PATH_CAP_MAX_FREE) * PATH_CAP_MULTIPLIER / std::max(total, 1U);
	}

	/**
	 * Get capacity ratio of this path.
	 * @return free capacity * 16 / (total capacity + 1).
	 */
	inline int GetCapacityRatio() const
	{
		return Path::GetCapacityRatio(this->free_capacity, this->capacity);
	}

	/** Get the overall distance of the path. */
	inline uint GetDistance() const { return this->distance; }

	/** Reduce the flow on this leg only by the specified amount. */
	inline void ReduceFlow(uint f) { this->flow -= f; }

	/** Increase the flow on this leg only by the specified amount. */
	inline void AddFlow(uint f) { this->flow += f; }

	/** Get the flow on this leg. */
	inline uint GetFlow() const { return this->flow; }

	/** Get the number of "forked off" child legs of this one. */
	inline uint GetNumChildren() const { return this->num_children; }

	/**
	 * Detach this path from its parent.
	 */
	inline void Detach()
	{
		if (this->GetParent() != nullptr) {
			this->GetParent()->num_children--;
			this->SetParent(nullptr);
		}
	}

	uint AddFlow(uint f, LinkGraphJob &job, uint max_saturation);
	void Fork(Path *base, uint cap, int free_cap, uint dist);

	inline bool GetAnnosSetFlag() const { return HasBit(this->parent_storage, 0); }
	inline void SetAnnosSetFlag(bool flag) { SB(this->parent_storage, 0, 1, flag ? 1 : 0); }

protected:

	/**
	 * Some boundaries to clamp against in order to avoid integer overflows.
	 */
	enum PathCapacityBoundaries {
		PATH_CAP_MULTIPLIER = 16,
		PATH_CAP_MIN_FREE = (INT_MIN + 1) / PATH_CAP_MULTIPLIER,
		PATH_CAP_MAX_FREE = (INT_MAX - 1) / PATH_CAP_MULTIPLIER
	};

	uint distance;     ///< Sum(distance of all legs up to this one).
	uint capacity;     ///< This capacity is min(capacity) fom all edges.
	int free_capacity; ///< This capacity is min(edge.capacity - edge.flow) for the current run of Dijkstra.
	uint flow;         ///< Flow the current run of the mcf solver assigns.
	NodeID node;       ///< Link graph node this leg passes.
	NodeID origin;     ///< Link graph node this path originates from.
	uint num_children; ///< Number of child legs that have been forked from this path.

	uintptr_t parent_storage; ///< Parent leg of this one, flag in LSB of pointer

	/** Get the parent leg of this one. */
	inline void SetParent(Path *parent) { this->parent_storage = reinterpret_cast<uintptr_t>(parent) | (this->parent_storage & 1); }
};

inline bool IsLinkGraphCargoExpress(CargoID cargo)
{
	return IsCargoInClass(cargo, CC_PASSENGERS) ||
			IsCargoInClass(cargo, CC_MAIL) ||
			IsCargoInClass(cargo, CC_EXPRESS);
}

#endif /* LINKGRAPHJOB_H */
