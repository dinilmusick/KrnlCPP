#include "krnl/lifecycle.hpp"
#include <array>
#include <stdexcept>

using namespace krnl;

constexpr std::array<LifecycleState, 7> states = {
  LifecycleState::uninitialized, LifecycleState::initializing,
  LifecycleState::ready, LifecycleState::starting, LifecycleState::running,
  LifecycleState::stopping, LifecycleState::stopped
};
constexpr std::array<LifecycleEvent, 9> events = {
  LifecycleEvent::initialize, LifecycleEvent::initialized, LifecycleEvent::start,
  LifecycleEvent::started, LifecycleEvent::stop, LifecycleEvent::stopped,
  LifecycleEvent::initialization_failed, LifecycleEvent::start_failed,
  LifecycleEvent::stop_failed
};

constexpr bool expected_target(LifecycleState from, LifecycleEvent event,
                               LifecycleState& next) {
  switch (event) {
    case LifecycleEvent::initialize:
      if (from == LifecycleState::uninitialized) {next = LifecycleState::initializing; return true;}
      break;
    case LifecycleEvent::initialized:
      if (from == LifecycleState::initializing) {next = LifecycleState::ready; return true;}
      break;
    case LifecycleEvent::initialization_failed:
      if (from == LifecycleState::initializing) {next = LifecycleState::uninitialized; return true;}
      break;
    case LifecycleEvent::start:
      if (from == LifecycleState::ready || from == LifecycleState::stopped) {next = LifecycleState::starting; return true;}
      break;
    case LifecycleEvent::started:
      if (from == LifecycleState::starting) {next = LifecycleState::running; return true;}
      break;
    case LifecycleEvent::start_failed:
      if (from == LifecycleState::starting) {next = LifecycleState::ready; return true;}
      break;
    case LifecycleEvent::stop:
      if (from == LifecycleState::running) {next = LifecycleState::stopping; return true;}
      break;
    case LifecycleEvent::stopped:
      if (from == LifecycleState::stopping) {next = LifecycleState::stopped; return true;}
      break;
    case LifecycleEvent::stop_failed:
      if (from == LifecycleState::stopping) {next = LifecycleState::running; return true;}
      break;
  }
  return false;
}

constexpr bool exhaustive_matrix_correct() {
  for (auto from : states) {
    for (auto event : events) {
      auto expected = from;
      auto actual = from;
      const bool should_accept = expected_target(from, event, expected);
      const bool accepted = try_transition(from, event, actual);
      if (accepted != should_accept || actual != expected) return false;
    }
  }
  return true;
}
static_assert(exhaustive_matrix_correct(), "Lifecycle must match canonical KrnlTS transition graph");

int main() {
  if (!exhaustive_matrix_correct()) return 1;
  Lifecycle service;
  service.apply(LifecycleEvent::initialize);
  service.apply(LifecycleEvent::initialization_failed);
  if (service.state() != LifecycleState::uninitialized) return 2;
  service.apply(LifecycleEvent::initialize);
  service.apply(LifecycleEvent::initialized);
  service.apply(LifecycleEvent::start);
  service.apply(LifecycleEvent::start_failed);
  if (service.state() != LifecycleState::ready) return 3;
  service.apply(LifecycleEvent::start);
  service.apply(LifecycleEvent::started);
  service.apply(LifecycleEvent::stop);
  service.apply(LifecycleEvent::stop_failed);
  if (service.state() != LifecycleState::running) return 4;
  service.apply(LifecycleEvent::stop);
  service.apply(LifecycleEvent::stopped);
  service.apply(LifecycleEvent::start);
  service.apply(LifecycleEvent::started);
  if (service.state() != LifecycleState::running) return 5;
  try { service.apply(LifecycleEvent::initialization_failed); }
  catch (const std::logic_error&) { return service.state() == LifecycleState::running ? 0 : 6; }
  return 7;
}
