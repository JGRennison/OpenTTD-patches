/** @file demands.cpp Definition of demand calculating link graph handler. */

#include "../stdafx.h"
#include "demands.h"
#include "../core/ring_buffer_queue.hpp"
#include <algorithm>
#include <tuple>

#include "../safeguards.h"

typedef ring_buffer_queue<NodeID> NodeList;

/**
 * Scale various things according to symmetric/asymmetric distribution.
 */
class Scaler {
public:
	void SetDemands(LinkGraphJob &job, NodeID from, NodeID to, uint demand_forw);
};

/**
 * Scaler for symmetric distribution.
 */
class SymmetricScaler : public Scaler {
public:
	/**
	 * Constructor.
	 * @param mod_size Size modifier to be used. Determines how much demands
	 *                 increase with the supply of the remote station.
	 */
	inline SymmetricScaler(uint mod_size) : mod_size(mod_size), supply_sum(0),
		demand_per_node(0)
	{}

	/**
	 * Count a node's supply into the sum of supplies.
	 * @param node Node.
	 */
	inline void AddNode(const Node &node)
	{
		this->supply_sum += node.Supply();
	}

	/**
	 * Calculate the mean demand per node using the sum of supplies.
	 * @param num_demands Number of accepting nodes.
	 */
	inline void SetDemandPerNode(uint num_demands)
	{
		this->demand_per_node = std::max(this->supply_sum / num_demands, 1U);
	}

	/**
	 * Get the effective supply of one node towards another one. In symmetric
	 * distribution the supply of the other node is weighed in.
	 * @param from The supplying node.
	 * @param to The receiving node.
	 * @return Effective supply.
	 */
	inline uint EffectiveSupply(const Node &from, const Node &to)
	{
		return std::max(from.Supply() * std::max(1U, to.Supply()) * this->mod_size / 100 / this->demand_per_node, 1U);
	}

	/**
	 * Check if there is any acceptance left for this node. In symmetric distribution
	 * nodes only accept anything if they also supply something. So if
	 * undelivered_supply == 0 at the node there isn't any demand left either.
	 * @param to Node to be checked.
	 * @return If demand is left.
	 */
	inline bool HasDemandLeft(const Node &to)
	{
		return (to.Supply() == 0 || to.UndeliveredSupply() > 0) && to.Demand() > 0;
	}

	void SetDemands(LinkGraphJob &job, NodeID from, NodeID to, uint demand_forw);

private:
	uint mod_size;        ///< Size modifier. Determines how much demands increase with the supply of the remote station.
	uint supply_sum;      ///< Sum of all supplies in the component.
	uint demand_per_node; ///< Mean demand associated with each node.
};

/**
 * A scaler for asymmetric distribution.
 */
class AsymmetricScaler : public Scaler {
public:
	/**
	 * Nothing to do here.
	 * @param unused.
	 */
	inline void AddNode(const Node &)
	{
	}

	/**
	 * Nothing to do here.
	 * @param unused.
	 */
	inline void SetDemandPerNode(uint)
	{
	}

	/**
	 * Nothing to do here.
	 * @param unused.
	 * @param unused.
	 */
	inline void AdjustDemandNodes(LinkGraphJob &, const std::vector<NodeID> &)
	{
	}

	/**
	 * Get the effective supply of one node towards another one.
	 * @param from The supplying node.
	 * @param unused.
	 */
	inline uint EffectiveSupply(const Node &from, const Node &)
	{
		return from.Supply();
	}

	/**
	 * Check if there is any acceptance left for this node. In asymmetric distribution
	 * nodes always accept as long as their demand > 0.
	 * @param to The node to be checked.
	 */
	inline bool HasDemandLeft(const Node &to) { return to.Demand() > 0; }
};

/**
 * A scaler for asymmetric distribution (equal supply).
 */
class AsymmetricScalerEq : public Scaler {
public:
	/**
	 * Count a node's supply into the sum of supplies.
	 * @param node Node.
	 */
	inline void AddNode(const Node &node)
	{
		this->supply_sum += node.Supply();
	}

	/**
	 * Calculate the mean demand per node using the sum of supplies.
	 * @param num_demands Number of accepting nodes.
	 */
	inline void SetDemandPerNode(uint num_demands)
	{
		this->demand_per_node = CeilDiv(this->supply_sum, num_demands);
		this->missing_supply = (this->demand_per_node * num_demands) - this->supply_sum;
	}

	/**
	 * Adjust demand nodes after setting demand per node.
	 * @param job The link graph job.
	 * @param demands List of demand nodes to adjust.
	 */
	inline void AdjustDemandNodes(LinkGraphJob &job, const std::vector<NodeID> &demands)
	{
		const uint count = std::min<uint>((uint)demands.size(), this->missing_supply);
		this->missing_supply = 0;
		for (uint i = 0; i < count; i++) {
			job[demands[i]].ReceiveDemand(1);
		}
	}

	/**
	 * Get the effective supply of one node towards another one. In symmetric
	 * distribution the supply of the other node is weighed in.
	 * @param from The supplying node.
	 * @param to The receiving node.
	 * @return Effective supply.
	 */
	inline uint EffectiveSupply(const Node &from, const Node &to)
	{
		return std::max<int>(std::min<int>(from.Supply(), ((int) this->demand_per_node) - ((int) to.ReceivedDemand())), 1);
	}

	/**
	 * Check if there is any acceptance left for this node. In asymmetric (equal) distribution
	 * nodes accept as long as their demand > 0 and received_demand < demand_per_node.
	 * @param to The node to be checked.
	 */
	inline bool HasDemandLeft(const Node &to)
	{
		return to.Demand() > 0 && to.ReceivedDemand() < this->demand_per_node;
	}

	void SetDemands(LinkGraphJob &job, NodeID from, NodeID to, uint demand_forw);

private:
	uint supply_sum;      ///< Sum of all supplies in the component.
	uint demand_per_node; ///< Mean demand associated with each node.
	uint missing_supply;  ///< Suppply/demand adjustment for in AdjustDemandNodes.
};

/**
 * Set the demands between two nodes using the given base demand. In symmetric mode
 * this sets demands in both directions.
 * @param job The link graph job.
 * @param from_id The supplying node.
 * @param to_id The receiving node.
 * @param demand_forw Demand calculated for the "forward" direction.
 */
void SymmetricScaler::SetDemands(LinkGraphJob &job, NodeID from_id, NodeID to_id, uint demand_forw)
{
	if (job[from_id].Demand() > 0) {
		uint demand_back = demand_forw * this->mod_size / 100;
		uint undelivered = job[to_id].UndeliveredSupply();
		if (demand_back > undelivered) {
			demand_back = undelivered;
			demand_forw = std::max(1U, demand_back * 100 / this->mod_size);
		}
		this->Scaler::SetDemands(job, to_id, from_id, demand_back);
	}

	this->Scaler::SetDemands(job, from_id, to_id, demand_forw);
}

/**
 * Set the demands between two nodes using the given base demand.
 * @param job The link graph job.
 * @param from_id The supplying node.
 * @param to_id The receiving node.
 * @param demand_forw Demand calculated for the "forward" direction.
 */
void AsymmetricScalerEq::SetDemands(LinkGraphJob &job, NodeID from_id, NodeID to_id, uint demand_forw)
{
	this->Scaler::SetDemands(job, from_id, to_id, demand_forw);
	job[to_id].ReceiveDemand(demand_forw);
}

/**
 * Set the demands between two nodes using the given base demand. In asymmetric mode
 * this only sets demand in the "forward" direction.
 * @param job The link graph job.
 * @param from_id The supplying node.
 * @param to_id The receiving node.
 * @param demand_forw Demand calculated for the "forward" direction.
 */
inline void Scaler::SetDemands(LinkGraphJob &job, NodeID from_id, NodeID to_id, uint demand_forw)
{
	if (demand_forw == 0) return;

	job[from_id].DeliverSupply(demand_forw);

	uint &demand = job.demand_matrix[(from_id * job.Size()) + to_id];
	if (demand == 0) job.demand_matrix_count++;
	demand += demand_forw;
}

/**
 * Do the actual demand calculation, called from constructor.
 * @param job Job to calculate the demands for.
 * @param reachable_nodes Bitmap of reachable nodes.
 * @tparam Tscaler Scaler to be used for scaling demands.
 */
template<class Tscaler>
void DemandCalculator::CalcDemand(LinkGraphJob &job, const std::vector<bool> &reachable_nodes, Tscaler scaler)
{
	NodeList supplies;
	NodeList demands;
	uint num_supplies = 0;
	uint num_demands = 0;

	for (NodeID node = 0; node < job.Size(); node++) {
		if (!reachable_nodes[node]) continue;
		scaler.AddNode(job[node]);
		if (job[node].Supply() > 0) {
			supplies.push(node);
			num_supplies++;
		}
		if (job[node].Demand() > 0) {
			demands.push(node);
			num_demands++;
		}
	}

	if (num_supplies == 0 || num_demands == 0) return;

	/* Mean acceptance attributed to each node. If the distribution is
	 * symmetric this is relative to remote supply, otherwise it is
	 * relative to remote demand. */
	scaler.SetDemandPerNode(num_demands);

	uint chance = 0;

	while (!supplies.empty() && !demands.empty()) {
		NodeID from_id = supplies.front();
		supplies.pop();

		for (uint i = 0; i < num_demands; ++i) {
			assert(!demands.empty());
			NodeID to_id = demands.front();
			demands.pop();
			if (from_id == to_id) {
				/* Only one node with supply and demand left */
				if (demands.empty() && supplies.empty()) return;

				demands.push(to_id);
				continue;
			}

			int32_t supply = scaler.EffectiveSupply(job[from_id], job[to_id]);
			assert(supply > 0);

			/* Scale the distance by mod_dist around max_distance */
			int32_t distance = this->max_distance - (this->max_distance -
					(int32_t)DistanceMaxPlusManhattan(job[from_id].XY(), job[to_id].XY())) *
					this->mod_dist / 100;

			/* Scale the accuracy by distance around accuracy / 2 */
			int32_t divisor = this->accuracy * (this->mod_dist - 50) / 100 +
					this->accuracy * distance / this->max_distance + 1;

			assert(divisor > 0);

			uint demand_forw = 0;
			if (divisor <= supply) {
				/* At first only distribute demand if
				 * effective supply / accuracy divisor >= 1
				 * Others are too small or too far away to be considered. */
				demand_forw = supply / divisor;
			} else if (++chance > this->accuracy * num_demands * num_supplies) {
				/* After some trying, if there is still supply left, distribute
				 * demand also to other nodes. */
				demand_forw = 1;
			}

			demand_forw = std::min(demand_forw, job[from_id].UndeliveredSupply());

			scaler.SetDemands(job, from_id, to_id, demand_forw);

			if (scaler.HasDemandLeft(job[to_id])) {
				demands.push(to_id);
			} else {
				num_demands--;
			}

			if (job[from_id].UndeliveredSupply() == 0) break;
		}

		if (job[from_id].UndeliveredSupply() != 0) {
			supplies.push(from_id);
		} else {
			num_supplies--;
		}
	}
}

/**
 * Do the actual demand calculation, called from constructor.
 * @param job Job to calculate the demands for.
 * @param reachable_nodes Bitmap of reachable nodes.
 * @tparam Tscaler Scaler to be used for scaling demands.
 */
template<class Tscaler>
void DemandCalculator::CalcMinimisedDistanceDemand(LinkGraphJob &job, const std::vector<bool> &reachable_nodes, Tscaler scaler)
{
	std::vector<NodeID> supplies;
	std::vector<NodeID> demands;

	for (NodeID node = 0; node < job.Size(); node++) {
		if (!reachable_nodes[node]) continue;
		scaler.AddNode(job[node]);
		if (job[node].Supply() > 0) {
			supplies.push_back(node);
		}
		if (job[node].Demand() > 0) {
			demands.push_back(node);
		}
	}

	if (supplies.empty() || demands.empty()) return;

	scaler.SetDemandPerNode((uint)demands.size());
	scaler.AdjustDemandNodes(job, demands);

	struct EdgeCandidate {
		NodeID from_id;
		NodeID to_id;
		uint distance;
	};
	std::vector<EdgeCandidate> candidates;
	candidates.reserve(supplies.size() * demands.size() - std::min(supplies.size(), demands.size()));
	for (NodeID from_id : supplies) {
		for (NodeID to_id : demands) {
			if (from_id != to_id) {
				candidates.push_back({ from_id, to_id, DistanceMaxPlusManhattan(job[from_id].XY(), job[to_id].XY()) });
			}
		}
	}
	std::sort(candidates.begin(), candidates.end(), [](const EdgeCandidate &a, const EdgeCandidate &b) {
		return std::tie(a.distance, a.from_id, a.to_id) < std::tie(b.distance, b.from_id, b.to_id);
	});
	for (const EdgeCandidate &candidate : candidates) {
		if (job[candidate.from_id].UndeliveredSupply() == 0) continue;
		if (!scaler.HasDemandLeft(job[candidate.to_id])) continue;

		scaler.SetDemands(job, candidate.from_id, candidate.to_id, std::min(job[candidate.from_id].UndeliveredSupply(), scaler.EffectiveSupply(job[candidate.from_id], job[candidate.to_id])));
	}
}

/**
 * Create the DemandCalculator and immediately do the calculation.
 * @param job Job to calculate the demands for.
 */
DemandCalculator::DemandCalculator(LinkGraphJob &job) :
	max_distance(DistanceMaxPlusManhattan(TileXY(0,0), TileXY(MapMaxX(), MapMaxY())))
{
	const LinkGraphSettings &settings = job.Settings();
	CargoID cargo = job.Cargo();

	this->accuracy = settings.accuracy;
	this->mod_dist = settings.demand_distance;
	if (this->mod_dist > 100) {
		/* Increase effect of mod_dist > 100 */
		int over100 = this->mod_dist - 100;
		this->mod_dist = 100 + over100 * over100;
	}

	if (settings.GetDistributionType(cargo) == DT_MANUAL) return;

	const uint size = job.Size();

	/* Symmetric edge matrix
	 * Storage order: e01  e02 e12  e03 e13 e23  e04 e14 e24 e34  ... */
	auto se_index = [](uint i, uint j) -> uint {
		if (j < i) std::swap(i, j);
		return i + (j * (j - 1) / 2);
	};
	std::vector<bool> symmetric_edges(se_index(0, size));

	for (auto &it : job.Graph().GetEdges()) {
		if (it.first.first != it.first.second) {
			symmetric_edges[se_index(it.first.first, it.first.second)] = true;
		}
	}
	uint first_unseen = 0;
	std::vector<bool> reachable_nodes(size);
	job.demand_matrix.reset(new uint[size * size]{});
	job.demand_matrix_count = 0;
	do {
		reachable_nodes.assign(size, false);
		std::vector<NodeID> queue;
		queue.push_back(first_unseen);
		reachable_nodes[first_unseen] = true;
		while (!queue.empty()) {
			NodeID from = queue.back();
			queue.pop_back();
			for (NodeID to = 0; to < size; ++to) {
				if (from == to) continue;
				if (symmetric_edges[se_index(from, to)]) {
					std::vector<bool>::reference bit = reachable_nodes[to];
					if (!bit) {
						bit = true;
						queue.push_back(to);
					}
				}
			}
		}

		switch (settings.GetDistributionType(cargo)) {
			case DT_SYMMETRIC:
				this->CalcDemand<SymmetricScaler>(job, reachable_nodes, SymmetricScaler(settings.demand_size));
				break;
			case DT_ASYMMETRIC:
				this->CalcDemand<AsymmetricScaler>(job, reachable_nodes, AsymmetricScaler());
				break;
			case DT_ASYMMETRIC_EQ:
				this->CalcMinimisedDistanceDemand<AsymmetricScalerEq>(job, reachable_nodes, AsymmetricScalerEq());
				break;
			case DT_ASYMMETRIC_NEAR:
				this->CalcMinimisedDistanceDemand<AsymmetricScaler>(job, reachable_nodes, AsymmetricScaler());
				break;
			default:
				/* Nothing to do. */
				break;
		}

		while (first_unseen < size && reachable_nodes[first_unseen]) {
			first_unseen++;
		}
	} while (first_unseen < size);

	if (job.demand_matrix_count > 0) {
		job.demand_annotation_store.resize(job.demand_matrix_count);
		size_t idx = 0;
		const uint *demand = job.demand_matrix.get();
		for (NodeID from = 0; from != size; from++) {
			const size_t start_idx = idx;
			for (NodeID to = 0; to != size; to++) {
				if (*demand != 0) {
					job.demand_annotation_store[idx] = { to, *demand, *demand };
					idx++;
				}
				demand++;
			}
			if (idx != start_idx) {
				job[from].SetDemandAnnotations({ job.demand_annotation_store.data() + start_idx, idx - start_idx });
			}
		}
	}
	job.demand_matrix.reset();
}
