/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file worker_thread.cpp Worker thread pool utility. */

#include "stdafx.h"
#include "worker_thread.h"
#include "thread.h"

#include "safeguards.h"

WorkerThreadPool _general_worker_pool;

void WorkerThreadPool::Start(const char *thread_name, uint max_workers)
{
	uint cpus = std::thread::hardware_concurrency();
	if (cpus <= 1) return;

	std::lock_guard<std::mutex> lk(this->lock);

	this->exit = false;

	uint worker_target = std::min<uint>(max_workers, cpus);
	if (this->workers >= worker_target) return;

	uint new_workers = worker_target - this->workers;

	for (uint i = 0; i < new_workers; i++) {
		this->workers++;
		if (!StartNewThread(nullptr, thread_name, &WorkerThreadPool::Run, this)) {
			this->workers--;
			return;
		}
	}
}

void WorkerThreadPool::Stop()
{
	std::unique_lock<std::mutex> lk(this->lock);
	this->exit = true;
	this->worker_wait_cv.notify_all();
	this->done_cv.wait(lk, [this]() { return this->workers == 0; });
}

void WorkerThreadPool::EnqueueJob(WorkerJobFunc *func, void *data1, void *data2, void *data3)
{
	std::unique_lock<std::mutex> lk(this->lock);
	if (this->workers == 0) {
		/* Just execute it here and now */
		lk.unlock();
		func(data1, data2, data3);
		return;
	}
	bool notify = this->jobs.size() < (size_t)this->workers_waiting;
	this->jobs.push({ func, data1, data2, data3 });
	lk.unlock();
	if (notify) this->worker_wait_cv.notify_one();
}

void WorkerThreadPool::Run(WorkerThreadPool *pool)
{
	std::unique_lock<std::mutex> lk(pool->lock);
	while (!pool->exit || !pool->jobs.empty()) {
		if (pool->jobs.empty()) {
			pool->workers_waiting++;
			pool->worker_wait_cv.wait(lk);
			pool->workers_waiting--;
		} else {
			WorkerJob job = pool->jobs.front();
			pool->jobs.pop();
			lk.unlock();
			job.func(job.data1, job.data2, job.data3);
			lk.lock();
		}
	}
	pool->workers--;
	if (pool->workers == 0) {
		pool->done_cv.notify_all();
	}
}
