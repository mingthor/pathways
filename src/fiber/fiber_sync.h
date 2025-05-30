#ifndef FIBER_SYNC_H_
#define FIBER_SYNC_H_

#include <deque>
#include "absl/base/thread_annotations.h" // For ABSL_GUARDED_BY
#include "absl/synchronization/mutex.h"
#include "fiber_types.h" // For FiberScheduler and Fiber forward declarations

// --- 3. FiberMutex Class ---
// A mutex that blocks (yields) fibers instead of kernel threads.
class FiberMutex {
public:
    FiberMutex();

    // Acquires the mutex. If locked, the current fiber blocks.
    void Lock();

    // Releases the mutex. If there are waiting fibers, one is unblocked.
    void Unlock();

private:
    // Helper to get the global scheduler instance.
    FiberScheduler* GetScheduler();

    absl::Mutex mu_;                            // Protects internal state of FiberMutex
    bool locked_ ABSL_GUARDED_BY(mu_);          // True if the mutex is currently held
    std::deque<Fiber*> waiting_fibers_ ABSL_GUARDED_BY(mu_); // Fibers waiting for this mutex
};

// --- 4. FiberConditionVariable Class ---
// A condition variable that blocks (yields) fibers instead of kernel threads.
class FiberConditionVariable {
public:
    // Waits on the condition variable. Releases the mutex, blocks the fiber,
    // and re-acquires the mutex when unblocked.
    void Wait(FiberMutex& mutex);

    // Notifies one waiting fiber.
    void NotifyOne();

    // Notifies all waiting fibers.
    void NotifyAll();

private:
    // Helper to get the global scheduler instance.
    FiberScheduler* GetScheduler();

    absl::Mutex mu_;                            // Protects internal state
    std::deque<Fiber*> waiting_fibers_ ABSL_GUARDED_BY(mu_); // Fibers waiting on this condition
};

#endif // FIBER_SYNC_H_
