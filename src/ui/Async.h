#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace su::ui {

// Runs a job on a detached worker thread; the UI polls take() every frame.
// Starting a new job discards the result of a previous, still-running one.
template <class T>
class Async {
 public:
  template <class F>
  void start(F job) {
    auto state = std::make_shared<State>();
    state_ = state;
    std::thread([state, job = std::move(job)]() mutable {
      T value = job();
      std::lock_guard lock(state->mutex);
      state->value = std::move(value);
    }).detach();
  }

  bool running() const {
    if (!state_) return false;
    std::lock_guard lock(state_->mutex);
    return !state_->value.has_value();
  }

  std::optional<T> take() {
    if (!state_) return std::nullopt;
    std::optional<T> out;
    {
      std::lock_guard lock(state_->mutex);
      if (!state_->value) return std::nullopt;
      out = std::move(state_->value);
    }
    state_.reset();
    return out;
  }

 private:
  struct State {
    std::mutex mutex;
    std::optional<T> value;
  };
  std::shared_ptr<State> state_;
};

}  // namespace su::ui
