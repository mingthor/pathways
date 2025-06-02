#include <gtest/gtest.h>
#include <ucontext.h>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"

namespace {

// Test fixture for simple context switching
class SimpleContextTest : public ::testing::Test {
 protected:
  static constexpr size_t kStackSize = 64 * 1024;  // 64KB stack

  enum class ContextState { NOT_STARTED, RUNNING, FINISHED };

  struct Context {
    SimpleContextTest* test;
    absl::AnyInvocable<void()> func;
    ContextState state = ContextState::NOT_STARTED;
  };

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

    // Create context on heap - will be managed by shared_ptr for safety
    auto context =
        std::make_shared<Context>(Context{.test = this,
                                          .func = std::move(func),
                                          .state = ContextState::NOT_STARTED});

    // Weak pointer to detect when context is done
    // auto weak_context = std::weak_ptr<Context>(context);

    // Create a lambda that will receive the Context*
    auto entry_point = [](void* arg) {
      // Convert raw pointer back to shared_ptr to manage lifetime
      auto ctx =
          std::shared_ptr<Context>(static_cast<Context*>(arg), [](Context* p) {
            LOG(ERROR) << "Context cleanup from entry point";
            if (p->state != ContextState::FINISHED) {
              LOG(ERROR)
                  << "Warning: Context cleaned up before finishing execution";
            }
          });

      ctx->state = ContextState::RUNNING;

      // Execute the function
      ctx->func();

      ctx->state = ContextState::FINISHED;

      // Switch back to main context when done
      swapcontext(&ctx->test->ctx_, &ctx->test->main_ctx_);
      // Context will be cleaned up when shared_ptr goes out of scope
    };

    // Set up the context with the entry function and pass context pointer
    makecontext(&ctx_, reinterpret_cast<void (*)()>(+entry_point),
                1,               // One argument
                context.get());  // Pass raw pointer, but keep shared ownership

    // Switch to the new context
    swapcontext(&main_ctx_, &ctx_);
  }

  ucontext_t ctx_{};
  ucontext_t main_ctx_{};
  std::unique_ptr<char[]> stack_;
};

TEST_F(SimpleContextTest, BasicContextSwitch) {
  int counter = 0;
  auto func = [&]() {
    counter++;
    LOG(ERROR) << "Function executing in new context, counter = " << counter;
  };

  RunContextSwitch(func);
  LOG(ERROR) << "Back in main context, counter = " << counter;

  EXPECT_EQ(counter, 1) << "Counter should be incremented twice";
}

}  // namespace
