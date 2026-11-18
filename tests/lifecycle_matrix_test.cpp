#include "krnl/lifecycle.hpp"
#include <array>
#include <cassert>
#include <stdexcept>

using namespace krnl;

constexpr std::array<LifecycleState, 7> states = {
    LifecycleState::uninitialized, LifecycleState::initializing,
    LifecycleState::ready, LifecycleState::starting,
    LifecycleState::running, LifecycleState::stopping,
    LifecycleState::stopped};
constexpr std::array<LifecycleEvent, 6> events = {
    LifecycleEvent::initialize, LifecycleEvent::initialized,
    LifecycleEvent::start, LifecycleEvent::started,
    LifecycleEvent::stop, LifecycleEvent::stopped};

constexpr bool expected(std::size_t state, std::size_t event) {
  return (state == 0 && event == 0) ||
         (state == 1 && event == 1) ||
         ((state == 2 || state == 6) && event == 2) ||
         (state == 3 && event == 3) ||
         (state == 4 && event == 4) ||
         (state == 5 && event == 5);
}
constexpr bool matrix_is_correct() {
  for (std::size_t i = 0; i < states.size(); ++i) {
    for (std::size_t j = 0; j < events.size(); ++j) {
      auto next = states[i];
      const bool accepted = try_transition(states[i], events[j], next);
      if (accepted != expected(i, j)) return false;
      if (!accepted && next != states[i]) return false;
    }
  }
  return true;
}
static_assert(matrix_is_correct(), "Lifecycle transition matrix must match KrnlTS contract");

int main() {
  assert(matrix_is_correct());
  for (std::size_t i = 0; i < states.size(); ++i) {
    for (std::size_t j = 0; j < events.size(); ++j) {
      Lifecycle instance;
      // The transition function is pure: rejected events must leave output unchanged.
      auto sentinel = LifecycleState::ready;
      const bool accepted = try_transition(states[i], events[j], sentinel);
      assert(accepted == expected(i, j));
      if (!accepted) assert(sentinel == LifecycleState::ready);
    }
  }
}
