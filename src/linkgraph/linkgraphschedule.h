/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraphschedule.h Declaration of link graph schedule used for cargo distribution. */

#ifndef LINKGRAPHSCHEDULE_H
#define LINKGRAPHSCHEDULE_H

#include "../thread.h"
#include "linkgraph.h"
#include <memory>

class LinkGraphJob;

namespace upstream_sl {
	SaveLoadTable GetLinkGraphScheduleDesc();
}

/**
 * A handler doing "something" on a link graph component. It must not keep any
 * state as it is called concurrently from different threads.
 */
class ComponentHandler {
public:
	/**
	 * Destroy the handler. Must be given due to virtual Run.
	 */
	virtual ~ComponentHandler() {}

	/**
	 * Run the handler. A link graph handler must not read or write any data
	 * outside the given component as that would create a potential desync.
	 * @param job Link graph component to run the handler on.
	 */
	virtual void Run(LinkGraphJob &job) const = 0;
};

class LinkGraphSchedule {
private:
	LinkGraphSchedule();
	~LinkGraphSchedule();
	typedef std::list<LinkGraph *> GraphList;
	typedef std::list<std::unique_ptr<LinkGraphJob>> JobList;
	friend SaveLoadTable GetLinkGraphScheduleDesc();
	friend upstream_sl::SaveLoadTable upstream_sl::GetLinkGraphScheduleDesc();

protected:
	std::unique_ptr<ComponentHandler> handlers[6]; ///< Handlers to be run for each job.
	GraphList schedule;            ///< Queue for new jobs.
	JobList running;               ///< Currently running jobs.

public:
	/* This is a tick where not much else is happening, so a small lag might go unnoticed. */
	static const uint SPAWN_JOIN_TICK = 21; ///< Tick when jobs are spawned or joined every day.
	static LinkGraphSchedule instance;

	static void Run(LinkGraphJob *job);
	static void Clear();

	void SpawnNext();
	bool IsJoinWithUnfinishedJobDue() const;
	void JoinNext();
	void SpawnAll();
	void ShiftDates(int interval);

	/**
	 * Queue a link graph for execution.
	 * @param lg Link graph to be queued.
	 */
	void Queue(LinkGraph *lg)
	{
		assert(LinkGraph::Get(lg->index) == lg);
		this->schedule.push_back(lg);
	}

	/**
	 * Remove a link graph from the execution queue.
	 * @param lg Link graph to be removed.
	 */
	void Unqueue(LinkGraph *lg) { this->schedule.remove(lg); }
};

class LinkGraphJobGroup : public std::enable_shared_from_this<LinkGraphJobGroup> {
	friend LinkGraphJob;

private:
	std::thread thread;                      ///< Thread the job group is running in or nullptr if it's running in the main thread.
	const std::vector<LinkGraphJob *> jobs;  ///< The set of jobs in this job set

private:
	struct constructor_token { };
	static void Run(void *group);
	void SpawnThread();
	void JoinThread();

public:
	LinkGraphJobGroup(constructor_token token, std::vector<LinkGraphJob *> jobs);

	struct JobInfo {
		LinkGraphJob * job;
		uint cost_estimate;

		JobInfo(LinkGraphJob *job);
		JobInfo(LinkGraphJob *job, uint cost_estimate) :
				job(job), cost_estimate(cost_estimate) { }
	};

	static void ExecuteJobSet(std::vector<JobInfo> jobs);
};

void StateGameLoop_LinkGraphPauseControl();
void AfterLoad_LinkGraphPauseControl();

#endif /* LINKGRAPHSCHEDULE_H */
