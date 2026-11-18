#include "krnl/lifecycle.hpp"
#include <array>
#include <cassert>
#include <stdexcept>
using namespace krnl;
struct TransitionCase { LifecycleState from; LifecycleEvent event; LifecycleState to; };
constexpr std::array<TransitionCase, 7> valid = {{{LifecycleState::uninitialized,LifecycleEvent::initialize,LifecycleState::initializing},{LifecycleState::initializing,LifecycleEvent::initialized,LifecycleState::ready},{LifecycleState::ready,LifecycleEvent::start,LifecycleState::starting},{LifecycleState::starting,LifecycleEvent::started,LifecycleState::running},{LifecycleState::running,LifecycleEvent::stop,LifecycleState::stopping},{LifecycleState::stopping,LifecycleEvent::stopped,LifecycleState::stopped},{LifecycleState::stopped,LifecycleEvent::start,LifecycleState::starting}}};
constexpr bool all_valid_targets() { for(const auto& transition:valid) { auto actual=transition.from; if(!try_transition(transition.from,transition.event,actual)||actual!=transition.to)return false; } return true; }
static_assert(all_valid_targets(), "Every accepted transition must produce the documented target state");
int main(){assert(all_valid_targets()); Lifecycle lifecycle; const std::array<LifecycleEvent,6> first={{LifecycleEvent::initialize,LifecycleEvent::initialized,LifecycleEvent::start,LifecycleEvent::started,LifecycleEvent::stop,LifecycleEvent::stopped}}; for(auto event:first) lifecycle.apply(event); assert(lifecycle.state()==LifecycleState::stopped); for(int i=0;i<3;++i){lifecycle.apply(LifecycleEvent::start);lifecycle.apply(LifecycleEvent::started);assert(lifecycle.state()==LifecycleState::running);lifecycle.apply(LifecycleEvent::stop);lifecycle.apply(LifecycleEvent::stopped);assert(lifecycle.state()==LifecycleState::stopped);} bool rejected=false;try{lifecycle.apply(LifecycleEvent::initialize);}catch(const std::logic_error&){rejected=true;}assert(rejected&&lifecycle.state()==LifecycleState::stopped);}
