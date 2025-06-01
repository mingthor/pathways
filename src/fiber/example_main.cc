#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/log/globals.h"
#include "absl/base/log_severity.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "fiber_sync.h"   // For FiberMutex and FiberConditionVariable
#include "thread_pool.h"  // For ThreadPool and global_thread_pool
#include <execinfo.h> // For backtrace, backtrace_symbols
#include <signal.h>   // For signal, SIGSEGV
#include <iostream>
#include <cstdlib>    // For exit

// A fiber function that simulates a long-duration blocking operation.
// It uses absl::SleepFor, which is fiber-aware and will yield the fiber
// without blocking the underlying kernel thread.
void LongBlockingFiber(int id, absl::Duration block_duration) {
    LOG(ERROR) << "LongBlockingFiber " << id << " started. Will block for " << absl::FormatDuration(block_duration) << ".";
    // Simulate a blocking operation. This call will cause the fiber to yield
    // and be marked BLOCKED by the scheduler until the duration passes.
    absl::SleepFor(block_duration);
    LOG(ERROR) << "LongBlockingFiber " << id << " finished blocking.";
}

// A simple fiber function that demonstrates the use of FiberMutex and FiberConditionVariable.
void FiberWorker(int id, FiberMutex& mutex, FiberConditionVariable& cv, bool& shared_flag) {
    LOG(ERROR) << "Fiber " << id << " started.";

    // Example 1: Mutex usage
    mutex.Lock();
    LOG(ERROR) << "Fiber " << id << " acquired mutex.";
    absl::SleepFor(absl::Milliseconds(50)); // Simulate some work that takes time.
    mutex.Unlock();
    LOG(ERROR) << "Fiber " << id << " released mutex.";

    // Example 2: Condition variable usage
    mutex.Lock();
    while (!shared_flag) {
        LOG(ERROR) << "Fiber " << id << " waiting on condition variable.";
        cv.Wait(mutex);
        LOG(ERROR) << "Fiber " << id << " woke up from condition variable wait. Checking flag...";
    }
    LOG(ERROR) << "Fiber " << id << " condition met. Shared flag is true.";
    mutex.Unlock();

    LOG(ERROR) << "Fiber " << id << " finished.";
}

// Another fiber function that sets the shared flag and notifies waiting fibers.
void NotifierFiber(int id, FiberMutex& mutex, FiberConditionVariable& cv, bool& shared_flag) {
    LOG(ERROR) << "Notifier Fiber " << id << " started.";
    absl::SleepFor(absl::Milliseconds(500)); // Simulate some delay before notifying.

    mutex.Lock();
    shared_flag = true;
    LOG(ERROR) << "Notifier Fiber " << id << " set shared_flag to true.";
    cv.NotifyAll();
    mutex.Unlock();
    LOG(ERROR) << "Notifier Fiber " << id << " notified all waiting fibers.";

    LOG(ERROR) << "Notifier Fiber " << id << " finished.";
}

void signal_handler(int sig) {
    void *array[10];
    size_t size;

    // Get void*'s for all entries on the stack
    size = backtrace(array, 10);

    // Print all of the subroutines that were called
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

int main() {
    signal(SIGSEGV, signal_handler); // Register the signal handler for segmentation faults

    absl::InitializeLog();
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);

    LOG(ERROR) << "Starting Fiber ThreadPool demonstration with many long-blocking functions.";

    // Create a ThreadPool with 2 kernel threads.
    // This ThreadPool internally manages the FiberScheduler.
    global_thread_pool = std::make_unique<ThreadPool>(2);

    const int kNumFibers = 4;
    const absl::Duration kBlockDuration = absl::Minutes(1); // Each fiber "blocks" for 1 minute.

    LOG(ERROR) << "Scheduling " << kNumFibers << " fibers, each simulating a "
              << absl::FormatDuration(kBlockDuration) << " blocking operation.";

    // Schedule all 10,000 fibers.
    for (int i = 0; i < kNumFibers; ++i) {
        global_thread_pool->Schedule(
            [&, i]() { LongBlockingFiber(i, kBlockDuration); }
        );
    }

    LOG(ERROR) << "All " << kNumFibers << " fibers scheduled. Main thread waiting for completion.";

    // The main thread will wait for a very long time for all fibers to complete.
    // Total expected time: (kNumFibers * kBlockDuration) / num_kernel_threads
    // (10000 * 1 minute) / 2 threads = 5000 minutes = ~83.3 hours.
    // For demonstration, we'll use a shorter sleep, but in a real scenario,
    // you'd need a mechanism to wait for all scheduled tasks to truly complete.
    // For this example, we'll just let it run for a bit and observe logs.
    absl::SleepFor(absl::Seconds(10)); // Observe initial fiber scheduling and blocking.

    LOG(ERROR) << "Main thread continuing after initial sleep. Fibers will continue running in background.";
    LOG(ERROR) << "To observe full completion, this program would need to run for approximately "
              << absl::FormatDuration((kNumFibers * kBlockDuration) / 2) << ".";

    return 0;
}
