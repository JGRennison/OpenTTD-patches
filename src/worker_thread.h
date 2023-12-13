/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file worker_thread.h Worker thread pool utility. */

#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include "core/ring_buffer_queue.hpp"
#include <mutex>
#include <condition_variable>

typedef void WorkerJobFunc(void *, void *, void *);

struct WorkerThreadPool {
private:
	struct WorkerJob {
		WorkerJobFunc *func;
		void *data1;
		void *data2;
		void *data3;
	};

	uint workers = 0;
	uint workers_waiting = 0;
	bool exit = false;
	std::mutex lock;
	ring_buffer_queue<WorkerJob> jobs;
	std::condition_variable worker_wait_cv;
	std::condition_variable done_cv;

	static void Run(WorkerThreadPool *pool);

public:

	void Start(const char *thread_name, uint max_workers);
	void Stop();
	void EnqueueJob(WorkerJobFunc *func, void *data1 = nullptr, void *data2 = nullptr, void *data3 = nullptr);

	~WorkerThreadPool()
	{
		this->Stop();
	}
};

extern WorkerThreadPool _general_worker_pool;

#endif /* WORKER_THREAD_H */
