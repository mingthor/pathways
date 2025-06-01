#ifndef FIBER_SCHEDULER_H_
#define FIBER_SCHEDULER_H_

#include <atomic>
#include <deque>
#include <memory>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <ucontext.h>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "fiber_types.h"  // For Fiber forward declaration and jmp_buf

// --- 2. FiberScheduler Class ---
// Manages the lifecycle and scheduling of fibers on a fixed-size pool of kernel
// threads.
class FiberScheduler {
 public:
  // Constructor: Initializes the thread pool.
  // 'num_threads': The number of kernel threads that will execute fibers.
  explicit FiberScheduler(int num_threads);

  // Destructor: Shuts down the scheduler and waits for all worker threads to
  // finish.
  ~FiberScheduler();

  // Schedules a fiber to be run. Adds it to the ready queue.
  void Schedule(Fiber *fiber);

  // The currently running fiber explicitly yields control back to the
  // scheduler. This is the core mechanism for cooperative multitasking.
  void Yield();

  // Blocks the currently running fiber, moving it to a BLOCKED state.
  // The fiber will not be scheduled again until explicitly unblocked.
  // This is used by `FiberMutex` and `FiberConditionVariable` to put fibers to
  // sleep.
  void Block();

  // Unblocks a specific fiber, moving it back to the READY state and
  // scheduling it. This is called by `FiberMutex` or `FiberConditionVariable`
  // when an event they are waiting for occurs.
  void Unblock(Fiber *fiber);

  // Get the current fiber running on this kernel thread.
  Fiber *GetCurrentFiber() const;

 private:
  // Initialize a new fiber's context with its stack and entry point
  void InitializeFiberContext(Fiber *fiber) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // The main loop for each worker kernel thread.
  // Each kernel thread continuously picks ready fibers and executes them.
  void WorkerThreadLoop();

  // Switches execution from the current context (either scheduler or another
  // fiber) to the specified fiber.
  void SwitchToFiber(Fiber *fiber);

  // Switches execution from the current fiber back to the scheduler's context
  // on the current kernel thread. This is called by `Fiber::Yield()`.
  void SwitchToSchedulerContext();

  std::atomic<bool> shutdown_;
  absl::Mutex mu_;
  std::vector<std::thread> threads_;
  std::unordered_set<Fiber *> fibers_ ABSL_GUARDED_BY(mu_);
  std::deque<Fiber *> ready_fibers_ ABSL_GUARDED_BY(mu_);
  absl::CondVar ready_fibers_cv_;
  std::unordered_map<std::thread::id, ucontext_t *> thread_contexts_;  // Changed from jmp_buf to ucontext_t
};

#endif  // FIBER_SCHEDULER_H_
