#include "fiber.h"

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "fiber_scheduler.h"
#include "fiber_sync.h"
#include "gtest/gtest.h"
#include "thread_pool.h"

// Re-declare the global_thread_pool for use in tests.
// It will be initialized in the test fixture's SetUp.
// extern std::unique_ptr<ThreadPool> global_thread_pool;

namespace {

// A test fixture to manage the ThreadPool lifecycle for each test.
class FiberSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize the global ThreadPool before each test.
    // Using 2 kernel threads for most tests to simulate concurrency.
    global_thread_pool = std::make_unique<ThreadPool>(2);
    LOG(ERROR) << "ThreadPool initialized for test.";
  }

  void TearDown() override {
    // Shut down and destroy the global ThreadPool after each test.
    // This ensures all fibers are cleaned up and kernel threads are joined.
    LOG(ERROR) << "ThreadPool shutting down for test.";
    global_thread_pool.reset();  // Calls destructor, which handles shutdown.
    LOG(ERROR) << "ThreadPool shut down.";
  }

  // Helper to wait for a condition in the test thread.
  // This is a kernel-thread-level wait, not a fiber-level wait.
  void WaitForCondition(absl::Mutex& mu, absl::CondVar& cv,
                        const std::function<bool()>& condition) {
    absl::MutexLock lock(&mu);
    while (!condition()) {
      cv.Wait(&mu);
    }
  }
  std::unique_ptr<ThreadPool> global_thread_pool;
};

// --- Fiber Class Tests ---

TEST_F(FiberSystemTest, FiberCreationAndDestruction) {
  bool fiber_ran = false;
  {
    // FiberScheduler is owned by global_thread_pool.
    // We need to get a raw pointer to it for Fiber construction.
    FiberScheduler* scheduler = global_thread_pool->GetInternalScheduler();
    Fiber fiber([&]() { fiber_ran = true; }, scheduler);

    EXPECT_EQ(fiber.state(), Fiber::READY);
    EXPECT_EQ(fiber.id(), 0);  // IDs should start from 0 and increment.
    // Don't schedule it, just test creation/destruction.
  }
  EXPECT_FALSE(fiber_ran);  // Should not have run if not scheduled.
}

TEST_F(FiberSystemTest, FiberRunEntryPointSetsState) {
  absl::Mutex mu;
  absl::CondVar cv;
  bool fiber_finished = false;

  // FiberScheduler is owned by global_thread_pool.
  FiberScheduler* scheduler = global_thread_pool->GetInternalScheduler();
  Fiber fiber(
      [&]() {
        LOG(ERROR) << "Fiber started running.";
        // The fiber's own RunEntryPoint will set its state to RUNNING.
        // After func_() returns, it will set state to TERMINATED.
        for (int i = 0; i < 5; ++i) {
          LOG(ERROR) << "Fiber running iteration " << i;
          absl::SleepFor(absl::Milliseconds(10));  // Simulate work
          scheduler->Yield();  // Yield to allow other fibers to run
        }

        absl::MutexLock lock(&mu);
        fiber_finished = true;
        cv.Signal();
      },
      scheduler);

  // Schedule the fiber.
  scheduler->Schedule(&fiber);

  // Wait for the fiber to finish.
  WaitForCondition(mu, cv, [&]() { return fiber_finished; });

  // Give a small moment for scheduler to process termination.
  absl::SleepFor(absl::Milliseconds(10));

  EXPECT_EQ(fiber.state(), Fiber::TERMINATED);
}

// --- FiberScheduler Tests ---

TEST_F(FiberSystemTest, ScheduleBasicFiber) {
  absl::Mutex mu;
  absl::CondVar cv;
  bool fiber_executed = false;

  global_thread_pool->Schedule([&]() {
    LOG(ERROR) << "Basic fiber task executed.";
    absl::MutexLock lock(&mu);
    fiber_executed = true;
    cv.Signal();
  });

  WaitForCondition(mu, cv, [&]() { return fiber_executed; });
  EXPECT_TRUE(fiber_executed);
}

TEST_F(FiberSystemTest, FiberYieldsAndResumes) {
  absl::Mutex mu;
  absl::CondVar cv;
  int step = 0;

  global_thread_pool->Schedule([&]() {
    LOG(ERROR) << "Yielding fiber: Step 1";
    absl::MutexLock lock(&mu);
    step = 1;
    cv.Signal();
    // Yield will put the fiber back to READY and scheduler will pick another.
    // When it resumes, it continues from here.
    global_thread_pool->GetInternalScheduler()->Yield();
    LOG(ERROR) << "Yielding fiber: Step 2 (resumed)";
    absl::MutexLock lock2(&mu);
    step = 2;
    cv.Signal();
  });

  // Wait for step 1.
  WaitForCondition(mu, cv, [&]() { return step == 1; });
  EXPECT_EQ(step, 1);

  // Give scheduler a moment to pick another fiber if available, then wait for
  // step 2. The scheduler will eventually pick up the yielded fiber again.
  WaitForCondition(mu, cv, [&]() { return step == 2; });
  EXPECT_EQ(step, 2);
}

TEST_F(FiberSystemTest, MultipleFibersRunConcurrently) {
  absl::Mutex mu;
  absl::CondVar cv;
  std::atomic<int> completed_fibers = 0;
  const int num_test_fibers = 10;  // More than 2 kernel threads.

  for (int i = 0; i < num_test_fibers; ++i) {
    global_thread_pool->Schedule([&, i]() {
      LOG(ERROR) << "Concurrent fiber " << i << " started.";
      absl::SleepFor(absl::Milliseconds(50));  // Simulate some work/blocking
      LOG(ERROR) << "Concurrent fiber " << i << " finished.";
      completed_fibers.fetch_add(1);
      absl::MutexLock lock(&mu);
      cv.Signal();  // Signal completion
    });
  }

  // Wait until all fibers have completed.
  WaitForCondition(
      mu, cv, [&]() { return completed_fibers.load() == num_test_fibers; });
  EXPECT_EQ(completed_fibers.load(), num_test_fibers);
}

TEST_F(FiberSystemTest, SchedulerShutdownCleansUp) {
  absl::Mutex mu;
  absl::CondVar cv;
  std::atomic<int> active_fibers = 0;

  // Schedule a fiber that will run for a while.
  global_thread_pool->Schedule([&]() {
    active_fibers.store(1);
    LOG(ERROR) << "Long-running fiber started.";
    absl::SleepFor(absl::Seconds(
        1));  // This fiber will be active when TearDown is called.
    LOG(ERROR) << "Long-running fiber finished.";
    active_fibers.store(0);
    absl::MutexLock lock(&mu);
    cv.Signal();
  });

  // Give the fiber a moment to start.
  WaitForCondition(mu, cv, [&]() { return active_fibers.load() == 1; });
  EXPECT_EQ(active_fibers.load(), 1);

  // TearDown will be called automatically, which should shut down the
  // ThreadPool and thus the FiberScheduler, ensuring threads are joined and
  // fibers cleaned. We don't explicitly call TearDown here, it's part of the
  // test fixture.
}

// --- FiberMutex Tests ---

TEST_F(FiberSystemTest, FiberMutexBasicLockUnlock) {
  FiberMutex mutex;
  absl::Mutex mu;
  absl::CondVar cv;
  std::atomic<int> counter = 0;
  const int num_increments = 100;

  global_thread_pool->Schedule([&]() {
    for (int i = 0; i < num_increments; ++i) {
      mutex.Lock();
      counter.fetch_add(1);  // Critical section
      mutex.Unlock();
    }
    absl::MutexLock lock(&mu);
    cv.Signal();
  });

  WaitForCondition(mu, cv, [&]() { return counter.load() == num_increments; });
  EXPECT_EQ(counter.load(), num_increments);
}

TEST_F(FiberSystemTest, FiberMutexContention) {
  FiberMutex mutex;
  absl::Mutex mu;
  absl::CondVar cv;
  std::atomic<int> shared_value = 0;
  std::atomic<int> completed_fibers = 0;
  const int num_fibers = 5;
  const int increments_per_fiber = 10;

  for (int i = 0; i < num_fibers; ++i) {
    global_thread_pool->Schedule([&]() {
      for (int j = 0; j < increments_per_fiber; ++j) {
        mutex.Lock();
        int current_val = shared_value.load();
        // Simulate some work while holding the lock
        absl::SleepFor(absl::Microseconds(10));
        shared_value.store(current_val + 1);
        mutex.Unlock();
      }
      completed_fibers.fetch_add(1);
      absl::MutexLock lock(&mu);
      cv.Signal();
    });
  }

  WaitForCondition(mu, cv,
                   [&]() { return completed_fibers.load() == num_fibers; });
  EXPECT_EQ(shared_value.load(), num_fibers * increments_per_fiber);
}

// --- FiberConditionVariable Tests ---

TEST_F(FiberSystemTest, FiberConditionVariableNotifyOne) {
  FiberMutex mutex;
  FiberConditionVariable cv_fiber;
  absl::Mutex mu_test;
  absl::CondVar cv_test;
  bool condition_met = false;
  std::atomic<int> notified_count = 0;

  // Waiting fiber
  global_thread_pool->Schedule([&]() {
    mutex.Lock();
    LOG(ERROR) << "Waiting fiber: About to wait on CV.";
    cv_fiber.Wait(mutex);  // Releases mutex, blocks fiber, re-acquires.
    LOG(ERROR) << "Waiting fiber: Woke up from CV wait.";
    notified_count.fetch_add(1);
    mutex.Unlock();
    absl::MutexLock lock(&mu_test);
    cv_test.Signal();  // Signal test framework
  });

  // Notifier fiber
  global_thread_pool->Schedule([&]() {
    absl::SleepFor(
        absl::Milliseconds(100));  // Give waiting fiber time to block
    mutex.Lock();
    LOG(ERROR) << "Notifier fiber: Setting condition and notifying.";
    condition_met = true;
    cv_fiber.NotifyOne();  // Notify one waiting fiber
    mutex.Unlock();
  });

  WaitForCondition(mu_test, cv_test,
                   [&]() { return notified_count.load() == 1; });
  EXPECT_TRUE(condition_met);
  EXPECT_EQ(notified_count.load(), 1);
}

TEST_F(FiberSystemTest, FiberConditionVariableNotifyAll) {
  FiberMutex mutex;
  FiberConditionVariable cv_fiber;
  absl::Mutex mu_test;
  absl::CondVar cv_test;
  bool condition_met = false;
  std::atomic<int> notified_count = 0;
  const int num_waiting_fibers = 5;

  for (int i = 0; i < num_waiting_fibers; ++i) {
    global_thread_pool->Schedule([&]() {
      mutex.Lock();
      // LOG(ERROR) << "Waiting fiber " <<
      // FiberScheduler::GetCurrentFiber()->id() << ": About to wait on CV.";
      cv_fiber.Wait(mutex);
      // LOG(ERROR) << "Waiting fiber " <<
      // FiberScheduler::GetCurrentFiber()->id() << ": Woke up from CV wait.";
      notified_count.fetch_add(1);
      mutex.Unlock();
      absl::MutexLock lock(&mu_test);
      cv_test.Signal();
    });
  }

  // Notifier fiber
  global_thread_pool->Schedule([&]() {
    absl::SleepFor(
        absl::Milliseconds(200));  // Give waiting fibers time to block
    mutex.Lock();
    LOG(ERROR) << "Notifier fiber: Setting condition and notifying all.";
    condition_met = true;
    cv_fiber.NotifyAll();  // Notify all waiting fibers
    mutex.Unlock();
  });

  WaitForCondition(mu_test, cv_test, [&]() {
    return notified_count.load() == num_waiting_fibers;
  });
  EXPECT_TRUE(condition_met);
  EXPECT_EQ(notified_count.load(), num_waiting_fibers);
}

// --- ThreadPool Tests ---

TEST_F(FiberSystemTest, ThreadPoolSchedulesAbseilInvocable) {
  absl::Mutex mu;
  absl::CondVar cv;
  bool task_executed = false;

  global_thread_pool->Schedule(absl::AnyInvocable<void()>([&]() {
    LOG(ERROR) << "ThreadPool scheduled task executed.";
    absl::MutexLock lock(&mu);
    task_executed = true;
    cv.Signal();
  }));

  WaitForCondition(mu, cv, [&]() { return task_executed; });
  EXPECT_TRUE(task_executed);
}

TEST_F(FiberSystemTest, ThreadPoolHandlesBlockingTasks) {
  absl::Mutex mu;
  absl::CondVar cv;
  std::atomic<int> completed_tasks = 0;
  const int num_tasks = 4;  // More than 2 kernel threads.
  const absl::Duration task_duration = absl::Milliseconds(100);

  for (int i = 0; i < num_tasks; ++i) {
    global_thread_pool->Schedule([&, i]() {
      LOG(ERROR) << "ThreadPool task " << i << " started blocking for "
                 << absl::FormatDuration(task_duration);
      absl::SleepFor(task_duration);  // This is a fiber-aware sleep.
      LOG(ERROR) << "ThreadPool task " << i << " finished blocking.";
      completed_tasks.fetch_add(1);
      absl::MutexLock lock(&mu);
      cv.Signal();
    });
  }

  WaitForCondition(mu, cv,
                   [&]() { return completed_tasks.load() == num_tasks; });
  EXPECT_EQ(completed_tasks.load(), num_tasks);
}

}  // namespace
