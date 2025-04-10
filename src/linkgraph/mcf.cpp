/** @file mcf.cpp Definition of Multi-Commodity-Flow solver. */

#include "../stdafx.h"
#include "../core/math_func.hpp"
#include "mcf.h"
#include "../3rdparty/cpp-btree/btree_map.h"
#include <set>

#include "../safeguards.h"

typedef btree::btree_map<NodeID, Path *> PathViaMap;

/**
 * This is a priority queue item for Tannotation annotation values and IDs
 */
template <typename Tannotation>
class AnnoQueueItem {
public:
	typename Tannotation::AnnotationValueType cached_annotation;
	NodeID node_id;

	AnnoQueueItem(Tannotation *anno) : cached_annotation(anno->GetAnnotation()), node_id(anno->GetNode()) {}
};

/**
 * Distance-based annotation for use in the Dijkstra algorithm. This is close
 * to the original meaning of "annotation" in this context. Paths are rated
 * according to the sum of distances of their edges.
 */
class DistanceAnnotation final : public Path {
public:
	typedef uint AnnotationValueType;

	/**
	 * Constructor.
	 * @param n ID of node to be annotated.
	 * @param source If the node is the source of its path.
	 */
	DistanceAnnotation(NodeID n, bool source = false) : Path(n, source) {}

	bool IsBetter(const DistanceAnnotation *base, uint cap, int free_cap, uint dist) const;

	/**
	 * Return the actual value of the annotation, in this case the distance.
	 * @return Distance.
	 */
	inline uint GetAnnotation() const { return this->distance; }

	/**
	 * Update the cached annotation value
	 */
	inline void UpdateAnnotation() { }

	/**
	 * Compare two annotation values.
	 * @param x First annotation values.
	 * @param y Second annotation values.
	 * @return If x is better than y.
	 */
	static bool CompareAnnotations(uint x, uint y)
	{
		return x < y;
	}

	/**
	 * Compare two annotation queue items.
	 * @param x First annotation queue items.
	 * @param y Second annotation queue items.
	 * @return If x is better than y.
	 */
	static bool CompareQueueItems(const AnnoQueueItem<DistanceAnnotation> &x, const AnnoQueueItem<DistanceAnnotation> &y)
	{
		return std::tie(x.cached_annotation, x.node_id) < std::tie(y.cached_annotation, y.node_id);
	}
};
static_assert(std::is_trivially_destructible_v<DistanceAnnotation>);

/**
 * Capacity-based annotation for use in the Dijkstra algorithm. This annotation
 * rates paths according to the maximum capacity of their edges. The Dijkstra
 * algorithm still gives meaningful results like this as the capacity of a path
 * can only decrease or stay the same if you add more edges.
 */
class CapacityAnnotation final : public Path {
	int cached_annotation;

public:
	typedef int AnnotationValueType;


	/**
	 * Constructor.
	 * @param n ID of node to be annotated.
	 * @param source If the node is the source of its path.
	 */
	CapacityAnnotation(NodeID n, bool source = false) : Path(n, source) {}

	bool IsBetter(const CapacityAnnotation *base, uint cap, int free_cap, uint dist) const;

	/**
	 * Return the actual value of the annotation, in this case the capacity.
	 * @return Capacity.
	 */
	inline int GetAnnotation() const { return this->cached_annotation; }

	/**
	 * Update the cached annotation value
	 */
	inline void UpdateAnnotation()
	{
		this->cached_annotation = this->GetCapacityRatio();
	}

	/**
	 * Compare two annotation values.
	 * @param x First annotation values.
	 * @param y Second annotation values.
	 * @return If x is better than y.
	 */
	static bool CompareAnnotations(int x, int y)
	{
		/* Note that x and y are swapped, as a larger capacity is better */
		return y < x;
	}

	/**
	 * Compare two annotation queue items.
	 * @param x First annotation queue items.
	 * @param y Second annotation queue items.
	 * @return If x is better than y.
	 */
	static bool CompareQueueItems(const AnnoQueueItem<CapacityAnnotation> &x, const AnnoQueueItem<CapacityAnnotation> &y)
	{
		/* Note that x and y are swapped for cached_annotation, as a larger capacity is better */
		return std::tie(y.cached_annotation, x.node_id) < std::tie(x.cached_annotation, y.node_id);
	}
};
static_assert(std::is_trivially_destructible_v<CapacityAnnotation>);

/**
 * Iterator class for getting the edges in the order of their next_edge
 * members.
 */
class GraphEdgeIterator {
private:
	LinkGraphJob &job;    ///< Job being executed
	const Edge *i;        ///< Iterator pointing to current edge.
	const Edge *end;      ///< Iterator pointing beyond last edge.
	NodeID node;          ///< Source node
	const Edge *saved;    ///< Saved edge

public:

	/**
	 * Construct a GraphEdgeIterator.
	 * @param job Job to iterate on.
	 */
	GraphEdgeIterator(LinkGraphJob &job) : job(job),
		i(nullptr), end(nullptr), node(INVALID_NODE), saved(nullptr)
	{}

	/**
	 * Setup the node to start iterating at.
	 * @param node Node to start iterating at.
	 */
	void SetNode(NodeID, NodeID node)
	{
		Node node_anno = this->job[node];
		std::span<Edge> edges = node_anno.GetEdges();
		this->i = edges.data();
		this->end = edges.data() + edges.size();
		this->node = node;
	}

	/**
	 * Retrieve the ID of the node the next edge points to.
	 * @return Next edge's target node ID or INVALID_NODE.
	 */
	NodeID Next()
	{
		if (this->i == this->end) return INVALID_NODE;
		NodeID to = this->i->To();
		this->saved = this->i;
		++this->i;
		return to;
	}

	bool SavedEdge() const { return true; }

	const Edge &GetSavedEdge() { return *(this->saved); }
};

/**
 * Iterator class for getting edges from a FlowStatMap.
 */
class FlowEdgeIterator {
private:
	LinkGraphJob &job; ///< Link graph job we're working with.

	/** Lookup table for getting NodeIDs from StationIDs. */
	std::vector<NodeID> station_to_node;

	/** Current iterator in the shares map. */
	FlowStat::const_iterator it;

	/** End of the shares map. */
	FlowStat::const_iterator end;
public:

	/**
	 * Constructor.
	 * @param job Link graph job to work with.
	 */
	FlowEdgeIterator(LinkGraphJob &job) : job(job)
	{
		for (NodeID i = 0; i < job.Size(); ++i) {
			StationID st = job[i].Station();
			if (st >= this->station_to_node.size()) {
				this->station_to_node.resize(st + 1);
			}
			this->station_to_node[st] = i;
		}
	}

	/**
	 * Setup the node to retrieve edges from.
	 * @param source Root of the current path tree.
	 * @param node Current node to be checked for outgoing flows.
	 */
	void SetNode(NodeID source, NodeID node)
	{
		const FlowStatMap &flows = this->job[node].Flows();
		FlowStatMap::const_iterator it = flows.find(this->job[source].Station());
		if (it != flows.end()) {
			this->it = it->begin();
			this->end = it->end();
		} else {
			this->it = nullptr;
			this->end = nullptr;
		}
	}

	/**
	 * Get the next node for which a flow exists.
	 * @return ID of next node with flow.
	 */
	NodeID Next()
	{
		if (this->it == this->end) return INVALID_NODE;
		return this->station_to_node[(this->it++)->second];
	}

	bool SavedEdge() const { return false; }

	const Edge &GetSavedEdge() { NOT_REACHED(); }
};

/**
 * Determines if an extension to the given Path with the given parameters is
 * better than this path.
 * @param base Other path.
 * @param free_cap Capacity of the new edge to be added to base.
 * @param dist Distance of the new edge.
 * @return True if base + the new edge would be better than the path associated
 * with this annotation.
 */
bool DistanceAnnotation::IsBetter(const DistanceAnnotation *base, uint,
		int free_cap, uint dist) const
{
	/* If any of the paths is disconnected, the other one is better. If both
	 * are disconnected, this path is better.*/
	if (base->distance == UINT_MAX) {
		return false;
	} else if (this->distance == UINT_MAX) {
		return true;
	}

	if (free_cap > 0 && base->free_capacity > 0) {
		/* If both paths have capacity left, compare their distances.
		 * If the other path has capacity left and this one hasn't, the
		 * other one's better (thus, return true). */
		return this->free_capacity > 0 ? (base->distance + dist < this->distance) : true;
	} else {
		/* If the other path doesn't have capacity left, but this one has,
		 * the other one is worse (thus, return false).
		 * If both paths are out of capacity, do the regular distance
		 * comparison. */
		return this->free_capacity > 0 ? false : (base->distance + dist < this->distance);
	}
}

/**
 * Determines if an extension to the given Path with the given parameters is
 * better than this path.
 * @param base Other path.
 * @param free_cap Capacity of the new edge to be added to base.
 * @param dist Distance of the new edge.
 * @return True if base + the new edge would be better than the path associated
 * with this annotation.
 */
bool CapacityAnnotation::IsBetter(const CapacityAnnotation *base, uint cap,
		int free_cap, uint dist) const
{
	int min_cap = Path::GetCapacityRatio(std::min(base->free_capacity, free_cap), std::min(base->capacity, cap));
	int this_cap = this->GetAnnotation();
	if (min_cap == this_cap) {
		/* If the capacities are the same and the other path isn't disconnected
		 * choose the shorter path. */
		return base->distance == UINT_MAX ? false : (base->distance + dist < this->distance);
	} else {
		return min_cap > this_cap;
	}
}

template <class Tannotation>
struct MultiCommodityFlow::DijkstraState {
	std::vector<AnnoQueueItem<Tannotation>> anno_queue;
	std::vector<Tannotation> local_paths;

	DijkstraState(uint16_t job_size)
	{
		this->anno_queue.reserve(job_size);
		this->local_paths.reserve(job_size);
		static_assert(std::is_trivially_destructible_v<Tannotation>);
	}
};

/**
 * A slightly modified Dijkstra algorithm. Grades the paths not necessarily by
 * distance, but by the value Tannotation computes. It uses the max_saturation
 * setting to artificially decrease capacities.
 * @tparam Tannotation Annotation to be used.
 * @tparam Tedge_iterator Iterator to be used for getting outgoing edges.
 * @param source_node Node where the algorithm starts.
 * @param paths Container for the paths to be calculated.
 */
template <class Tannotation, class Tedge_iterator>
void MultiCommodityFlow::Dijkstra(NodeID source_node, PathVector &paths, DijkstraState<Tannotation> &state)
{
	const uint size = this->job.Size();

	std::vector<AnnoQueueItem<Tannotation>> &anno_queue = state.anno_queue;
	anno_queue.clear();
	auto anno_queue_comp = [](const AnnoQueueItem<Tannotation> &a, const AnnoQueueItem<Tannotation> &b) {
		/* Note that arguments are passed in opposite order, as std::push_heap sorts largest items to front, but the queue should return the lowest/best items */
		return Tannotation::CompareQueueItems(b, a);
	};

	auto update_queue_anno = [&](NodeID id, Tannotation *anno) -> bool {
		auto it = std::find_if(anno_queue.begin(), anno_queue.end(), [&](const AnnoQueueItem<Tannotation> &item) {
			return item.node_id == id;
		});
		if (it == anno_queue.end()) return false; // Couldn't fine node

		/* Update annotation value */
		if (Tannotation::CompareAnnotations(it->cached_annotation, anno->GetAnnotation())) {
			/* Annotation is worse, moving the queue item backwards in the heap is awkward, so just remove it via moving it to the front */
			it->cached_annotation = Tannotation::CompareAnnotations(0, 1) ?
					std::numeric_limits<typename Tannotation::AnnotationValueType>::min() : std::numeric_limits<typename Tannotation::AnnotationValueType>::max();
			it->node_id = 0;
			std::push_heap(anno_queue.begin(), it + 1, anno_queue_comp);
			assert(anno_queue.front().node_id == 0);
			std::pop_heap(anno_queue.begin(), anno_queue.end(), anno_queue_comp);
			anno_queue.pop_back();
			dbg_assert(std::is_heap(anno_queue.begin(), anno_queue.end(), anno_queue_comp));
			return false;
		} else {
			/* Annotation is better or equal, queue item never needs to be moved away from front of heap */
			it->cached_annotation = anno->GetAnnotation();
			std::push_heap(anno_queue.begin(), it + 1, anno_queue_comp);
			dbg_assert(std::is_heap(anno_queue.begin(), anno_queue.end(), anno_queue_comp));
			return true;
		}
	};

	Tedge_iterator iter(this->job);

	state.local_paths.clear();
	for (NodeID node = 0; node < size; ++node) {
		Tannotation *anno = &state.local_paths.emplace_back(node, node == source_node);
		anno->UpdateAnnotation();
		if (node == source_node) {
			anno_queue.emplace_back(anno);
			std::push_heap(anno_queue.begin(), anno_queue.end(), anno_queue_comp);
			anno->SetAnnosSetFlag(true);
		}
	}
	Tannotation *local_paths = state.local_paths.data();
	while (!anno_queue.empty()) {
		NodeID from = anno_queue.front().node_id;
		std::pop_heap(anno_queue.begin(), anno_queue.end(), anno_queue_comp);
		anno_queue.pop_back();
		Tannotation *source = local_paths + from;

		iter.SetNode(source_node, from);
		for (NodeID to = iter.Next(); to != INVALID_NODE; to = iter.Next()) {
			if (to == from) continue; // Not a real edge but a consumption sign.
			const Edge &edge = iter.SavedEdge() ? iter.GetSavedEdge() : this->job[from].GetEdgeTo(to);
			uint capacity = edge.Capacity();
			if (this->max_saturation != UINT_MAX) {
				capacity *= this->max_saturation;
				capacity /= 100;
				if (capacity == 0) capacity = 1;
			}

			Tannotation *dest = local_paths + to;
			if (dest->IsBetter(source, capacity, capacity - edge.Flow(), edge.DistanceAnno())) {
				dest->Fork(source, capacity, capacity - edge.Flow(), edge.DistanceAnno());
				dest->UpdateAnnotation();
				if (!dest->GetAnnosSetFlag() || !update_queue_anno(to, dest)) {
					/* Add new item to queue */
					anno_queue.emplace_back(dest);
					std::push_heap(anno_queue.begin(), anno_queue.end(), anno_queue_comp);
					dest->SetAnnosSetFlag(true);
				}
			}
		}
	}

	/* Copy path nodes to path_allocator, fill output vector */
	paths.clear();
	paths.reserve(size);
	this->job.path_allocator.SetParameters(sizeof(Tannotation), (8192 - 32) / sizeof(Tannotation));
	for (NodeID node = 0; node < size; ++node) {
		/* Allocate and copy nodes */
		paths.push_back(new (this->job.path_allocator.Allocate()) Tannotation(local_paths[node]));
	}
	for (NodeID node = 0; node < size; ++node) {
		/* Fixup parent pointers */
		Path *path = paths[node];
		if (path->GetParent() != nullptr) {
			path->SetParent(paths[path->GetParent()->GetNode()]);
		}
	}
}

/**
 * Clean up paths that lead nowhere and the root path.
 * @param source_id ID of the root node.
 * @param paths Paths to be cleaned up.
 */
void MultiCommodityFlow::CleanupPaths(NodeID source_id, PathVector &paths)
{
	Path *source = paths[source_id];
	paths[source_id] = nullptr;
	for (PathVector::iterator i = paths.begin(); i != paths.end(); ++i) {
		Path *path = *i;
		if (path == nullptr) continue;
		if (path->GetParent() == source) path->Detach();
		while (path != source && path != nullptr && path->GetFlow() == 0) {
			Path *parent = path->GetParent();
			path->Detach();
			if (path->GetNumChildren() == 0) {
				paths[path->GetNode()] = nullptr;
				path->~Path();
				this->job.path_allocator.Free(path);
			}
			path = parent;
		}
	}
	source->~Path();
	this->job.path_allocator.Free(source);
	paths.clear();
}

/**
 * Push flow along a path and update the unsatisfied_demand of the associated
 * edge.
 * @param anno Distance annotation whose ends the path connects.
 * @param path End of the path the flow should be pushed on.
 * @param min_step_size Minimum flow size.
 * @param accuracy Accuracy of the calculation.
 * @param max_saturation If < UINT_MAX only push flow up to the given
 *                       saturation, otherwise the path can be "overloaded".
 */
uint MultiCommodityFlow::PushFlow(DemandAnnotation &anno, Path *path, uint min_step_size, uint accuracy,
		uint max_saturation)
{
	dbg_assert(anno.unsatisfied_demand > 0);
	uint flow = std::min(std::max(anno.demand / accuracy, min_step_size), anno.unsatisfied_demand);
	flow = path->AddFlow(flow, this->job, max_saturation);
	anno.unsatisfied_demand -= flow;
	return flow;
}

/**
 * Find the flow along a cycle including cycle_begin in path.
 * @param path Set of paths that form the cycle.
 * @param cycle_begin Path to start at.
 * @return Flow along the cycle.
 */
uint MCF1stPass::FindCycleFlow(const PathVector &path, const Path *cycle_begin)
{
	uint flow = UINT_MAX;
	const Path *cycle_end = cycle_begin;
	do {
		flow = std::min(flow, cycle_begin->GetFlow());
		cycle_begin = path[cycle_begin->GetNode()];
	} while (cycle_begin != cycle_end);
	return flow;
}

/**
 * Eliminate a cycle of the given flow in the given set of paths.
 * @param path Set of paths containing the cycle.
 * @param cycle_begin Part of the cycle to start at.
 * @param flow Flow along the cycle.
 */
void MCF1stPass::EliminateCycle(PathVector &path, Path *cycle_begin, uint flow)
{
	Path *cycle_end = cycle_begin;
	do {
		NodeID prev = cycle_begin->GetNode();
		cycle_begin->ReduceFlow(flow);
		if (cycle_begin->GetFlow() == 0) {
			PathList &node_paths = this->job[cycle_begin->GetParent()->GetNode()].Paths();
			for (PathList::iterator i = node_paths.begin(); i != node_paths.end(); ++i) {
				if (*i == cycle_begin) {
					*i = nullptr;
					break;
				}
			}
		}
		cycle_begin = path[prev];
		Edge &edge = this->job[prev].GetEdgeTo(cycle_begin->GetNode());
		edge.RemoveFlow(flow);
	} while (cycle_begin != cycle_end);
}

/**
 * Eliminate cycles for origin_id in the graph. Start searching at next_id and
 * work recursively. Also "summarize" paths: Add up the flows along parallel
 * paths in one.
 * @param path Paths checked in parent calls to this method.
 * @param origin_id Origin of the paths to be checked.
 * @param next_id Next node to be checked.
 * @return If any cycles have been found and eliminated.
 */
bool MCF1stPass::EliminateCycles(PathVector &path, NodeID origin_id, NodeID next_id)
{
	Path *at_next_pos = path[next_id];

	/* this node has already been searched */
	if (at_next_pos == Path::invalid_path) return false;

	if (at_next_pos == nullptr) {
		/* Summarize paths; add up the paths with the same source and next hop
		 * in one path each. */
		PathList &paths = this->job[next_id].Paths();
		PathViaMap next_hops;
		uint holes = 0;
		for (PathList::iterator i = paths.begin(); i != paths.end();) {
			Path *new_child = *i;
			if (new_child != nullptr) {
				if (new_child->GetOrigin() == origin_id) {
					PathViaMap::iterator via_it = next_hops.find(new_child->GetNode());
					if (via_it == next_hops.end()) {
						next_hops[new_child->GetNode()] = new_child;
					} else {
						uint new_flow = new_child->GetFlow();
						Path *child = via_it->second;
						child->AddFlow(new_flow);
						new_child->ReduceFlow(new_flow);

						*i = nullptr;
						holes++;
					}
				}
			} else {
				holes++;
			}
			++i;
		}
		if (holes > paths.size() / 4) {
			/* remove any holes */
			paths.erase(std::remove(paths.begin(), paths.end(), nullptr), paths.end());
		}

		bool found = false;
		/* Search the next hops for nodes we have already visited */
		for (PathViaMap::iterator via_it = next_hops.begin();
				via_it != next_hops.end(); ++via_it) {
			Path *child = via_it->second;
			if (child->GetFlow() > 0) {
				/* Push one child into the path vector and search this child's
				 * children. */
				path[next_id] = child;
				found = this->EliminateCycles(path, origin_id, child->GetNode()) || found;
			}
		}
		/* All paths departing from this node have been searched. Mark as
		 * resolved if no cycles found. If cycles were found further cycles
		 * could be found in this branch, thus it has to be searched again next
		 * time we spot it.
		 */
		path[next_id] = found ? nullptr : Path::invalid_path;
		return found;
	}

	/* This node has already been visited => we have a cycle.
	 * Backtrack to find the exact flow. */
	uint flow = this->FindCycleFlow(path, at_next_pos);
	if (flow > 0) {
		this->EliminateCycle(path, at_next_pos, flow);
		return true;
	}

	return false;
}

/**
 * Eliminate all cycles in the graph. Check paths starting at each node for
 * potential cycles.
 * @return If any cycles have been found and eliminated.
 */
bool MCF1stPass::EliminateCycles()
{
	bool cycles_found = false;
	uint16_t size = this->job.Size();
	PathVector path(size, nullptr);
	for (NodeID node = 0; node < size; ++node) {
		/* Starting at each node in the graph find all cycles involving this
		 * node. */
		std::fill(path.begin(), path.end(), (Path *)nullptr);
		cycles_found |= this->EliminateCycles(path, node, node);
	}
	return cycles_found;
}

/**
 * Run the first pass of the MCF calculation.
 * @param job Link graph job to calculate.
 */
MCF1stPass::MCF1stPass(LinkGraphJob &job) : MultiCommodityFlow(job)
{
	const uint16_t size = job.Size();
	PathVector paths;
	DijkstraState<DistanceAnnotation> state(size);
	uint accuracy = job.Settings().accuracy;
	bool more_loops;
	std::vector<bool> finished_sources(size);

	uint min_step_size = 1;
	const uint adjust_threshold = 50;
	if (size >= adjust_threshold) {
		uint64_t total_demand = 0;
		uint demand_count = 0;
		for (NodeID source = 0; source < size; ++source) {
			const Node &node = job[source];
			for (const DemandAnnotation &anno : node.GetDemandAnnotations()) {
				if (anno.unsatisfied_demand > 0) {
					total_demand += anno.unsatisfied_demand;
					demand_count++;
				}
			}
		}
		if (demand_count == 0) return;
		min_step_size = std::max<uint>(min_step_size, (total_demand * (1 + FindLastBit(size / adjust_threshold))) / (size * accuracy));
		accuracy = Clamp(IntSqrt((4 * accuracy * accuracy * size) / demand_count), CeilDiv(accuracy, 4), accuracy);
	}

	do {
		more_loops = false;
		for (NodeID source = 0; source < size; ++source) {
			if (finished_sources[source]) continue;

			/* First saturate the shortest paths. */
			this->Dijkstra<DistanceAnnotation, GraphEdgeIterator>(source, paths, state);

			bool source_demand_left = false;
			for (DemandAnnotation &anno : job[source].GetDemandAnnotations()) {
				NodeID dest = anno.dest;
				if (anno.unsatisfied_demand > 0) {
					Path *path = paths[dest];
					assert(path != nullptr);
					/* Generally only allow paths that don't exceed the
					 * available capacity. But if no demand has been assigned
					 * yet, make an exception and allow any valid path *once*. */
					if (path->GetFreeCapacity() > 0 && this->PushFlow(anno, path,
							min_step_size, accuracy, this->max_saturation) > 0) {
						/* If a path has been found there is a chance we can
						 * find more. */
						more_loops = more_loops || (anno.unsatisfied_demand > 0);
					} else if (anno.unsatisfied_demand == anno.demand &&
							path->GetFreeCapacity() > INT_MIN) {
						this->PushFlow(anno, path, min_step_size, accuracy, UINT_MAX);
					}
					if (anno.unsatisfied_demand > 0) source_demand_left = true;
				}
			}
			if (!source_demand_left) finished_sources[source] = true;
			this->CleanupPaths(source, paths);
		}
	} while ((more_loops || this->EliminateCycles()) && !job.IsJobAborted());
}

/**
 * Run the second pass of the MCF calculation which assigns all remaining
 * demands to existing paths.
 * @param job Link graph job to calculate.
 */
MCF2ndPass::MCF2ndPass(LinkGraphJob &job) : MultiCommodityFlow(job)
{
	this->max_saturation = UINT_MAX; // disable artificial cap on saturation
	const uint16_t size = job.Size();
	PathVector paths;
	DijkstraState<CapacityAnnotation> state(size);
	uint accuracy = job.Settings().accuracy;
	bool demand_left = true;
	std::vector<bool> finished_sources(size);
	while (demand_left && !job.IsJobAborted()) {
		demand_left = false;
		for (NodeID source = 0; source < size; ++source) {
			if (finished_sources[source]) continue;

			this->Dijkstra<CapacityAnnotation, FlowEdgeIterator>(source, paths, state);

			bool source_demand_left = false;
			for (DemandAnnotation &anno : this->job[source].GetDemandAnnotations()) {
				if (anno.unsatisfied_demand == 0) continue;
				Path *path = paths[anno.dest];
				if (path->GetFreeCapacity() > INT_MIN) {
					this->PushFlow(anno, path, 1, accuracy, UINT_MAX);
					if (anno.unsatisfied_demand > 0) {
						demand_left = true;
						source_demand_left = true;
					}
				}
			}
			if (!source_demand_left) finished_sources[source] = true;
			this->CleanupPaths(source, paths);
		}
	}
}
