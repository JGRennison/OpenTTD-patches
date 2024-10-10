/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraphjob.cpp Definition of link graph job classes used for cargo distribution. */

#include "../stdafx.h"
#include "../core/pool_func.hpp"
#include "../window_func.h"
#include "linkgraphjob.h"
#include "linkgraphschedule.h"

#include "../safeguards.h"

/* Initialize the link-graph-job-pool */
LinkGraphJobPool _link_graph_job_pool("LinkGraphJob");
INSTANTIATE_POOL_METHODS(LinkGraphJob)

/**
 * Static instance of an invalid path.
 * Note: This instance is created on task start.
 *       Lazy creation on first usage results in a data race between the CDist threads.
 */
/* static */ Path *Path::invalid_path = new Path(INVALID_NODE, true);

static ScaledTickCounter GetLinkGraphJobJoinTick(uint duration_multiplier)
{
	ScaledTickCounter ticks = (_settings_game.linkgraph.recalc_time * DAY_TICKS * duration_multiplier) / SECONDS_PER_DAY;
	return ticks + _scaled_tick_counter;
}

/**
 * Create a link graph job from a link graph. The link graph will be copied so
 * that the calculations don't interfere with the normal operations on the
 * original. The job is immediately started.
 * @param orig Original LinkGraph to be copied.
 */
LinkGraphJob::LinkGraphJob(const LinkGraph &orig, uint duration_multiplier) :
		/* Copying the link graph here also copies its index member.
		 * This is on purpose. */
		link_graph(orig),
		settings(_settings_game.linkgraph),
		join_tick(GetLinkGraphJobJoinTick(duration_multiplier)),
		start_tick(_scaled_tick_counter),
		day_length_factor(DayLengthFactor()),
		job_completed(false),
		job_aborted(false)
{
}

/**
 * Erase all flows originating at a specific node.
 * @param from Node to erase flows for.
 */
void LinkGraphJob::EraseFlows(NodeID from)
{
	for (NodeID node_id = 0; node_id < this->Size(); ++node_id) {
		(*this)[node_id].Flows().erase(from);
	}
}

void LinkGraphJob::SetJobGroup(std::shared_ptr<LinkGraphJobGroup> group)
{
	this->group = std::move(group);
}

/**
 * Join the calling thread with this job's thread if threading is enabled.
 */
void LinkGraphJob::JoinThread()
{
	if (this->group != nullptr) {
		this->group->JoinThread();
		this->group.reset();
	}
}

/**
 * Join the link graph job thread, if not already joined.
 */
LinkGraphJob::~LinkGraphJob()
{
	this->JoinThread();
}

/**
 * Join the link graph job thread, then merge/apply it.
 */
void LinkGraphJob::FinaliseJob()
{
	this->JoinThread();

	/* If the job has been aborted, the job state is invalid.
	 * This should never be reached, as once the job has been marked as aborted
	 * the only valid job operation is to clear the LinkGraphJob pool. */
	assert(!this->IsJobAborted());

	/* Link graph has been merged into another one. */
	if (!LinkGraph::IsValidID(this->link_graph.index)) return;

	uint16_t size = this->Size();
	for (NodeID node_id = 0; node_id < size; ++node_id) {
		Node from = (*this)[node_id];

		/* The station can have been deleted. Remove all flows originating from it then. */
		Station *st = Station::GetIfValid(from.Station());
		if (st == nullptr) {
			this->EraseFlows(node_id);
			continue;
		}

		/* Link graph merging and station deletion may change around IDs. Make
		 * sure that everything is still consistent or ignore it otherwise. */
		GoodsEntry &ge = st->goods[this->Cargo()];
		if (ge.link_graph != this->link_graph.index || ge.node != node_id) {
			this->EraseFlows(node_id);
			continue;
		}

		LinkGraph *lg = LinkGraph::Get(ge.link_graph);
		FlowStatMap &flows = from.Flows();
		FlowStatMap &geflows = ge.CreateData().flows;

		for (Edge &edge : from.GetEdges()) {
			if (edge.Flow() == 0) continue;
			StationID to = (*this)[edge.To()].Station();
			Station *st2 = Station::GetIfValid(to);
			LinkGraph::ConstEdge lg_edge = lg->GetConstEdge(edge.From(), edge.To());
			if (st2 == nullptr || st2->goods[this->Cargo()].link_graph != this->link_graph.index ||
					st2->goods[this->Cargo()].node != edge.To() ||
					lg_edge.LastUpdate() == EconTime::INVALID_DATE) {
				/* Edge has been removed. Delete flows. */
				StationIDStack erased = flows.DeleteFlows(to);
				/* Delete old flows for source stations which have been deleted
				 * from the new flows. This avoids flow cycles between old and
				 * new flows. */
				while (!erased.IsEmpty()) geflows.erase(erased.Pop());
			} else if (lg_edge.LastUnrestrictedUpdate() == EconTime::INVALID_DATE) {
				/* Edge is fully restricted. */
				flows.RestrictFlows(to);
			}
		}

		/* Swap shares and invalidate ones that are completely deleted. Don't
		 * really delete them as we could then end up with unroutable cargo
		 * somewhere. Do delete them and also reroute relevant cargo if
		 * automatic distribution has been turned off for that cargo. */
		for (FlowStatMap::iterator it(geflows.begin()); it != geflows.end();) {
			FlowStatMap::iterator new_it = flows.find(it->GetOrigin());
			if (new_it == flows.end()) {
				if (_settings_game.linkgraph.GetDistributionType(this->Cargo()) != DT_MANUAL) {
					if (it->Invalidate()) {
						NodeID origin = it->GetOrigin();
						FlowStat shares(INVALID_STATION, INVALID_STATION, 1);
						it->SwapShares(shares);
						it = geflows.erase(it);
						for (FlowStat::const_iterator shares_it(shares.begin());
								shares_it != shares.end(); ++shares_it) {
							RerouteCargoFromSource(st, this->Cargo(), origin, shares_it->second, st->index);
						}
					} else {
						++it;
					}
				} else {
					FlowStat shares(INVALID_STATION, INVALID_STATION, 1);
					it->SwapShares(shares);
					it = geflows.erase(it);
					for (FlowStat::const_iterator shares_it(shares.begin());
							shares_it != shares.end(); ++shares_it) {
						RerouteCargo(st, this->Cargo(), shares_it->second, st->index);
					}
				}
			} else {
				it->SwapShares(*new_it);
				flows.erase(new_it);
				++it;
			}
		}
		for (FlowStatMap::iterator it(flows.begin()); it != flows.end(); ++it) {
			geflows.insert(std::move(*it));
		}
		geflows.SortStorage();
		ge.RemoveDataIfUnused();
		InvalidateWindowData(WC_STATION_VIEW, st->index, this->Cargo());
	}
}

/**
 * Initialize the link graph job: Resize nodes and edges and populate them.
 * This is done after the constructor so that we can do it in the calculation
 * thread without delaying the main game.
 */
void LinkGraphJob::Init()
{
	uint size = this->Size();
	this->nodes.resize(size);
	for (uint i = 0; i < size; ++i) {
		this->nodes[i].Init(this->link_graph[i].Supply());
	}

	/* Prioritize the fastest route for passengers, mail and express cargo,
	 * and the shortest route for other classes of cargo.
	 * In-between stops are punished with a 1 tile or 1 day penalty. */
	const bool express = IsLinkGraphCargoExpress(this->Cargo());
	const uint16_t aircraft_link_scale = this->Settings().aircraft_link_scale;

	size_t edge_count = 0;
	for (auto &it : this->link_graph.GetEdges()) {
		if (it.first.first == it.first.second) continue;
		edge_count++;
	}

	this->edges.resize(edge_count);
	size_t start_idx = 0;
	size_t idx = 0;
	NodeID last_from = INVALID_NODE;
	auto flush = [&]() {
		if (last_from == INVALID_NODE) return;
		this->nodes[last_from].edges = { this->edges.data() + start_idx, idx - start_idx };
	};
	for (auto &it : this->link_graph.GetEdges()) {
		if (it.first.first == it.first.second) continue;

		if (it.first.first != last_from) {
			flush();
			last_from = it.first.first;
			start_idx = idx;
		}

		LinkGraph::ConstEdge edge(it.second);

		auto calculate_distance = [&]() {
			return DistanceMaxPlusManhattan((*this)[it.first.first].XY(), (*this)[it.first.second].XY()) + 1;
		};

		uint distance_anno;
		if (express) {
			/* Compute a default travel time from the distance and an average speed of 1 tile/day. */
			distance_anno = (edge.TravelTime() != 0) ? edge.TravelTime() + DAY_TICKS : calculate_distance() * DAY_TICKS;
		} else {
			distance_anno = calculate_distance();
		}

		if (edge.LastAircraftUpdate() != EconTime::INVALID_DATE && aircraft_link_scale > 100) {
			distance_anno *= aircraft_link_scale;
			distance_anno /= 100;
		}

		this->edges[idx].InitEdge(it.first.first, it.first.second, edge.Capacity(), distance_anno);
		idx++;
	}
	flush();
}

/**
 * Initialize a Linkgraph job node. The underlying memory is expected to be
 * freshly allocated, without any constructors having been called.
 * @param supply Initial undelivered supply.
 */
void LinkGraphJob::NodeAnnotation::Init(uint supply)
{
	this->undelivered_supply = supply;
	this->received_demand = 0;
}

/**
 * Add this path as a new child to the given base path, thus making this path
 * a "fork" of the base path.
 * @param base Path to fork from.
 * @param cap Maximum capacity of the new leg.
 * @param free_cap Remaining free capacity of the new leg.
 * @param dist Distance of the new leg.
 */
void Path::Fork(Path *base, uint cap, int free_cap, uint dist)
{
	this->capacity = std::min(base->capacity, cap);
	this->free_capacity = std::min(base->free_capacity, free_cap);
	this->distance = base->distance + dist;
	assert(this->distance > 0);
	if (this->GetParent() != base) {
		this->Detach();
		this->SetParent(base);
		base->num_children++;
	}
	this->origin = base->origin;
}

/**
 * Push some flow along a path and register the path in the nodes it passes if
 * successful.
 * @param new_flow Amount of flow to push.
 * @param job Link graph job this node belongs to.
 * @param max_saturation Maximum saturation of edges.
 * @return Amount of flow actually pushed.
 */
uint Path::AddFlow(uint new_flow, LinkGraphJob &job, uint max_saturation)
{
	if (this->GetParent() != nullptr) {
		LinkGraphJob::Edge &edge = job[this->GetParent()->node].GetEdgeTo(this->node);
		if (max_saturation != UINT_MAX) {
			uint usable_cap = edge.Capacity() * max_saturation / 100;
			if (usable_cap > edge.Flow()) {
				new_flow = std::min(new_flow, usable_cap - edge.Flow());
			} else {
				return 0;
			}
		}
		new_flow = this->GetParent()->AddFlow(new_flow, job, max_saturation);
		if (this->flow == 0 && new_flow > 0) {
			job[this->GetParent()->node].Paths().push_back(this);
		}
		edge.AddFlow(new_flow);
	}
	this->flow += new_flow;
	return new_flow;
}

/**
 * Create a leg of a path in the link graph.
 * @param n Id of the link graph node this path passes.
 * @param source If true, this is the first leg of the path.
 */
Path::Path(NodeID n, bool source) :
	distance(source ? 0 : UINT_MAX),
	capacity(source ? UINT_MAX : 0),
	free_capacity(source ? INT_MAX : INT_MIN),
	flow(0), node(n), origin(source ? n : INVALID_NODE),
	num_children(0), parent_storage(0)
{}

