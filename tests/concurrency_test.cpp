#include "krnl/kernel.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <future>
#include <thread>
#include <atomic>
using namespace krnl;
struct In {int value;};
int main() {
 auto svc=std::make_shared<Service>(ServiceInfo{"svc","Lifetime","1",{},{}},LifecycleHooks{});
 std::promise<void> entered, unblock;auto released=unblock.get_future().share();
 assert((svc->add_operation<In,int>({"hold","",{},{}},[&](const In&,const InvocationContext&,Cancellation){entered.set_value();released.wait();return Result<int>(42);}).ok()));
 assert(svc->initialize().ok()&&svc->start().ok());
 std::thread invoking([&]{assert((svc->invoke<In,int>("hold",In{1}).value()==42));});
 entered.get_future().wait();
 std::atomic<bool> stopped{false};std::thread stopping([&]{assert(svc->stop().ok());stopped=true;});
 // Stop cannot complete while an invocation holds an in-flight lease.
 assert(!stopped.load());unblock.set_value();invoking.join();stopping.join();assert(stopped);
 assert(svc->start().ok());
 InvocationQueue q(2,1);std::promise<void> entered_queue,unblock_queue;auto qrelease=unblock_queue.get_future().share();
 assert(q.push([&]()->Result<std::any>{entered_queue.set_value();qrelease.wait();return std::any(7);}).ok());
 assert(q.push([]()->Result<std::any>{return std::any(8);}).ok());
 std::thread pumping([&]{assert(std::any_cast<int>(q.pump_one().value())==7);});entered_queue.get_future().wait();
 assert(q.pump_one().error().code==ErrorCode::queue_full);assert(q.size()==1);
 unblock_queue.set_value();pumping.join();assert(std::any_cast<int>(q.pump_one().value())==8);
 std::atomic<int> updates{0};auto sub=svc->state().subscribe("counter",[&](const std::any&){++updates;});svc->state().set("counter",1);sub.unsubscribe();svc->state().set("counter",2);assert(updates==1);
 assert(svc->dispose().ok());
}
