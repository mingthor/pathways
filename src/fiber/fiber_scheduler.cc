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
  // Only initialize context for new fibers
  if (fibers_.insert(fiber)
          .second) {  // Returns true if fiber is newly inserted
    InitializeFiberContext(fiber);
  }
  ready_fibers_.push_back(fiber);
  fiber->set_state(Fiber::READY);
  ready_fibers_cv_.Signal();
  LOG(ERROR) << "Scheduled Fiber " << fiber->id()
             << ", state: READY. Ready queue size: " << ready_fibers_.size();
}

void FiberScheduler::Yield() {
  LOG(ERROR) << "Yield called from FiberScheduler.";
  Fiber *caller_fiber = current_fiber;
  if (caller_fiber == nullptr) {
    LOG(ERROR) << "Yield called from non-fiber context!";
    return;
  }

  LOG(ERROR) << "Fiber " << caller_fiber->id() << " yielding. Caller fiber state: "
             << caller_fiber->state();

  if (caller_fiber->state() == Fiber::RUNNING) {
    caller_fiber->set_state(Fiber::READY);
    Schedule(caller_fiber);
  }
  SwitchToSchedulerContext();
  
  LOG(ERROR) << "Fiber " << caller_fiber->id() << " resumed.";
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

void FiberScheduler::InitializeFiberContext(Fiber *fiber) {
  // Get the stack base
  char *stack_base = fiber->stack_base();
  
  // Initialize the context
  ucontext_t* ctx = fiber->context();
  getcontext(ctx);  // Initialize ctx with current context

  // Set up the new context's stack
  ctx->uc_stack.ss_sp = stack_base;
  ctx->uc_stack.ss_size = kFiberStackSize;
  ctx->uc_stack.ss_flags = 0;
  ctx->uc_link = nullptr;  // No link context - fiber will call Yield when done

  // Set up the entry point function
  makecontext(ctx, 
              (void (*)())&Fiber::RunEntryPoint,  // Entry point
              0);  // No arguments

  LOG(ERROR) << "Initialized context for Fiber " << fiber->id();
}

void FiberScheduler::WorkerThreadLoop() {
  auto thread_context = std::make_unique<ucontext_t>();
  getcontext(thread_context.get());
  thread_contexts_[std::this_thread::get_id()] = thread_context.get();

  LOG(ERROR) << "Worker thread " << std::this_thread::get_id() << " started.";

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

  LOG(ERROR) << "Worker thread " << std::this_thread::get_id() << " exiting.";
}

void FiberScheduler::SwitchToFiber(Fiber *fiber) {
  current_fiber = fiber;
  fiber->set_state(Fiber::RUNNING);
  
  ucontext_t* current_ctx = thread_contexts_[std::this_thread::get_id()];
  
  LOG(ERROR) << "Switching to Fiber " << fiber->id() << ".";
  swapcontext(current_ctx, fiber->context());
  LOG(ERROR) << "Switched back from Fiber " << fiber->id() << ".";
}

void FiberScheduler::SwitchToSchedulerContext() {
  ucontext_t* thread_ctx = thread_contexts_[std::this_thread::get_id()];
  if (thread_ctx == nullptr) {
    LOG(FATAL) << "Scheduler context not found for current thread!";
  }
  
  LOG(ERROR) << "Switching back to scheduler context.";
  Fiber* fiber = current_fiber;
  current_fiber = nullptr;
  swapcontext(fiber->context(), thread_ctx);
}
