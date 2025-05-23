/** @file demands.h Declaration of demand calculating link graph handler. */

#ifndef DEMANDS_H
#define DEMANDS_H

#include "linkgraphjob_base.h"
#include <vector>

/**
 * Calculate the demands. This class has a state, but is recreated for each
 * call to of DemandHandler::Run.
 */
class DemandCalculator {
public:
	DemandCalculator(LinkGraphJob &job);

private:
	int32_t base_distance; ///< Base distance for scaling purposes.
	int32_t mod_dist;      ///< Distance modifier, determines how much demands decrease with distance.
	int32_t accuracy;      ///< Accuracy of the calculation.

	template <class Tscaler>
	void CalcDemand(LinkGraphJob &job, const std::vector<bool> &reachable_nodes, Tscaler scaler);

	template <class Tscaler>
	void CalcMinimisedDistanceDemand(LinkGraphJob &job, const std::vector<bool> &reachable_nodes, Tscaler scaler);
};

/**
 * Stateless, thread safe demand handler. Doesn't do anything but call DemandCalculator.
 */
class DemandHandler : public ComponentHandler {
public:

	/**
	 * Call the demand calculator on the given component.
	 * @param job Component to calculate the demands for.
	 */
	void Run(LinkGraphJob &job) const override { DemandCalculator c(job); }

	/**
	 * Virtual destructor has to be defined because of virtual Run().
	 */
	virtual ~DemandHandler() = default;
};

#endif /* DEMANDS_H */
