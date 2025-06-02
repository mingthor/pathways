#include <gtest/gtest.h>
#include <ucontext.h>
#include <array>
#include <functional>
#include "absl/log/log.h"

namespace {

// Test fixture for context switching experiments
class ContextSwitchingTest : public ::testing::Test {
 protected:
  static constexpr size_t kStackSize = 64 * 1024;  // 64KB stacks
  
  void SetUp() override {
    // Initialize both contexts and their stacks
    stack1_ = std::make_unique<char[]>(kStackSize);
    stack2_ = std::make_unique<char[]>(kStackSize);
    
    // Zero-initialize contexts
    std::memset(&ctx1_, 0, sizeof(ctx1_));
    std::memset(&ctx2_, 0, sizeof(ctx2_));
    std::memset(&main_ctx_, 0, sizeof(main_ctx_));
  }

  // Helper to run the test with two functions that switch between each other
  void RunContextSwitch(std::function<void()> func1, std::function<void()> func2) {
    // Save the current context as main context
    getcontext(&main_ctx_);
    
    // Set up context 1
    getcontext(&ctx1_);
    ctx1_.uc_stack.ss_sp = stack1_.get();
    ctx1_.uc_stack.ss_size = kStackSize;
    ctx1_.uc_link = &main_ctx_;  // Return to main context when done
    
    // Set up context 2
    getcontext(&ctx2_);
    ctx2_.uc_stack.ss_sp = stack2_.get();
    ctx2_.uc_stack.ss_size = kStackSize;
    ctx2_.uc_link = &main_ctx_;  // Return to main context when done

    // Store the test fixture pointer to access from static functions
    current_test_ = this;
    
    // Set up the contexts with their respective functions
    makecontext(&ctx1_, reinterpret_cast<void(*)()>(&ContextSwitchingTest::StaticFunc1), 0);
    makecontext(&ctx2_, reinterpret_cast<void(*)()>(&ContextSwitchingTest::StaticFunc2), 0);

    // Store the functions to be executed
    func1_ = std::move(func1);
    func2_ = std::move(func2);

    // Start execution with context 1
    swapcontext(&main_ctx_, &ctx1_);
  }

  // Static functions that will be used with makecontext
  static void StaticFunc1() {
    current_test_->func1_();
    // Switch to context 2
    swapcontext(&current_test_->ctx1_, &current_test_->ctx2_);
  }

  static void StaticFunc2() {
    current_test_->func2_();
    // Switch back to context 1
    swapcontext(&current_test_->ctx2_, &current_test_->ctx1_);
  }

  ucontext_t ctx1_{};
  ucontext_t ctx2_{};
  ucontext_t main_ctx_{};
  std::unique_ptr<char[]> stack1_;
  std::unique_ptr<char[]> stack2_;
  std::function<void()> func1_;
  std::function<void()> func2_;
  
  static ContextSwitchingTest* current_test_;
};

ContextSwitchingTest* ContextSwitchingTest::current_test_ = nullptr;

TEST_F(ContextSwitchingTest, BasicContextSwitch) {
  int counter = 0;
  bool context1_ran = false;
  bool context2_ran = false;

  auto func1 = [&]() {
    context1_ran = true;
    counter++;
    LOG(ERROR) << "Function 1: counter = " << counter;
  };

  auto func2 = [&]() {
    context2_ran = true;
    counter++;
    LOG(ERROR) << "Function 2: counter = " << counter;
  };

  RunContextSwitch(func1, func2);

  EXPECT_TRUE(context1_ran) << "Context 1 did not run";
  EXPECT_TRUE(context2_ran) << "Context 2 did not run";
  EXPECT_EQ(counter, 2) << "Both contexts should increment counter once";
}

TEST_F(ContextSwitchingTest, MultipleSwitches) {
  std::vector<int> execution_order;

  auto func1 = [&]() {
    for (int i = 0; i < 3; i++) {
      execution_order.push_back(1);
      LOG(ERROR) << "Function 1: iteration " << i;
      swapcontext(&ctx1_, &ctx2_);
    }
  };

  auto func2 = [&]() {
    for (int i = 0; i < 3; i++) {
      execution_order.push_back(2);
      LOG(ERROR) << "Function 2: iteration " << i;
      swapcontext(&ctx2_, &ctx1_);
    }
  };

  RunContextSwitch(func1, func2);

  std::vector<int> expected = {1, 2, 1, 2, 1, 2};
  EXPECT_EQ(execution_order, expected) << "Context switches did not alternate as expected";
}

}  // namespace
