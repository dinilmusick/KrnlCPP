#include "krnl/kernel.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <future>
#include <thread>
#include <string>
using namespace krnl;
struct In {int a;};struct Out {int b;};
struct EchoTransport final:HostTransport {Result<std::any> send(const Endpoint& e,const std::string& op,const std::any& input,const InvocationContext&)override{if(e.protocol!="memory"||op!="double")return failure(ErrorCode::transport,"not supported");return std::any(Out{std::any_cast<In>(input).a*2});}};
int main(){
 int initialized=0,started=0,stopped=0,disposed=0;
 auto s=std::make_shared<Service>(ServiceInfo{"svc","Example","1.0",{"math"},{"memory"}},LifecycleHooks{[&]{++initialized;},[&]{++started;},[&]{++stopped;},[&]{++disposed;}},2);
 assert((s->add_operation<In,Out>({"double","Multiply by two",{"math"},{"memory"}},[](const In& in,const InvocationContext&,Cancellation){return Result<Out>(Out{in.a*2});},[](const In& in){return in.a>=0;}).ok()));
 assert((!s->add_operation<In,Out>({"double","",{},{}},[](auto&,auto&,auto){return Result<Out>(Out{0});}).ok()));
 assert(s->initialize().ok());assert(s->start().ok());
 auto x=s->invoke<In,Out>("double",In{2},InvocationContext{"caller","corr-1","","",{}});assert(x.ok()&&x.value().b==4);
 assert((s->invoke<In,Out>("double",In{-1}).error().code==ErrorCode::invalid_argument));
 assert((s->invoke<int,Out>("double",7).error().code==ErrorCode::type_mismatch));
 assert((s->invoke<In,int>("double",In{1}).error().code==ErrorCode::type_mismatch));
 assert(s->traces().size()==2); // bounded, includes failures
 auto subscription=s->events().subscribe("double:completed",[](const std::any& a){assert(std::any_cast<std::string>(a).empty());});
 assert((s->invoke<In,Out>("double",In{1}).ok()));subscription.unsubscribe();
 s->state().set("value",std::string("ready"));assert(s->state().get<std::string>("value").value()=="ready");assert(s->state().get<int>("value").error().code==ErrorCode::type_mismatch);
 auto flow=execute_flow(*s,{{"first","double",{},[](auto const&){return std::any(In{3});}},{"second","double",{"first"},[](auto const& result){return std::any(In{std::any_cast<Out>(result.at("first")).b});}}});assert(flow.ok()&&std::any_cast<Out>(flow.value().at("second")).b==12);
 auto bad=execute_flow(*s,{{"first","double",{"second"},{}},{"second","double",{"first"},{}}});assert(!bad.ok()&&bad.error().code==ErrorCode::invalid_graph);
 ServiceRuntime runtime;assert(runtime.register_service(s).ok());assert(!runtime.register_service(s).ok());assert(runtime.discover("math").size()==1);
 assert(s->restart().ok());assert(s->status()==LifecycleState::running);assert(started==2&&stopped==1);
 Cancellation c;InvocationQueue queue(1);assert(queue.push([&]{return s->call("double",std::any(In{4}));}).ok());assert(!queue.push([]{return Result<std::any>(std::any(1));}).ok());auto qr=queue.pump_one();assert(qr.ok()&&std::any_cast<Out>(qr.value()).b==8);
 c.cancel();assert(queue.push([]{return Result<std::any>(std::any(1));},c).ok());assert(queue.pump_one().error().code==ErrorCode::cancelled);
 int attempts=0;assert(queue.push([&]()->Result<std::any>{++attempts;if(attempts<3)return failure(ErrorCode::transport,"retry",true);return std::any(4);},{},RetryPolicy{2}).ok());assert(queue.pump_one().ok()&&attempts==3);
 EchoTransport transport;ServiceProxy proxy({"memory","test",0},transport);assert((proxy.invoke<In,Out>("double",In{5}).value().b==10));
 assert(s->stop().ok());assert((!s->invoke<In,Out>("double",In{1}).ok()));assert(s->start().ok());
 assert(runtime.dispose_all().ok());assert(runtime.dispose_all().ok());assert(disposed==1&&stopped==3&&s->disposed());assert((s->invoke<In,Out>("double",In{1}).error().code==ErrorCode::disposed));
 {
 auto fail=std::make_shared<Service>(ServiceInfo{"fail","Fail","1",{}, {}},LifecycleHooks{[]{throw std::runtime_error("init");},{},{},{}});assert(!fail->initialize().ok()&&fail->status()==LifecycleState::uninitialized);
 }
 {
 int begins=0;auto fail=std::make_shared<Service>(ServiceInfo{"fail2","Fail2","1",{}, {}},LifecycleHooks{{},[&]{if(++begins==1)throw std::runtime_error("start");},{},{}});assert(fail->initialize().ok());assert(!fail->start().ok()&&fail->status()==LifecycleState::ready);assert(fail->start().ok());
 }
 {
 auto fail=std::make_shared<Service>(ServiceInfo{"fail3","Fail3","1",{}, {}},LifecycleHooks{{},{},[]{throw std::runtime_error("stop");},{}});assert(fail->initialize().ok()&&fail->start().ok());assert(!fail->stop().ok()&&fail->status()==LifecycleState::running);
 }
}
