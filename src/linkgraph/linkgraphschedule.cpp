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
#include "../command_func.h"

#include "../safeguards.h"

/**
 * Static instance of LinkGraphSchedule.
 * Note: This instance is created on task start.
 *       Lazy creation on first usage results in a data race between the CDist threads.
 */
/* static */ LinkGraphSchedule LinkGraphSchedule::instance;

/**
 * Start the next job in the schedule.
 */
void LinkGraphSchedule::SpawnNext()
{
	if (this->schedule.empty()) return;
	LinkGraph *next = this->schedule.front();
	LinkGraph *first = next;
	while (next->Size() < 2) {
		this->schedule.splice(this->schedule.end(), this->schedule, this->schedule.begin());
		next = this->schedule.front();
		if (next == first) return;
	}
	assert(next == LinkGraph::Get(next->index));
	this->schedule.pop_front();
	if (LinkGraphJob::CanAllocateItem()) {
		LinkGraphJob *job = new LinkGraphJob(*next);
		job->SpawnThread();
		this->running.push_back(job);
	} else {
		NOT_REACHED();
	}
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
		LinkGraphJob *next = this->running.front();
		if (!next->IsFinished()) return;
		this->running.pop_front();
		LinkGraphID id = next->LinkGraphIndex();
		delete next; // implicitly joins the thread
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
	for (JobList::iterator i = this->running.begin(); i != this->running.end(); ++i) {
		(*i)->SpawnThread();
	}
}

/**
 * Clear all link graphs and jobs from the schedule.
 */
/* static */ void LinkGraphSchedule::Clear()
{
	for (JobList::iterator i(instance.running.begin()); i != instance.running.end(); ++i) {
		(*i)->JoinThread();
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
	this->handlers[0] = new InitHandler;
	this->handlers[1] = new DemandHandler;
	this->handlers[2] = new MCFHandler<MCF1stPass>;
	this->handlers[3] = new FlowMapper(false);
	this->handlers[4] = new MCFHandler<MCF2ndPass>;
	this->handlers[5] = new FlowMapper(true);
}

/**
 * Delete a link graph schedule and its handlers.
 */
LinkGraphSchedule::~LinkGraphSchedule()
{
	this->Clear();
	for (uint i = 0; i < lengthof(this->handlers); ++i) {
		delete this->handlers[i];
	}
}

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
		LinkGraphSchedule::instance.JoinNext();
	}
}


