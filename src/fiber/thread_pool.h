#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_

#include <memory>
#include "absl/functional/any_invocable.h"
#include "fiber_types.h" // For FiberScheduler forward declaration

// --- 5. ThreadPool Class ---
// A high-level abstraction for scheduling absl::AnyInvocable tasks
// using the underlying FiberScheduler.
class ThreadPool {
public:
    // Constructor: Initializes the internal FiberScheduler with the given number of threads.
    explicit ThreadPool(int num_threads);

    // Destructor: The unique_ptr will automatically destroy the FiberScheduler,
    // which handles joining its worker threads.
    ~ThreadPool();

    // Schedules a task (absl::AnyInvocable) to be executed by a fiber.
    // The task will run on one of the ThreadPool's kernel threads.
    void Schedule(absl::AnyInvocable<void()> task);

    // Provides access to the internal FiberScheduler.
    // This is needed for FiberMutex and FiberConditionVariable's GetScheduler helpers.
    FiberScheduler* GetInternalScheduler();

private:
    std::unique_ptr<FiberScheduler> scheduler_; // The underlying FiberScheduler.
};

// Global ThreadPool instance. Declared extern here, defined in thread_pool.cc.
extern std::unique_ptr<ThreadPool> global_thread_pool;

#endif // THREAD_POOL_H_
