#ifndef FIBER_TYPES_H_
#define FIBER_TYPES_H_

#include <setjmp.h> // For jmp_buf

// Forward declarations to avoid circular dependencies and minimize includes.
class Fiber;
class FiberScheduler;
class FiberMutex;
class FiberConditionVariable;

// Thread-local storage for the currently running fiber on a kernel thread.
// This is crucial for fibers to interact with the scheduler (e.g., to yield or block).
extern thread_local class Fiber* current_fiber;

// Define a small stack size for demonstration.
constexpr size_t kFiberStackSize = 128 * 1024;  // 128 KB

#endif // FIBER_TYPES_H_
