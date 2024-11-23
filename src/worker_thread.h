/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file worker_thread.h Worker thread pool utility. */

#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include "core/bit_cast.hpp"
#include "core/ring_buffer_queue.hpp"
#include <mutex>
#include <condition_variable>
#include <tuple>

struct WorkerThreadPool {
private:
	struct WorkerJob {
		using WorkerJobFunc = void(WorkerJob &job);
		using Payload = std::array<uintptr_t, 3>;

		WorkerJobFunc *func;
		Payload payload;

		template <auto F, typename T, size_t... i>
		inline void Execute(std::index_sequence<i...>)
		{
			F(bit_cast_from_storage<std::tuple_element_t<i, T>>(this->payload[i])...);
		}
	};

	uint workers = 0;
	uint workers_waiting = 0;
	bool exit = false;
	std::mutex lock;
	ring_buffer_queue<WorkerJob> jobs;
	std::condition_variable worker_wait_cv;
	std::condition_variable done_cv;

	static void Run(WorkerThreadPool *pool);

	void EnqueueWorkerJob(WorkerJob job);

public:

	void Start(const char *thread_name, uint max_workers);
	void Stop();

	/* Currently supports up to 3 arguments up to sizeof(uintptr_t) */
	template <auto F, typename... Args>
	void EnqueueJob(Args... args)
	{
		using tuple_type = decltype(std::make_tuple(args...));

		WorkerJob job;
		job.payload = { bit_cast_to_storage<uintptr_t>(args)... };
		job.func = [](WorkerJob &job) {
			job.Execute<F, tuple_type>(std::index_sequence_for<Args...>{});
		};
		this->EnqueueWorkerJob(job);
	}

	~WorkerThreadPool()
	{
		this->Stop();
	}
};

extern WorkerThreadPool _general_worker_pool;

#endif /* WORKER_THREAD_H */
