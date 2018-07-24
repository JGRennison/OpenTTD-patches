/* $Id$ */

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
 * The cost estimate is summed for all running and scheduled jobs to form the total cost estimate T = sum C.
 * The nominal cycle time (in recalc intervals) required to schedule all jobs is calculated as S = 1 + log_2 T.
 * Hence the nominal duration of an individual job (in recalc intervals) is D = ceil(S * C / T)
 * The cost budget for an individual call to this method is given by T / S.
 *
 * The purpose of this algorithm is so that overall responsiveness is not hindered by large numbers of small/cheap
 * jobs which would previously need to be cycled through individually, but equally large/slow jobs have an extended
 * duration in which to execute, to avoid unnecessary pauses.
 */
void LinkGraphSchedule::SpawnNext()
{
	if (this->schedule.empty()) return;

	GraphList schedule_to_back;
	uint64 total_cost = 0;
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
	uint scaling = 1 + FindLastBit(total_cost);
	uint64 cost_budget = total_cost / scaling;
	uint64 used_budget = 0;
	std::vector<LinkGraphJobGroup::JobInfo> jobs_to_execute;
	while (used_budget < cost_budget && !this->schedule.empty()) {
		LinkGraph *lg = this->schedule.front();
		assert(lg == LinkGraph::Get(lg->index));
		this->schedule.pop_front();
		uint64 cost = lg->CalculateCostEstimate();
		used_budget += cost;
		if (LinkGraphJob::CanAllocateItem()) {
			uint duration_multiplier = CeilDivT<uint64_t>(scaling * cost, total_cost);
			std::unique_ptr<LinkGraphJob> job(new LinkGraphJob(*lg, duration_multiplier));
			jobs_to_execute.emplace_back(job.get(), cost);
			if (this->running.empty() || job->JoinDateTicks() >= this->running.back()->JoinDateTicks()) {
				this->running.push_back(std::move(job));
				DEBUG(linkgraph, 3, "LinkGraphSchedule::SpawnNext(): Running job: id: %u, nodes: %u, cost: " OTTD_PRINTF64U ", duration_multiplier: %u",
						lg->index, lg->Size(), cost, duration_multiplier);
			} else {
				// find right place to insert
				auto iter = std::upper_bound(this->running.begin(), this->running.end(), job->JoinDateTicks(), [](DateTicks a, const std::unique_ptr<LinkGraphJob> &b) {
					return a < b->JoinDateTicks();
				});
				this->running.insert(iter, std::move(job));
				DEBUG(linkgraph, 3, "LinkGraphSchedule::SpawnNext(): Running job (re-ordering): id: %u, nodes: %u, cost: " OTTD_PRINTF64U ", duration_multiplier: %u",
						lg->index, lg->Size(), cost, duration_multiplier);
			}
		} else {
			NOT_REACHED();
		}
	}

	this->schedule.splice(this->schedule.end(), schedule_to_back);

	LinkGraphJobGroup::ExecuteJobSet(std::move(jobs_to_execute));

	DEBUG(linkgraph, 2, "LinkGraphSchedule::SpawnNext(): Linkgraph job totals: cost: " OTTD_PRINTF64U ", budget: " OTTD_PRINTF64U ", scaling: %u, scheduled: " PRINTF_SIZE ", running: " PRINTF_SIZE,
			total_cost, cost_budget, scaling, this->schedule.size(), this->running.size());
}

/**
 * Join the next finished job, if available.
 */
bool LinkGraphSchedule::IsJoinWithUnfinishedJobDue() const
{
	for (JobList::const_iterator it = this->running.begin(); it != this->running.end(); ++it) {
		if (!((*it)->IsFinished(1))) {
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
		if (!this->running.front()->IsFinished()) return;
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
 * Run all handlers for the given Job. This method is tailored to
 * ThreadObject::New.
 * @param j Pointer to a link graph job.
 */
/* static */ void LinkGraphSchedule::Run(void *j)
{
	LinkGraphJob *job = (LinkGraphJob *)j;
	for (uint i = 0; i < lengthof(instance.handlers); ++i) {
		if (job->IsJobAborted()) return;
		instance.handlers[i]->Run(*job);
	}

	/*
	 * Note that this it not guaranteed to be an atomic write and there are no memory barriers or other protections.
	 * Readers of this variable in another thread may see an out of date value.
	 * However this is OK as this will only happen just as a job is completing, and the real synchronisation is provided
	 * by the thread join operation. In the worst case the main thread will be paused for longer than strictly necessary before
	 * joining.
	 * This is just a hint variable to avoid performing the join excessively early and blocking the main thread.
	 */

#if defined(__GNUC__) || defined(__clang__)
	__atomic_store_n(&(job->job_completed), true, __ATOMIC_RELAXED);
#else
	job->job_completed = true;
#endif
}

/**
 * Start all threads in the running list. This is only useful for save/load.
 * Usually threads are started when the job is created.
 */
void LinkGraphSchedule::SpawnAll()
{
	std::vector<LinkGraphJobGroup::JobInfo> jobs_to_execute;
	for (JobList::iterator i = this->running.begin(); i != this->running.end(); ++i) {
		jobs_to_execute.emplace_back(i->get());
	}
	LinkGraphJobGroup::ExecuteJobSet(std::move(jobs_to_execute));
}

/**
 * Clear all link graphs and jobs from the schedule.
 */
/* static */ void LinkGraphSchedule::Clear()
{
	for (JobList::iterator i(instance.running.begin()); i != instance.running.end(); ++i) {
		(*i)->AbortJob();
	}
	instance.running.clear();
	instance.schedule.clear();
}

/**
 * Shift all dates (join dates and edge annotations) of link graphs and link
 * graph jobs by the number of days given.
 * @param interval Number of days to be added or subtracted.
 */
void LinkGraphSchedule::ShiftDates(int interval)
{
	LinkGraph *lg;
	FOR_ALL_LINK_GRAPHS(lg) lg->ShiftDates(interval);
	LinkGraphJob *lgj;
	FOR_ALL_LINK_GRAPH_JOBS(lgj) lgj->ShiftJoinDate(interval);
}

/**
 * Create a link graph schedule and initialize its handlers.
 */
LinkGraphSchedule::LinkGraphSchedule()
{
	this->handlers[0].reset(new InitHandler);
	this->handlers[1].reset(new DemandHandler);
	this->handlers[2].reset(new MCFHandler<MCF1stPass>);
	this->handlers[3].reset(new FlowMapper(false));
	this->handlers[4].reset(new MCFHandler<MCF2ndPass>);
	this->handlers[5].reset(new FlowMapper(true));
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

void LinkGraphJobGroup::SpawnThread() {
	ThreadObject *t = nullptr;

	/**
	 * Spawn a thread if possible and run the link graph job in the thread. If
	 * that's not possible run the job right now in the current thread.
	 */
	if (ThreadObject::New(&(LinkGraphJobGroup::Run), this, &t, "ottd:linkgraph")) {
		this->thread.reset(t);
		for (auto &it : this->jobs) {
			it->SetJobGroup(this->shared_from_this());
		}
	} else {
		this->thread.reset();
		/* Of course this will hang a bit.
		 * On the other hand, if you want to play games which make this hang noticably
		 * on a platform without threads then you'll probably get other problems first.
		 * OK:
		 * If someone comes and tells me that this hangs for him/her, I'll implement a
		 * smaller grained "Step" method for all handlers and add some more ticks where
		 * "Step" is called. No problem in principle. */
		LinkGraphJobGroup::Run(this);
	}
}

void LinkGraphJobGroup::JoinThread() {
	if (!this->thread || this->joined_thread) return;
	this->thread->Join();
	this->joined_thread = true;
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
		return a.cost_estimate < b.cost_estimate;
	});

	std::vector<LinkGraphJob *> bucket;
	uint bucket_cost = 0;
	auto flush_bucket = [&]() {
		if (!bucket_cost) return;
		DEBUG(linkgraph, 2, "LinkGraphJobGroup::ExecuteJobSet: Creating Job Group: jobs: " PRINTF_SIZE ", cost: %u", bucket.size(), bucket_cost);
		auto group = std::make_shared<LinkGraphJobGroup>(constructor_token(), std::move(bucket));
		group->SpawnThread();
		bucket_cost = 0;
		bucket.clear();
	};

	for (JobInfo &it : jobs) {
		if (bucket_cost && (bucket_cost + it.cost_estimate > thread_budget)) flush_bucket();
		bucket.push_back(it.job);
		bucket_cost += it.cost_estimate;
	}
	flush_bucket();
}

LinkGraphJobGroup::JobInfo::JobInfo(LinkGraphJob *job) :
		job(job), cost_estimate(job->Graph().CalculateCostEstimate()) { }

/**
 * Pause the game if on the next _date_fract tick, we would do a join with the next
 * link graph job, but it is still running.
 * If we previous paused, unpause if the job is now ready to be joined with
 */
void StateGameLoop_LinkGraphPauseControl()
{
	if (_pause_mode & PM_PAUSED_LINK_GRAPH) {
		/* We are paused waiting on a job, check the job every tick */
		if (!LinkGraphSchedule::instance.IsJoinWithUnfinishedJobDue()) {
			DoCommandP(0, PM_PAUSED_LINK_GRAPH, 0, CMD_PAUSE);
		}
	} else if (_pause_mode == PM_UNPAUSED && _tick_skip_counter == 0) {
		if (!_settings_game.linkgraph.recalc_not_scaled_by_daylength || _settings_game.economy.day_length_factor == 1) {
			if (_date_fract != LinkGraphSchedule::SPAWN_JOIN_TICK - 1) return;
			if (_date % _settings_game.linkgraph.recalc_interval != _settings_game.linkgraph.recalc_interval / 2) return;
		} else {
			int date_ticks = ((_date * DAY_TICKS) + _date_fract - (LinkGraphSchedule::SPAWN_JOIN_TICK - 1));
			int interval = max<int>(2, (_settings_game.linkgraph.recalc_interval * DAY_TICKS / _settings_game.economy.day_length_factor));
			if (date_ticks % interval != interval / 2) return;
		}

		/* perform check one _date_fract tick before we would join */
		if (LinkGraphSchedule::instance.IsJoinWithUnfinishedJobDue()) {
			DoCommandP(0, PM_PAUSED_LINK_GRAPH, 1, CMD_PAUSE);
		}
	}
}

/**
 * Spawn or join a link graph job or compress a link graph if any link graph is
 * due to do so.
 */
void OnTick_LinkGraph()
{
	int offset;
	int interval;
	if (!_settings_game.linkgraph.recalc_not_scaled_by_daylength || _settings_game.economy.day_length_factor == 1) {
		if (_date_fract != LinkGraphSchedule::SPAWN_JOIN_TICK) return;
		interval = _settings_game.linkgraph.recalc_interval;
		offset = _date % interval;
	} else {
		interval = max<int>(2, (_settings_game.linkgraph.recalc_interval * DAY_TICKS / _settings_game.economy.day_length_factor));
		offset = ((_date * DAY_TICKS) + _date_fract - LinkGraphSchedule::SPAWN_JOIN_TICK) % interval;
	}
	if (offset == 0) {
		LinkGraphSchedule::instance.SpawnNext();
	} else if (offset == interval / 2) {
		PerformanceMeasurer framerate(PFE_GL_LINKGRAPH);
		LinkGraphSchedule::instance.JoinNext();
	}
}


