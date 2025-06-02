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
    ctx_.uc_link = nullptr;  // We'll use explicit swapcontext, so no link needed
    
    // Store the test fixture pointer and function
    current_test_ = this;
    func_ = std::move(func);
    
    // Set up the context with the entry function
    makecontext(&ctx_, reinterpret_cast<void(*)()>(&SimpleContextTest::StaticFunc), 0);

    // Switch to the new context
    swapcontext(&main_ctx_, &ctx_);
  }

  // Static function that will be used with makecontext
  static void StaticFunc() {
    current_test_->func_();
    // Switch back to main context when done
    swapcontext(&current_test_->ctx_, &current_test_->main_ctx_);
  }

  ucontext_t ctx_{};
  ucontext_t main_ctx_{};
  std::unique_ptr<char[]> stack_;
  absl::AnyInvocable<void()> func_;
  
  static SimpleContextTest* current_test_;
};

SimpleContextTest* SimpleContextTest::current_test_ = nullptr;

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
