#include "krnl/lifecycle.hpp"
#include <cassert>
#include <stdexcept>
using namespace krnl;

int main() {
  Lifecycle lifecycle;
  assert(lifecycle.state() == LifecycleState::uninitialized);
  const auto invalid = [&lifecycle](LifecycleEvent event) {
    const auto before = lifecycle.state();
    bool threw = false;
    try { lifecycle.apply(event); }
    catch (const std::logic_error&) { threw = true; }
    assert(threw && lifecycle.state() == before);
  };
  invalid(LifecycleEvent::start);
  invalid(LifecycleEvent::stop);
  lifecycle.apply(LifecycleEvent::initialize);
  invalid(LifecycleEvent::initialize);
  lifecycle.apply(LifecycleEvent::initialized);
  lifecycle.apply(LifecycleEvent::start);
  invalid(LifecycleEvent::start);
  lifecycle.apply(LifecycleEvent::started);
  invalid(LifecycleEvent::started);
  lifecycle.apply(LifecycleEvent::stop);
  invalid(LifecycleEvent::start);
  lifecycle.apply(LifecycleEvent::stopped);
  lifecycle.apply(LifecycleEvent::start); // restart after stop
  lifecycle.apply(LifecycleEvent::started);
  assert(lifecycle.state() == LifecycleState::running);
  LifecycleState next = LifecycleState::ready;
  assert(!try_transition(LifecycleState::running, LifecycleEvent::initialize, next));
  assert(next == LifecycleState::ready);
  assert(to_string(LifecycleState::running) == "running");
}
