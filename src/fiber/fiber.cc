#include "fiber.h"

#include "absl/log/log.h"     // For logging
#include "fiber_scheduler.h"  // For FiberScheduler methods

// Define the static atomic counter for fiber IDs.
std::atomic<int> Fiber::next_id_ = 0;

// Define the thread_local current_fiber.
thread_local class Fiber *current_fiber = nullptr;

Fiber::Fiber(absl::AnyInvocable<void()> func, FiberScheduler *scheduler)
    : id_(next_id_.fetch_add(1)),
      state_(READY),
      func_(std::move(func)),
      scheduler_(scheduler),
      stack_(std::make_unique<char[]>(kFiberStackSize)) {
  LOG(ERROR) << "Fiber " << id_ << " created.";
}

Fiber::~Fiber() { LOG(ERROR) << "Fiber " << id_ << " destroyed."; }

int Fiber::id() const { return id_; }

Fiber::State Fiber::state() const { return state_.load(); }

void Fiber::set_state(State s) { state_.store(s); }

ucontext_t *Fiber::context() { return &context_; }

char *Fiber::stack_base() { return stack_.get(); }

void Fiber::RunEntryPoint() {
  current_fiber = this;
  state_ = RUNNING;

  LOG(ERROR) << "Fiber " << id_ << " entering RunEntryPoint.";
  
  try {
    // Execute the user's function exactly once
    func_();  // The moved AnyInvocable is valid here
  } catch (const std::exception& e) {
    LOG(ERROR) << "Fiber " << id_ << " threw exception: " << e.what();
  }

  state_ = TERMINATED;
  LOG(ERROR) << "Fiber " << id_ << " finished, state: TERMINATED.";

  // Final yield to let scheduler know we're done
  scheduler_->Yield();
}
