#include "fiber_sync.h"

#include "absl/log/log.h"
#include "fiber.h"           // For Fiber class and State enum
#include "fiber_scheduler.h" // For FiberScheduler methods
#include "thread_pool.h"     // For global_thread_pool access

// --- FiberMutex Implementation ---

FiberMutex::FiberMutex() : locked_(false) {}

void FiberMutex::Lock() {
    FiberScheduler* scheduler = GetScheduler();
    Fiber* current = scheduler->GetCurrentFiber();
    if (current == nullptr) {
        LOG(FATAL) << "FiberMutex::Lock called from non-fiber context!";
    }

    absl::MutexLock lock(&mu_);
    if (locked_) {
        LOG(INFO) << "Fiber " << current->id() << " trying to acquire locked FiberMutex. Blocking...";
        waiting_fibers_.push_back(current);
        scheduler->Block();
    }
    locked_ = true;
    LOG(INFO) << "Fiber " << current->id() << " acquired FiberMutex.";
}

void FiberMutex::Unlock() {
    FiberScheduler* scheduler = GetScheduler();
    Fiber* current = scheduler->GetCurrentFiber();
    if (current == nullptr) {
        LOG(FATAL) << "FiberMutex::Unlock called from non-fiber context!";
    }

    absl::MutexLock lock(&mu_);
    if (!locked_) {
        LOG(WARNING) << "Fiber " << current->id() << " unlocking an unlocked FiberMutex!";
        return;
    }
    locked_ = false;
    LOG(INFO) << "Fiber " << current->id() << " released FiberMutex.";

    if (!waiting_fibers_.empty()) {
        Fiber* next_fiber = waiting_fibers_.front();
        waiting_fibers_.pop_front();
        scheduler->Unblock(next_fiber);
        LOG(INFO) << "FiberMutex unblocked Fiber " << next_fiber->id() << ".";
    }
}

// --- FiberConditionVariable Implementation ---

void FiberConditionVariable::Wait(FiberMutex& mutex) {
    FiberScheduler* scheduler = GetScheduler();
    Fiber* current = scheduler->GetCurrentFiber();
    if (current == nullptr) {
        LOG(FATAL) << "FiberConditionVariable::Wait called from non-fiber context!";
    }

    LOG(INFO) << "Fiber " << current->id() << " waiting on FiberConditionVariable.";

    {
        absl::MutexLock lock(&mu_);
        waiting_fibers_.push_back(current);
    }
    mutex.Unlock();

    scheduler->Block();

    mutex.Lock();
    LOG(INFO) << "Fiber " << current->id() << " resumed from FiberConditionVariable wait.";
}

void FiberConditionVariable::NotifyOne() {
    FiberScheduler* scheduler = GetScheduler();
    absl::MutexLock lock(&mu_);
    if (!waiting_fibers_.empty()) {
        Fiber* fiber_to_unblock = waiting_fibers_.front();
        waiting_fibers_.pop_front();
        scheduler->Unblock(fiber_to_unblock);
        LOG(INFO) << "FiberConditionVariable notified one fiber: " << fiber_to_unblock->id();
    }
}

void FiberConditionVariable::NotifyAll() {
    FiberScheduler* scheduler = GetScheduler();
    absl::MutexLock lock(&mu_);
    while (!waiting_fibers_.empty()) {
        Fiber* fiber_to_unblock = waiting_fibers_.front();
        waiting_fibers_.pop_front();
        scheduler->Unblock(fiber_to_unblock);
        LOG(INFO) << "FiberConditionVariable notified all fibers: " << fiber_to_unblock->id();
    }
}

// Helper function definitions for getting the global scheduler.
// These need to be defined after `global_thread_pool` is declared in `thread_pool.h`.
// We include "thread_pool.h" here to get access to `global_thread_pool`.
FiberScheduler* FiberMutex::GetScheduler() {
    if (!global_thread_pool) {
        LOG(FATAL) << "global_thread_pool is not initialized when GetScheduler() is called!";
    }
    return global_thread_pool->GetInternalScheduler();
}

FiberScheduler* FiberConditionVariable::GetScheduler() {
    if (!global_thread_pool) {
        LOG(FATAL) << "global_thread_pool is not initialized when GetScheduler() is called!";
    }
    return global_thread_pool->GetInternalScheduler();
}
