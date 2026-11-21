#pragma once
#include "krnl/lifecycle.hpp"
#include <algorithm>
#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <exception>
#include <stdexcept>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace krnl {
enum class ErrorCode { invalid_argument, invalid_state, not_found, duplicate, type_mismatch, cancelled, queue_full, exhausted, transport, exception, disposed, invalid_graph };
struct Error { ErrorCode code; std::string message; bool retryable=false; };
template<class T> class Result {
  std::variant<T,Error> data_;
 public:
  Result(T value):data_(std::move(value)){}
  Result(Error error):data_(std::move(error)){}
  bool ok()const noexcept{return std::holds_alternative<T>(data_);}
  const T& value()const{return std::get<T>(data_);}
  T& value(){return std::get<T>(data_);}
  const Error& error()const{return std::get<Error>(data_);}
};
template<> class Result<void> {
  std::optional<Error> error_;
 public:
  Result()=default;
  Result(Error error):error_(std::move(error)){}
  bool ok()const noexcept{return !error_.has_value();}
  const Error& error()const{return *error_;}
};
inline Error failure(ErrorCode code,std::string message,bool retryable=false){return {code,std::move(message),retryable};}
struct InvocationContext {
 std::string caller,correlation_id,idempotency_key,protocol;
 std::map<std::string,std::string> metadata;
};
class Cancellation {
 std::shared_ptr<std::atomic<bool>> flag_=std::make_shared<std::atomic<bool>>(false);
 public:
 void cancel()const noexcept{flag_->store(true);}
 bool cancelled()const noexcept{return flag_->load();}
};
struct OperationInfo {std::string name,description;std::vector<std::string> capabilities,protocols;std::type_index input{typeid(void)},output{typeid(void)};};
struct ServiceInfo { std::string id,name,version;std::vector<std::string> capabilities,protocols;};
inline Result<void> validate(const ServiceInfo& info){
 if(info.id.empty()||info.name.empty()||info.version.empty())return failure(ErrorCode::invalid_argument,"service identity/name/version required");
 return {};
}
struct InvocationTrace {std::uint64_t id=0;std::string service,operation,correlation_id;bool success=false;ErrorCode code=ErrorCode::exception;};
class TraceBuffer {
 mutable std::mutex mutex_; std::deque<InvocationTrace> records_; std::size_t capacity_;
 public:
 explicit TraceBuffer(std::size_t capacity=128):capacity_(capacity){}
 void add(InvocationTrace r){std::lock_guard<std::mutex> lock(mutex_);if(!capacity_)return; if(records_.size()==capacity_)records_.pop_front();records_.push_back(std::move(r));}
 std::vector<InvocationTrace> snapshot()const{std::lock_guard<std::mutex> lock(mutex_);return {records_.begin(),records_.end()};}
};
// Functionality-level notifications, not service-owned application event definitions.
class EventBus {
 struct State {std::mutex mutex;std::uint64_t next=1;std::map<std::uint64_t,std::pair<std::string,std::function<void(const std::any&)>>> listeners;};
 std::shared_ptr<State> state_=std::make_shared<State>();
 public:
 class Subscription {
  std::weak_ptr<State> state_; std::uint64_t id_=0;
  public:
  Subscription(std::weak_ptr<State> state,std::uint64_t id):state_(std::move(state)),id_(id){}
  Subscription(const Subscription&)=delete;Subscription& operator=(const Subscription&)=delete;
  Subscription(Subscription&& other)noexcept:state_(std::move(other.state_)),id_(std::exchange(other.id_,0)){}
  Subscription& operator=(Subscription&& other)noexcept{if(this!=&other){unsubscribe();state_=std::move(other.state_);id_=std::exchange(other.id_,0);}return *this;}
  ~Subscription(){unsubscribe();}
  void unsubscribe()noexcept{if(!id_)return;if(auto s=state_.lock()){std::lock_guard<std::mutex> lock(s->mutex);s->listeners.erase(id_);}id_=0;}
 };
 Subscription subscribe(std::string key,std::function<void(const std::any&)> callback){auto s=state_;std::lock_guard<std::mutex> lock(s->mutex);auto id=s->next++;s->listeners.emplace(id,std::make_pair(std::move(key),std::move(callback)));return {s,id};}
 void emit(const std::string& key,const std::any& payload)const{std::vector<std::function<void(const std::any&)>> callbacks;{auto s=state_;std::lock_guard<std::mutex> lock(s->mutex);for(auto& p:s->listeners)if(p.second.first==key)callbacks.push_back(p.second.second);}for(auto& cb:callbacks){try{cb(payload);}catch(...){ /* user callback isolation */ }}}
 void clear(){auto s=state_;std::lock_guard<std::mutex> lock(s->mutex);s->listeners.clear();}
};
class StateStore {
 mutable std::mutex mutex_; std::map<std::string,std::any> values_; EventBus updates_;
 public:
 template<class T> void set(std::string key,T value){{std::lock_guard<std::mutex> lock(mutex_);values_[key]=std::any(std::move(value));}updates_.emit(key,std::any(key));}
 EventBus::Subscription subscribe(std::string key,std::function<void(const std::any&)> fn){return updates_.subscribe(std::move(key),std::move(fn));}
 template<class T> Result<T> get(const std::string& key)const{std::lock_guard<std::mutex> lock(mutex_);auto it=values_.find(key);if(it==values_.end())return failure(ErrorCode::not_found,"state key not found");auto p=std::any_cast<T>(&it->second);if(!p)return failure(ErrorCode::type_mismatch,"state type mismatch");return *p;}
 void clear(){{std::lock_guard<std::mutex> lock(mutex_);values_.clear();}updates_.clear();}
};
struct LifecycleHooks {std::function<void()> on_init,on_start,on_stop,on_dispose;};
class Service {
 using Handler=std::function<Result<std::any>(const std::any&,const InvocationContext&,Cancellation)>;
 struct Operation {OperationInfo info;Handler handler;};
 ServiceInfo info_;LifecycleHooks hooks_;
 mutable std::mutex mutex_;std::condition_variable idle_;Lifecycle lifecycle_;bool disposed_=false;std::size_t in_flight_=0;
 std::map<std::string,Operation> operations_;std::atomic<std::uint64_t> sequence_{0};TraceBuffer traces_;EventBus events_;StateStore state_;
 static Result<void> run_hook(const std::function<void()>& hook){try{if(hook)hook();return {};}catch(const std::exception& e){return failure(ErrorCode::exception,e.what());}catch(...){return failure(ErrorCode::exception,"unknown hook failure");}}
 public:
 explicit Service(ServiceInfo info,LifecycleHooks hooks={},std::size_t trace_limit=128):info_(std::move(info)),hooks_(std::move(hooks)),traces_(trace_limit){}
 Service(const Service&)=delete;Service& operator=(const Service&)=delete;
 ~Service(){(void)dispose();}
 const ServiceInfo& info()const noexcept{return info_;}
 LifecycleState status()const{std::lock_guard<std::mutex> lock(mutex_);return lifecycle_.state();}
 bool disposed()const{std::lock_guard<std::mutex> lock(mutex_);return disposed_;}
 EventBus& events()noexcept{return events_;}
 StateStore& state()noexcept{return state_;}
 std::vector<InvocationTrace> traces()const{return traces_.snapshot();}
 Result<void> initialize(){std::unique_lock<std::mutex> lock(mutex_);if(disposed_)return failure(ErrorCode::disposed,"service disposed");if(lifecycle_.state()!=LifecycleState::uninitialized)return failure(ErrorCode::invalid_state,"initialize requires uninitialized");auto valid=validate(info_);if(!valid.ok())return valid;lifecycle_.apply(LifecycleEvent::initialize);lock.unlock();auto r=run_hook(hooks_.on_init);lock.lock();lifecycle_.apply(r.ok()?LifecycleEvent::initialized:LifecycleEvent::initialization_failed);return r;}
 Result<void> start(){std::unique_lock<std::mutex> lock(mutex_);if(disposed_)return failure(ErrorCode::disposed,"service disposed");if(lifecycle_.state()==LifecycleState::running)return {};if(lifecycle_.state()!=LifecycleState::ready&&lifecycle_.state()!=LifecycleState::stopped)return failure(ErrorCode::invalid_state,"start requires ready or stopped");lifecycle_.apply(LifecycleEvent::start);lock.unlock();auto r=run_hook(hooks_.on_start);lock.lock();lifecycle_.apply(r.ok()?LifecycleEvent::started:LifecycleEvent::start_failed);return r;}
 Result<void> stop(){std::unique_lock<std::mutex> lock(mutex_);if(disposed_)return failure(ErrorCode::disposed,"service disposed");if(lifecycle_.state()==LifecycleState::stopped)return {};if(lifecycle_.state()!=LifecycleState::running)return failure(ErrorCode::invalid_state,"stop requires running");lifecycle_.apply(LifecycleEvent::stop);idle_.wait(lock,[this]{return in_flight_==0;});lock.unlock();auto r=run_hook(hooks_.on_stop);lock.lock();lifecycle_.apply(r.ok()?LifecycleEvent::stopped:LifecycleEvent::stop_failed);return r;}
 Result<void> restart(){auto s=status();if(s==LifecycleState::running){auto r=stop();if(!r.ok())return r;}return start();}
 Result<void> dispose(){std::unique_lock<std::mutex> lock(mutex_);if(disposed_)return {};bool running=lifecycle_.state()==LifecycleState::running;lock.unlock();std::optional<Error> first;if(running){auto stopped=stop();if(!stopped.ok())first=stopped.error();}lock.lock();if(disposed_)return first?Result<void>(*first):Result<void>();idle_.wait(lock,[this]{return in_flight_==0;});disposed_=true;lock.unlock();auto r=run_hook(hooks_.on_dispose);lock.lock();operations_.clear();lock.unlock();events_.clear();state_.clear();if(first)return *first;return r;}
 template<class In,class Out,class Fn>
 Result<void> add_operation(OperationInfo info,Fn handler,std::function<bool(const In&)> check_in={},std::function<bool(const Out&)> check_out={}){
  static_assert(!std::is_void<In>::value&&!std::is_void<Out>::value,"use concrete contract types");
  if(info.name.empty())return failure(ErrorCode::invalid_argument,"operation name required");
  info.input=typeid(In);info.output=typeid(Out);
  Handler erase=[fn=std::move(handler),check_in=std::move(check_in),check_out=std::move(check_out)](const std::any& input,const InvocationContext& ctx,Cancellation cancellation)->Result<std::any>{
   auto ptr=std::any_cast<In>(&input);if(!ptr)return failure(ErrorCode::type_mismatch,"input contract type mismatch");
   if(check_in&&!check_in(*ptr))return failure(ErrorCode::invalid_argument,"input validation failed");
   if(cancellation.cancelled())return failure(ErrorCode::cancelled,"cancelled");
   try{Result<Out> r=fn(*ptr,ctx,cancellation);if(!r.ok())return r.error();if(check_out&&!check_out(r.value()))return failure(ErrorCode::invalid_argument,"output validation failed");return std::any(r.value());}
   catch(const std::exception& e){return failure(ErrorCode::exception,e.what());}catch(...){return failure(ErrorCode::exception,"operation threw");}
  };
  std::lock_guard<std::mutex> lock(mutex_);if(disposed_)return failure(ErrorCode::disposed,"service disposed");if(operations_.count(info.name))return failure(ErrorCode::duplicate,"operation already registered");auto key=info.name;operations_.emplace(std::move(key),Operation{std::move(info),std::move(erase)});return {};
 }
 std::vector<OperationInfo> operations()const{std::lock_guard<std::mutex> lock(mutex_);std::vector<OperationInfo> out;for(auto& op:operations_)out.push_back(op.second.info);return out;}
 Result<std::any> call(const std::string& name,const std::any& input,InvocationContext ctx={},Cancellation cancellation={}){
  Handler handler;std::uint64_t id=++sequence_;
  {std::lock_guard<std::mutex> lock(mutex_);
   if(disposed_)return failure(ErrorCode::disposed,"service disposed");
   if(lifecycle_.state()!=LifecycleState::running)return failure(ErrorCode::invalid_state,"service is not running");
   auto it=operations_.find(name);if(it==operations_.end())return failure(ErrorCode::not_found,"operation not found");handler=it->second.handler;++in_flight_;
  }
  Result<std::any> r=failure(ErrorCode::exception,"unexpected invocation exception");
  try{r=handler(input,ctx,cancellation);}catch(const std::exception& e){r=failure(ErrorCode::exception,e.what());}catch(...){r=failure(ErrorCode::exception,"unknown invocation exception");}
  traces_.add({id,info_.id,name,ctx.correlation_id,r.ok(),r.ok()?ErrorCode::exception:r.error().code});
  events_.emit(name+(r.ok()?":completed":":failed"),std::any(ctx.correlation_id));
  {std::lock_guard<std::mutex> lock(mutex_);--in_flight_;}idle_.notify_all();return r;
 }
 template<class In,class Out> Result<Out> invoke(const std::string& name,const In& input,InvocationContext ctx={},Cancellation cancel={}){
  auto r=call(name,std::any(input),std::move(ctx),cancel);if(!r.ok())return r.error();if(auto out=std::any_cast<Out>(&r.value()))return *out;return failure(ErrorCode::type_mismatch,"output contract type mismatch");
 }
};
// Registry owns shared service lifetimes; no cross-service graph or catalog distribution.
class ServiceRuntime {
 mutable std::mutex mutex_;std::map<std::string,std::shared_ptr<Service>> services_;
 public:
 Result<void> register_service(std::shared_ptr<Service> service){if(!service||!validate(service->info()).ok())return failure(ErrorCode::invalid_argument,"invalid service");std::lock_guard<std::mutex> lock(mutex_);if(services_.count(service->info().id))return failure(ErrorCode::duplicate,"duplicate service ID");services_.emplace(service->info().id,std::move(service));return {};}
 std::shared_ptr<Service> get(const std::string& id)const{std::lock_guard<std::mutex> lock(mutex_);auto it=services_.find(id);return it==services_.end()?nullptr:it->second;}
 bool unregister_service(const std::string& id){std::lock_guard<std::mutex> lock(mutex_);return services_.erase(id)>0;}
 std::vector<ServiceInfo> discover(const std::string& capability="")const{std::lock_guard<std::mutex> lock(mutex_);std::vector<ServiceInfo> out;for(auto& it:services_){auto info=it.second->info();if(capability.empty()||std::find(info.capabilities.begin(),info.capabilities.end(),capability)!=info.capabilities.end())out.push_back(std::move(info));}return out;}
 Result<void> start_all(){std::vector<std::shared_ptr<Service>> started;for(auto& info:discover()){auto service=get(info.id);if(service->status()==LifecycleState::uninitialized){auto r=service->initialize();if(!r.ok()){for(auto it=started.rbegin();it!=started.rend();++it)(void)(*it)->stop();return r;}}auto r=service->start();if(!r.ok()){for(auto it=started.rbegin();it!=started.rend();++it)(void)(*it)->stop();return r;}started.push_back(service);}return {};}
 Result<void> stop_all(){auto infos=discover();std::optional<Error> first;for(auto it=infos.rbegin();it!=infos.rend();++it){auto s=get(it->id);if(s->status()==LifecycleState::running){auto r=s->stop();if(!r.ok()&&!first)first=r.error();}}return first?Result<void>(*first):Result<void>();}
 Result<void> dispose_all(){auto infos=discover();std::optional<Error> first;for(auto it=infos.rbegin();it!=infos.rend();++it){auto r=get(it->id)->dispose();if(!r.ok()&&!first)first=r.error();}return first?Result<void>(*first):Result<void>();}
};
struct RetryPolicy {unsigned retries=0;};
// Caller-driven, fixed-capacity queue: host owns threads, event loop and clock.
class InvocationQueue {
 struct Job {std::function<Result<std::any>()> execute;Cancellation cancel;RetryPolicy retry;};
 std::mutex mutex_;std::deque<Job> jobs_;std::size_t capacity_,active_limit_,active_=0;
 public:
 explicit InvocationQueue(std::size_t capacity,std::size_t max_active=1):capacity_(capacity),active_limit_(max_active){}
 Result<void> push(std::function<Result<std::any>()> task,Cancellation cancel={},RetryPolicy policy={}){std::lock_guard<std::mutex> lock(mutex_);if(jobs_.size()>=capacity_)return failure(ErrorCode::queue_full,"queue at capacity");if(!task)return failure(ErrorCode::invalid_argument,"missing task");jobs_.push_back({std::move(task),cancel,policy});return {};}
 std::size_t size(){std::lock_guard<std::mutex> lock(mutex_);return jobs_.size();}
 Result<std::any> pump_one(){Job job;{
 std::lock_guard<std::mutex> lock(mutex_);
 if(!active_limit_||active_>=active_limit_)return failure(ErrorCode::queue_full,"executor at concurrency limit",true);
 if(jobs_.empty())return failure(ErrorCode::not_found,"queue empty");
 job=std::move(jobs_.front());jobs_.pop_front();++active_;
 }
 // Release execution capacity on cancellation, errors, or success.
 struct Release {InvocationQueue& q;~Release(){std::lock_guard<std::mutex> lock(q.mutex_);--q.active_;}} release{*this};
 if(job.cancel.cancelled())return failure(ErrorCode::cancelled,"queue job cancelled");
 for(unsigned n=0;;++n){
  if(job.cancel.cancelled())return failure(ErrorCode::cancelled,"queue job cancelled");
  Result<std::any> r=failure(ErrorCode::exception,"job failure");
  try{r=job.execute();}catch(const std::exception& e){r=failure(ErrorCode::exception,e.what());}catch(...){r=failure(ErrorCode::exception,"job exception");}
  if(r.ok()||!r.error().retryable||n>=job.retry.retries)return r;
 }
 }
 void cancel_pending(){std::lock_guard<std::mutex> lock(mutex_);for(auto& job:jobs_)job.cancel.cancel();jobs_.clear();}
};
struct FlowStep {std::string id,operation;std::vector<std::string> depends_on;std::function<std::any(const std::map<std::string,std::any>&)> make_input;};
// A flow is bound to exactly one service instance; no service-address step exists.
inline Result<std::map<std::string,std::any>> execute_flow(Service& service,const std::vector<FlowStep>& steps,const InvocationContext& ctx={}){
 std::set<std::string> ids;for(auto& step:steps)if(step.id.empty()||step.operation.empty()||!ids.insert(step.id).second)return failure(ErrorCode::invalid_graph,"duplicate or missing flow step");
 for(auto& step:steps)for(auto& dep:step.depends_on)if(!ids.count(dep))return failure(ErrorCode::invalid_graph,"missing dependency");
 std::map<std::string,std::any> output;std::set<std::string> done;
 while(done.size()<steps.size()){bool progress=false;for(auto& step:steps){if(done.count(step.id))continue;if(!std::all_of(step.depends_on.begin(),step.depends_on.end(),[&](const std::string& dep){return done.count(dep)>0;}))continue;
 std::any in;try{if(step.make_input)in=step.make_input(output);}catch(const std::exception& e){return failure(ErrorCode::exception,e.what());}catch(...){return failure(ErrorCode::exception,"flow input failed");}
 auto r=service.call(step.operation,in,ctx);if(!r.ok())return r.error();output.emplace(step.id,std::move(r.value()));done.insert(step.id);progress=true;
 }if(!progress)return failure(ErrorCode::invalid_graph,"cyclic functionality DAG");}return output;
}
struct Endpoint {std::string protocol,address;std::uint16_t port=0;};
class HostTransport {public:virtual ~HostTransport()=default;virtual Result<std::any> send(const Endpoint&,const std::string&,const std::any&,const InvocationContext&)=0;};
class ServiceProxy {
 Endpoint endpoint_;HostTransport& transport_;
 public:
 ServiceProxy(Endpoint endpoint,HostTransport& transport):endpoint_(std::move(endpoint)),transport_(transport){}
 template<class In,class Out> Result<Out> invoke(const std::string& operation,const In& input,InvocationContext ctx={}){
  if(endpoint_.protocol.empty()||endpoint_.address.empty())return failure(ErrorCode::invalid_argument,"invalid endpoint");
  Result<std::any> r=failure(ErrorCode::transport,"transport exception");try{r=transport_.send(endpoint_,operation,std::any(input),ctx);}catch(const std::exception& e){return failure(ErrorCode::transport,e.what());}catch(...){return failure(ErrorCode::transport,"transport threw");}
  if(!r.ok()) return r.error();
  if(auto out=std::any_cast<Out>(&r.value())) return *out;
  return failure(ErrorCode::type_mismatch,"proxy response contract mismatch");
 }
};
} // namespace krnl
