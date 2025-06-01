#ifndef FIBER_H_
#define FIBER_H_

#include <atomic>
#include <memory>
#include <ucontext.h>

#include "absl/functional/any_invocable.h"
#include "fiber_types.h"  // Includes FiberScheduler forward declaration and current_fiber extern

// --- 1. Fiber Class ---
// Represents a single user-level thread (fiber).
class Fiber {
 public:
  // Enum to describe the current state of the fiber.
  enum State {
    READY,      // The fiber is ready to be run by the scheduler.
    RUNNING,    // The fiber is currently executing on a kernel thread.
    BLOCKED,    // The fiber is waiting for an event (e.g., mutex, condition
                // variable, I/O) and has yielded control.
    TERMINATED  // The fiber has completed its execution.
  };

  // Constructor for a new fiber.
  // 'func': The function (now an absl::AnyInvocable) that this fiber will
  // execute. 'scheduler': A pointer to the global FiberScheduler instance,
  // allowing the
  //              fiber to interact with the scheduler (e.g., to yield).
  Fiber(absl::AnyInvocable<void()> func, FiberScheduler* scheduler);

  // Destructor: Cleans up the fiber's allocated stack memory.
  ~Fiber();

  // Get the fiber's unique integer ID.
  int id() const;

  // Get the fiber's current state (thread-safe read).
  State state() const;

  // Set the fiber's state (thread-safe write).
  void set_state(State s);

  // Get a pointer to the ucontext_t structure, which stores the fiber's
  // CPU context and stack information
  ucontext_t* context();

  // Get a pointer to the base of the fiber's dedicated stack.
  char* stack_base();

  // The actual entry point for the fiber. This function is called via makecontext.
  // It sets up the thread-local current_fiber and executes the user-provided function.
  void RunEntryPoint();

 private:
  int id_ = -1;  // Unique identifier for this fiber.
  static std::atomic<int>
      next_id_;  // Static atomic counter for generating unique IDs.
  std::atomic<State> state_;         // Current state of the fiber.
  absl::AnyInvocable<void()> func_;  // The user-defined function for this fiber
                                     // (now absl::AnyInvocable).
  ucontext_t context_;               // Changed from jmp_buf to ucontext_t
  FiberScheduler*
      scheduler_;  // Pointer back to the scheduler for interactions.
  std::unique_ptr<char[]>
      stack_;  // Unique pointer to the fiber's dedicated stack memory.
};

#endif  // FIBER_H_
