#include "thread_pool.h"

#include "absl/log/log.h"
#include "fiber.h"            // For Fiber class
#include "fiber_scheduler.h"  // For FiberScheduler class

// Define the global ThreadPool instance.
std::unique_ptr<ThreadPool> global_thread_pool;

ThreadPool::ThreadPool(int num_threads)
    : scheduler_(std::make_unique<FiberScheduler>(num_threads)) {
  LOG(ERROR) << "ThreadPool initialized with " << num_threads
             << " kernel threads.";
}

ThreadPool::~ThreadPool() { LOG(ERROR) << "ThreadPool shutting down."; }

void ThreadPool::Schedule(absl::AnyInvocable<void()> task) {
  auto fiber = std::make_unique<Fiber>(std::move(task), scheduler_.get());
  scheduler_->Schedule(fiber.release());  // FiberScheduler takes ownership.
}

FiberScheduler *ThreadPool::GetInternalScheduler() { return scheduler_.get(); }
