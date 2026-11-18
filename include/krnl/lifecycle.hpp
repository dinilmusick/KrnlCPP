#pragma once
#include <stdexcept>
#include <string_view>

namespace krnl {
enum class LifecycleState {
  uninitialized, initializing, ready, starting, running, stopping, stopped
};
enum class LifecycleEvent { initialize, initialized, start, started, stop, stopped };

constexpr std::string_view to_string(LifecycleState s) noexcept {
  switch (s) {
    case LifecycleState::uninitialized: return "uninitialized";
    case LifecycleState::initializing: return "initializing";
    case LifecycleState::ready: return "ready";
    case LifecycleState::starting: return "starting";
    case LifecycleState::running: return "running";
    case LifecycleState::stopping: return "stopping";
    case LifecycleState::stopped: return "stopped";
  }
  return "unknown";
}

// Transition logic is pure; failed transitions never mutate the service state.
constexpr bool try_transition(LifecycleState from, LifecycleEvent event,
                              LifecycleState& next) noexcept {
  switch (event) {
    case LifecycleEvent::initialize:
      if (from == LifecycleState::uninitialized) { next = LifecycleState::initializing; return true; }
      break;
    case LifecycleEvent::initialized:
      if (from == LifecycleState::initializing) { next = LifecycleState::ready; return true; }
      break;
    case LifecycleEvent::start:
      if (from == LifecycleState::ready || from == LifecycleState::stopped) {
        next = LifecycleState::starting; return true;
      }
      break;
    case LifecycleEvent::started:
      if (from == LifecycleState::starting) { next = LifecycleState::running; return true; }
      break;
    case LifecycleEvent::stop:
      if (from == LifecycleState::running) { next = LifecycleState::stopping; return true; }
      break;
    case LifecycleEvent::stopped:
      if (from == LifecycleState::stopping) { next = LifecycleState::stopped; return true; }
      break;
  }
  return false;
}

class Lifecycle {
 public:
  constexpr LifecycleState state() const noexcept { return state_; }
  void apply(LifecycleEvent event) {
    LifecycleState next = state_;
    if (!try_transition(state_, event, next))
      throw std::logic_error("invalid service lifecycle transition");
    state_ = next;
  }
 private:
  LifecycleState state_ = LifecycleState::uninitialized;
};
}  // namespace krnl
