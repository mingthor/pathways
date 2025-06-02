#include <gtest/gtest.h>
#include <ucontext.h>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"

namespace {

// Test fixture for simple context switching
class SimpleContextTest : public ::testing::Test {
 protected:
  static constexpr size_t kStackSize = 64 * 1024;  // 64KB stack

  void SetUp() override {
    // Initialize context and stack
    stack_ = std::make_unique<char[]>(kStackSize);

    // Zero-initialize contexts
    std::memset(&ctx_, 0, sizeof(ctx_));
    std::memset(&main_ctx_, 0, sizeof(main_ctx_));
  }

  void RunContextSwitch(absl::AnyInvocable<void()> func) {
    // Save the current context as main context
    getcontext(&main_ctx_);

    // Set up the new context
    getcontext(&ctx_);
    ctx_.uc_stack.ss_sp = stack_.get();
    ctx_.uc_stack.ss_size = kStackSize;
    ctx_.uc_link =
        nullptr;  // We'll use explicit swapcontext, so no link needed

    // Store the function
    func_ = std::move(func);

    // Create a lambda that captures 'this' to avoid static member
    auto entry_point = [](void* arg) {
      auto self = static_cast<SimpleContextTest*>(arg);
      self->func_();
      // Switch back to main context when done
      swapcontext(&self->ctx_, &self->main_ctx_);
    };

    // Set up the context with the entry function and pass 'this' as argument
    makecontext(&ctx_, reinterpret_cast<void (*)()>(+entry_point),
                1,      // One argument
                this);  // Pass this pointer as argument

    // Switch to the new context
    swapcontext(&main_ctx_, &ctx_);
  }

  ucontext_t ctx_{};
  ucontext_t main_ctx_{};
  std::unique_ptr<char[]> stack_;
  absl::AnyInvocable<void()> func_;
};

TEST_F(SimpleContextTest, BasicContextSwitch) {
  int counter = 0;
  bool function_ran = false;

  auto func = [&]() {
    function_ran = true;
    counter++;
    LOG(ERROR) << "Function executing in new context, counter = " << counter;

    // We can switch back and forth multiple times
    swapcontext(&ctx_, &main_ctx_);  // Switch to main

    counter++;
    LOG(ERROR) << "Function resumed in new context, counter = " << counter;
  };

  RunContextSwitch(func);
  LOG(ERROR) << "Back in main context, counter = " << counter;

  // We can switch back to the function context
  swapcontext(&main_ctx_, &ctx_);
  LOG(ERROR) << "Back in main context final time, counter = " << counter;

  EXPECT_TRUE(function_ran) << "Function did not run";
  EXPECT_EQ(counter, 2) << "Counter should be incremented twice";
}

}  // namespace
