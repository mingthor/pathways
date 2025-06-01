#include "fiber_scheduler.h"

#include "absl/log/log.h"
#include "fiber.h"  // For Fiber class methods and State enum

FiberScheduler::FiberScheduler(int num_threads) : shutdown_(false) {
  LOG(ERROR) << "FiberScheduler initializing with " << num_threads
             << " threads.";
  for (int i = 0; i < num_threads; ++i) {
    threads_.emplace_back(&FiberScheduler::WorkerThreadLoop, this);
  }
}

FiberScheduler::~FiberScheduler() {
  LOG(ERROR) << "FiberScheduler shutting down.";
  shutdown_.store(true);
  {
    absl::MutexLock lock(&mu_);
    ready_fibers_cv_.SignalAll();
  }
  for (std::thread &t : threads_) {
    if (t.joinable()) {
      t.join();
    }
  }
  LOG(ERROR) << "FiberScheduler shutdown complete. Remaining fibers: "
             << fibers_.size();
  for (Fiber *f : fibers_) {
    delete f;
  }
}

void FiberScheduler::Schedule(Fiber *fiber) {
  absl::MutexLock lock(&mu_);
  fibers_.insert(fiber);
  ready_fibers_.push_back(fiber);
  fiber->set_state(Fiber::READY);
  ready_fibers_cv_.Signal();
  LOG(ERROR) << "Scheduled Fiber " << fiber->id()
             << ", state: READY. Ready queue size: " << ready_fibers_.size();
}

void FiberScheduler::Yield() {
  Fiber *caller_fiber = current_fiber;
  if (caller_fiber == nullptr) {
    LOG(ERROR) << "Yield called from non-fiber context!";
    return;
  }

  LOG(ERROR) << "Fiber " << caller_fiber->id() << " yielding.";

  if (setjmp(*caller_fiber->context()) == 0) {
    if (caller_fiber->state() == Fiber::RUNNING) {
      caller_fiber->set_state(Fiber::READY);
      Schedule(caller_fiber);
    }
    SwitchToSchedulerContext();
  } else {
    LOG(ERROR) << "Fiber " << caller_fiber->id() << " resumed.";
  }
}

void FiberScheduler::Block() {
  Fiber *caller_fiber = current_fiber;
  if (caller_fiber == nullptr) {
    LOG(ERROR) << "Block called from non-fiber context!";
    return;
  }
  LOG(ERROR) << "Fiber " << caller_fiber->id() << " blocking.";
  caller_fiber->set_state(Fiber::BLOCKED);
  Yield();
}

void FiberScheduler::Unblock(Fiber *fiber) {
  if (fiber == nullptr) return;
  LOG(ERROR) << "Unblocking Fiber " << fiber->id() << ".";
  Schedule(fiber);
}

Fiber *FiberScheduler::GetCurrentFiber() const { return current_fiber; }

void FiberScheduler::WorkerThreadLoop() {
  jmp_buf thread_context;
  thread_contexts_[std::this_thread::get_id()] = &thread_context;

  LOG(ERROR) << "Worker thread " << std::this_thread::get_id() << " started.";

  if (setjmp(thread_context) == 0) {
    while (!shutdown_.load()) {
      Fiber *fiber_to_run = nullptr;
      {
        absl::MutexLock lock(&mu_);
        while (ready_fibers_.empty() && !shutdown_.load()) {
          ready_fibers_cv_.Wait(&mu_);
        }
        if (shutdown_.load()) {
          break;
        }
        fiber_to_run = ready_fibers_.front();
        ready_fibers_.pop_front();
      }

      if (fiber_to_run) {
        LOG(ERROR) << "Worker thread " << std::this_thread::get_id()
                   << " picked Fiber " << fiber_to_run->id()
                   << ". State: " << fiber_to_run->state();
        SwitchToFiber(fiber_to_run);
      }
    }
  } else {
    // This else block is where fibers longjmp back to when they
    // yield/block/terminate. The worker thread will then loop back to the
    // `while` condition.
  }
  LOG(ERROR) << "Worker thread " << std::this_thread::get_id() << " exiting.";
}

void FiberScheduler::SwitchToFiber(Fiber *fiber) {
  current_fiber = fiber;
  fiber->set_state(Fiber::RUNNING);

  LOG(ERROR) << "Switching to Fiber " << fiber->id() << ".";
  longjmp(*fiber->context(), 1);
  LOG(ERROR) << "Switched to Fiber " << fiber->id() << ".";
}

void FiberScheduler::SwitchToSchedulerContext() {
  jmp_buf *thread_ctx = thread_contexts_[std::this_thread::get_id()];
  if (thread_ctx == nullptr) {
    LOG(FATAL) << "Scheduler context not found for current thread!";
  }
  LOG(ERROR) << "Switching back to scheduler context.";
  current_fiber = nullptr;
  longjmp(*thread_ctx, 1);
}
