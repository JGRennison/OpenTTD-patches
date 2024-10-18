/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraphschedule.cpp Definition of link graph schedule used for cargo distribution. */

#include "../stdafx.h"
#include "linkgraphschedule.h"
#include "init.h"
#include "demands.h"
#include "mcf.h"
#include "flowmapper.h"
#include "../framerate_type.h"
#include "../command_func.h"
#include "../network/network.h"
#include <algorithm>

#include "../safeguards.h"

/**
 * Static instance of LinkGraphSchedule.
 * Note: This instance is created on task start.
 *       Lazy creation on first usage results in a data race between the CDist threads.
 */
/* static */ LinkGraphSchedule LinkGraphSchedule::instance;

/**
 * Start the next job(s) in the schedule.
 *
 * The cost estimate of a link graph job is C ~ N^2 log N, where
 * N is the number of nodes in the job link graph.
 *
 * The cost estimate is summed for all running and scheduled jobs to form the total cost estimate T = sum C.
 * The clamped total cost estimate is calculated as U = min(1 << 25, T). This is to prevent excessively high cost budgets.
 * The nominal cycle time (in recalc intervals) required to schedule all jobs is calculated as S = 1 + max(0, log_2 U - 13).
 * The cost budget for an individual call to this method is given by U / S.
 * The last scheduled job may exceed the cost budget.
 *
 * The nominal duration of an individual job is D = N / 75
 *
 * The purpose of this algorithm is so that overall responsiveness is not hindered by large numbers of small/cheap
 * jobs which would previously need to be cycled through individually, but equally large/slow jobs have an extended
 * duration in which to execute, to avoid unnecessary pauses.
 */
void LinkGraphSchedule::SpawnNext()
{
	if (this->schedule.empty()) return;

	GraphList schedule_to_back;
	uint64_t total_cost = 0;
	for (auto iter = this->schedule.begin(); iter != this->schedule.end();) {
		auto current = iter;
		++iter;
		const LinkGraph *lg = *current;

		if (lg->Size() < 2) {
			schedule_to_back.splice(schedule_to_back.end(), this->schedule, current);
		} else {
			total_cost += lg->CalculateCostEstimate();
		}
	}
	for (auto &it : this->running) {
		total_cost += it->Graph().CalculateCostEstimate();
	}
	uint64_t clamped_total_cost = std::min<uint64_t>(total_cost, 1 << 25);
	uint log2_clamped_total_cost = FindLastBit(clamped_total_cost);
	uint scaling = log2_clamped_total_cost > 13 ? log2_clamped_total_cost - 12 : 1;
	uint64_t cost_budget = clamped_total_cost / scaling;
	uint64_t used_budget = 0;
	std::vector<LinkGraphJobGroup::JobInfo> jobs_to_execute;
	while (used_budget < cost_budget && !this->schedule.empty()) {
		LinkGraph *lg = this->schedule.front();
		assert(lg == LinkGraph::Get(lg->index));
		this->schedule.pop_front();
		uint64_t cost = lg->CalculateCostEstimate();
		used_budget += cost;
		if (LinkGraphJob::CanAllocateItem()) {
			uint duration_multiplier = CeilDivT<uint64_t>(lg->Size(), 75);
			std::unique_ptr<LinkGraphJob> job(new LinkGraphJob(*lg, duration_multiplier));
			jobs_to_execute.emplace_back(job.get(), cost);
			if (this->running.empty() || job->JoinTick() >= this->running.back()->JoinTick()) {
				this->running.push_back(std::move(job));
				Debug(linkgraph, 3, "LinkGraphSchedule::SpawnNext(): Running job: id: {}, nodes: {}, cost: {}, duration_multiplier: {}",
						lg->index, lg->Size(), cost, duration_multiplier);
			} else {
				/* Find right place to insert */
				auto iter = std::upper_bound(this->running.begin(), this->running.end(), job->JoinTick(), [](ScaledTickCounter a, const std::unique_ptr<LinkGraphJob> &b) {
					return a < b->JoinTick();
				});
				this->running.insert(iter, std::move(job));
				Debug(linkgraph, 3, "LinkGraphSchedule::SpawnNext(): Running job (re-ordering): id: {}, nodes: {}, cost: {}, duration_multiplier: {}",
						lg->index, lg->Size(), cost, duration_multiplier);
			}
		} else {
			NOT_REACHED();
		}
	}

	this->schedule.splice(this->schedule.end(), schedule_to_back);

	LinkGraphJobGroup::ExecuteJobSet(std::move(jobs_to_execute));

	Debug(linkgraph, 2, "LinkGraphSchedule::SpawnNext(): Linkgraph job totals: cost: {}, budget: {}, scaling: {}, scheduled: {}, running: {}",
			total_cost, cost_budget, scaling, this->schedule.size(), this->running.size());
}

/**
 * Join the next finished job, if available.
 */
bool LinkGraphSchedule::IsJoinWithUnfinishedJobDue() const
{
	for (JobList::const_iterator it = this->running.begin(); it != this->running.end(); ++it) {
		if (!((*it)->IsScheduledToBeJoined(2))) {
			/* job is not due to be joined yet */
			return false;
		}
		if (!((*it)->IsJobCompleted())) {
			/* job is due to be joined, but is not completed */
			return true;
		}
	}
	return false;
}

/**
 * Join the next finished job, if available.
 */
void LinkGraphSchedule::JoinNext()
{
	while (!(this->running.empty())) {
		if (!this->running.front()->IsScheduledToBeJoined()) return;
		std::unique_ptr<LinkGraphJob> next = std::move(this->running.front());
		this->running.pop_front();
		LinkGraphID id = next->LinkGraphIndex();
		next->FinaliseJob(); // joins the thread and finalises the job
		assert(!next->IsJobAborted());
		next.reset();
		if (LinkGraph::IsValidID(id)) {
			LinkGraph *lg = LinkGraph::Get(id);
			this->Unqueue(lg); // Unqueue to avoid double-queueing recycled IDs.
			this->Queue(lg);
		}
	}
}

/**
 * Run all handlers for the given Job.
 * @param job Pointer to a link graph job.
 */
/* static */ void LinkGraphSchedule::Run(LinkGraphJob *job)
{
	for (const auto &handler : instance.handlers) {
		if (job->IsJobAborted()) return;
		handler->Run(*job);
	}

	/*
	 * Readers of this variable in another thread may see an out of date value.
	 * However this is OK as this will only happen just as a job is completing,
	 * and the real synchronisation is provided by the thread join operation.
	 * In the worst case the main thread will be paused for longer than
	 * strictly necessary before joining.
	 * This is just a hint variable to avoid performing the join excessively
	 * early and blocking the main thread.
	 */

	job->job_completed.store(true, std::memory_order_release);
}

/**
 * Start all threads in the running list. This is only useful for save/load.
 * Usually threads are started when the job is created.
 */
void LinkGraphSchedule::SpawnAll()
{
	std::vector<LinkGraphJobGroup::JobInfo> jobs_to_execute;
	for (auto &it : this->running) {
		jobs_to_execute.emplace_back(it.get());
	}
	LinkGraphJobGroup::ExecuteJobSet(std::move(jobs_to_execute));
}

/**
 * Clear all link graphs and jobs from the schedule.
 */
/* static */ void LinkGraphSchedule::Clear()
{
	for (auto &it : instance.running) {
		it->AbortJob();
	}
	instance.running.clear();
	instance.schedule.clear();
}

/**
 * Shift all dates (join dates and edge annotations) of link graphs and link
 * graph jobs by the number of days given.
 * @param interval Number of days to be added or subtracted.
 */
void LinkGraphSchedule::ShiftDates(DateDelta interval)
{
	for (LinkGraph *lg : LinkGraph::Iterate()) lg->ShiftDates(interval);
}

/**
 * Create a link graph schedule and initialize its handlers.
 */
LinkGraphSchedule::LinkGraphSchedule()
{
	this->handlers[0] = std::make_unique<InitHandler>();
	this->handlers[1] = std::make_unique<DemandHandler>();
	this->handlers[2] = std::make_unique<MCFHandler<MCF1stPass>>();
	this->handlers[3] = std::make_unique<FlowMapper>(false);
	this->handlers[4] = std::make_unique<MCFHandler<MCF2ndPass>>();
	this->handlers[5] = std::make_unique<FlowMapper>(true);
}

/**
 * Delete a link graph schedule and its handlers.
 */
LinkGraphSchedule::~LinkGraphSchedule()
{
	this->Clear();
}

LinkGraphJobGroup::LinkGraphJobGroup(constructor_token token, std::vector<LinkGraphJob *> jobs) :
	jobs(std::move(jobs)) { }

void LinkGraphJobGroup::SpawnThread()
{
	/**
	 * Spawn a thread if possible and run the link graph job in the thread. If
	 * that's not possible run the job right now in the current thread.
	 */
	if (StartNewThread(&this->thread, "ottd:linkgraph", &(LinkGraphJobGroup::Run), this)) {
		for (auto &it : this->jobs) {
			it->SetJobGroup(this->shared_from_this());
		}
	} else {
		/* Of course this will hang a bit.
		 * On the other hand, if you want to play games which make this hang noticably
		 * on a platform without threads then you'll probably get other problems first.
		 * OK:
		 * If someone comes and tells me that this hangs for them, I'll implement a
		 * smaller grained "Step" method for all handlers and add some more ticks where
		 * "Step" is called. No problem in principle. */
		LinkGraphJobGroup::Run(this);
	}
}

void LinkGraphJobGroup::JoinThread()
{
	if (this->thread.joinable()) {
		this->thread.join();
	}
}

/**
 * Run all jobs for the given LinkGraphJobGroup. This method is tailored to
 * ThreadObject::New.
 * @param j Pointer to a LinkGraphJobGroup.
 */
/* static */ void LinkGraphJobGroup::Run(void *group)
{
	LinkGraphJobGroup *job_group = (LinkGraphJobGroup *)group;
	for (LinkGraphJob *job : job_group->jobs) {
		LinkGraphSchedule::Run(job);
	}
}

/* static */ void LinkGraphJobGroup::ExecuteJobSet(std::vector<JobInfo> jobs) {
	const uint thread_budget = 200000;

	std::sort(jobs.begin(), jobs.end(), [](const JobInfo &a, const JobInfo &b) {
		return std::make_pair(a.job->JoinTick(), a.cost_estimate) < std::make_pair(b.job->JoinTick(), b.cost_estimate);
	});

	std::vector<LinkGraphJob *> bucket;
	uint bucket_cost = 0;
	ScaledTickCounter bucket_join_tick = 0;
	auto flush_bucket = [&]() {
		if (!bucket_cost) return;
		Debug(linkgraph, 2, "LinkGraphJobGroup::ExecuteJobSet: Creating Job Group: jobs: {}, cost: {}, join after: {}",
				bucket.size(), bucket_cost, bucket_join_tick - _scaled_tick_counter);
		auto group = std::make_shared<LinkGraphJobGroup>(constructor_token(), std::move(bucket));
		group->SpawnThread();
		bucket_cost = 0;
		bucket.clear();
	};

	for (JobInfo &it : jobs) {
		if (bucket_cost && (bucket_join_tick != it.job->JoinTick() || (bucket_cost + it.cost_estimate > thread_budget))) flush_bucket();
		bucket_join_tick = it.job->JoinTick();
		bucket.push_back(it.job);
		bucket_cost += it.cost_estimate;
	}
	flush_bucket();
}

LinkGraphJobGroup::JobInfo::JobInfo(LinkGraphJob *job) :
		job(job), cost_estimate(job->Graph().CalculateCostEstimate()) { }

/**
 * Pause the game if in 2 ticks, we would do a join with the next
 * link graph job, but it is still running.
 * The check is done 2 ticks early instead of 1, as in multiplayer
 * calls to DoCommandP are executed after a delay of 1 tick.
 * If we previously paused, unpause if the job is now ready to be joined with.
 */
void StateGameLoop_LinkGraphPauseControl()
{
	if (_pause_mode & PM_PAUSED_LINK_GRAPH) {
		/* We are paused waiting on a job, check the job every tick */
		if (!LinkGraphSchedule::instance.IsJoinWithUnfinishedJobDue()) {
			DoCommandP(0, PM_PAUSED_LINK_GRAPH, 0, CMD_PAUSE);
		}
	} else if (_pause_mode == PM_UNPAUSED) {
		int interval = _settings_game.linkgraph.recalc_interval * DAY_TICKS / SECONDS_PER_DAY;
		int offset = _scaled_tick_counter % interval;
		if (offset == (interval / 2) - 2) {
			/* perform check 2 ticks before we would join */
			if (LinkGraphSchedule::instance.IsJoinWithUnfinishedJobDue()) {
				DoCommandP(0, PM_PAUSED_LINK_GRAPH, 1, CMD_PAUSE);
			}
		}
	}
}

/**
 * Pause the game on load if we would do a join with the next link graph job,
 * but it is still running, and it would not be caught by a call to
 * StateGameLoop_LinkGraphPauseControl().
 */
void AfterLoad_LinkGraphPauseControl()
{
	if (LinkGraphSchedule::instance.IsJoinWithUnfinishedJobDue()) {
		_pause_mode |= PM_PAUSED_LINK_GRAPH;
	}
}

/**
 * Spawn or join a link graph job or compress a link graph if any link graph is
 * due to do so.
 */
void OnTick_LinkGraph()
{
	int interval = _settings_game.linkgraph.recalc_interval * DAY_TICKS / SECONDS_PER_DAY;
	int offset = _scaled_tick_counter % interval;
	if (offset == 0) {
		LinkGraphSchedule::instance.SpawnNext();
	} else if (offset == interval / 2) {
		if (!_networking || _network_server) {
			PerformanceMeasurer::SetInactive(PFE_GL_LINKGRAPH);
			LinkGraphSchedule::instance.JoinNext();
		} else {
			PerformanceMeasurer framerate(PFE_GL_LINKGRAPH);
			LinkGraphSchedule::instance.JoinNext();
		}
	}
}


