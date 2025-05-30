#ifndef FIBER_SCHEDULER_H_
#define FIBER_SCHEDULER_H_

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <thread>
#include <unordered_set>

#include "absl/base/thread_annotations.h" // For ABSL_GUARDED_BY
#include "absl/synchronization/condition.h"
#include "absl/synchronization/mutex.h"
#include "fiber_types.h" // For Fiber forward declaration and jmp_buf

// --- 2. FiberScheduler Class ---
// Manages the lifecycle and scheduling of fibers on a fixed-size pool of kernel threads.
class FiberScheduler {
public:
    // Constructor: Initializes the thread pool.
    // 'num_threads': The number of kernel threads that will execute fibers.
    explicit FiberScheduler(int num_threads);

    // Destructor: Shuts down the scheduler and waits for all worker threads to finish.
    ~FiberScheduler();

    // Schedules a fiber to be run. Adds it to the ready queue.
    void Schedule(Fiber* fiber);

    // The currently running fiber explicitly yields control back to the scheduler.
    // This is the core mechanism for cooperative multitasking.
    void Yield();

    // Blocks the currently running fiber, moving it to a BLOCKED state.
    // The fiber will not be scheduled again until explicitly unblocked.
    // This is used by `FiberMutex` and `FiberConditionVariable` to put fibers to sleep.
    void Block();

    // Unblocks a specific fiber, moving it back to the READY state and
    // scheduling it. This is called by `FiberMutex` or `FiberConditionVariable`
    // when an event they are waiting for occurs.
    void Unblock(Fiber* fiber);

    // Get the current fiber running on this kernel thread.
    Fiber* GetCurrentFiber() const;

private:
    // The main loop for each worker kernel thread.
    // Each kernel thread continuously picks ready fibers and executes them.
    void WorkerThreadLoop();

    // Switches execution from the current context (either scheduler or another fiber)
    // to the specified fiber.
    void SwitchToFiber(Fiber* fiber);

    // Switches execution from the current fiber back to the scheduler's context
    // on the current kernel thread. This is called by `Fiber::Yield()`.
    void SwitchToSchedulerContext();

    absl::Mutex mu_;                                     // Protects shared scheduler data (e.g., `ready_fibers_`).
    absl::Condition ready_fibers_cv_ ABSL_GUARDED_BY(mu_); // Condition variable for worker threads to wait for fibers.
    std::deque<Fiber*> ready_fibers_ ABSL_GUARDED_BY(mu_); // Queue of fibers that are ready to run.
    std::vector<std::thread> threads_;                   // Collection of worker kernel threads.
    std::atomic<bool> shutdown_;                         // Flag to signal scheduler shutdown.
    std::unordered_set<Fiber*> fibers_ ABSL_GUARDED_BY(mu_); // Set of all active fibers (for management/cleanup).

    // Map to store the `jmp_buf` for each worker thread.
    // This allows fibers to `longjmp` back to the correct worker thread's context
    // when they yield or block.
    std::map<std::thread::id, jmp_buf*> thread_contexts_;
};

#endif // FIBER_SCHEDULER_H_
