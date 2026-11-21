#include "krnl/kernel.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <atomic>
#include <future>
#include <thread>
using namespace krnl;
int main(){
 std::atomic<int> disposed{0};std::promise<void> enter,release;
 auto gate=release.get_future().share();
 auto s=std::make_shared<Service>(ServiceInfo{"race","Race","1",{},{}},LifecycleHooks{{},{},{},[&]{disposed++;enter.set_value();gate.wait();}});
 assert(s->initialize().ok());assert(s->start().ok());
 auto a=std::async(std::launch::async,[&]{return s->dispose();});
 enter.get_future().wait();
 auto b=std::async(std::launch::async,[&]{return s->dispose();});
 assert((s->invoke<int,int>("missing",1).error().code==ErrorCode::disposed));
 release.set_value();
 assert(a.get().ok()&&b.get().ok());assert(disposed==1&&s->disposed());
 // A disposing service prevents subsequent starts or operations.
 assert(s->start().error().code==ErrorCode::disposed);
}
