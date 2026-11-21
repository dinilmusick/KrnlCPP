#pragma once
// Host-driven queue: no worker threads, clocks, sockets or sleeping in the kernel.
#include "krnl/kernel.hpp"
#include <algorithm>
#include <cstdint>
#include <future>
#include <limits>
#include <string>
#include <vector>

namespace krnl {
enum class JobStatus { waiting, active, completed, failed, cancelled };
struct JobPolicy {
 int priority=0;
 unsigned retries=0;
 std::uint64_t backoff_ms=0;
 bool exponential_backoff=false;
 std::uint64_t timeout_ms=0; // 0 disables the queue deadline
};
namespace detail {
inline std::uint64_t add_saturated(std::uint64_t a,std::uint64_t b) noexcept {
 const auto maximum=std::numeric_limits<std::uint64_t>::max();
 return b>maximum-a?maximum:a+b;
}
struct JobState {
 explicit JobState(std::string name):id(std::move(name)),future(promise.get_future().share()){}
 std::string id;
 mutable std::mutex mutex;
 JobStatus status=JobStatus::waiting;
 Cancellation cancellation;
 std::promise<Result<std::any>> promise;
 std::shared_future<Result<std::any>> future;
 bool settled=false;
 void change(JobStatus next){std::lock_guard<std::mutex> lock(mutex);if(!settled)status=next;}
 JobStatus current()const{std::lock_guard<std::mutex> lock(mutex);return status;}
 void settle(Result<std::any> result,JobStatus final){
  std::lock_guard<std::mutex> lock(mutex);
  if(settled)return;
  settled=true;status=final;promise.set_value(std::move(result));
 }
};
} // namespace detail
class JobHandle {
 std::shared_ptr<detail::JobState> state_;
 public:
 explicit JobHandle(std::shared_ptr<detail::JobState> s):state_(std::move(s)){}
 const std::string& id()const noexcept{return state_->id;}
 JobStatus status()const{return state_->current();}
 void cancel()const{
  state_->cancellation.cancel();
  if(state_->current()==JobStatus::waiting)
   state_->settle(failure(ErrorCode::cancelled,"job cancelled"),JobStatus::cancelled);
 }
 bool cancelled()const noexcept{return state_->cancellation.cancelled();}
 bool ready()const{return state_->future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;}
 Result<std::any> result()const{return state_->future.get();}
 template<class T> Result<T> result_as()const{
  auto r=result();if(!r.ok())return r.error();
  auto value=std::any_cast<T>(&r.value());
  return value?Result<T>(*value):Result<T>(failure(ErrorCode::type_mismatch,"job result type mismatch"));
 }
};
class JobQueue {
 struct Pending {
  std::shared_ptr<detail::JobState> state;
  std::function<Result<std::any>(Cancellation)> handler;
  JobPolicy policy;
  std::uint64_t order=0,ready_at=0,deadline=0;
  unsigned attempts=0;
 };
 mutable std::mutex mutex_;
 std::vector<Pending> waiting_;
 std::vector<std::shared_ptr<detail::JobState>> active_states_;
 std::size_t capacity_,max_active_,active_=0;
 std::condition_variable idle_;
 std::uint64_t next_id_=0;
 bool closed_=false;
 static bool terminal(const Pending& job){return job.state->current()!=JobStatus::waiting;}
 void prune(){waiting_.erase(std::remove_if(waiting_.begin(),waiting_.end(),terminal),waiting_.end());}
 public:
 explicit JobQueue(std::size_t max_waiting,std::size_t max_active=1):capacity_(max_waiting),max_active_(max_active){}
 JobQueue(const JobQueue&)=delete;JobQueue& operator=(const JobQueue&)=delete;
 ~JobQueue(){close();std::unique_lock<std::mutex> lock(mutex_);idle_.wait(lock,[this]{return active_==0;});}
 Result<JobHandle> enqueue(std::function<Result<std::any>(Cancellation)> handler,
                           JobPolicy policy={},std::uint64_t now_ms=0){
  if(!handler)return failure(ErrorCode::invalid_argument,"job handler required");
  std::lock_guard<std::mutex> lock(mutex_);
  if(closed_)return failure(ErrorCode::disposed,"queue closed");
  prune();
  if(waiting_.size()>=capacity_)return failure(ErrorCode::queue_full,"queue at capacity");
  const auto id=++next_id_;
  auto state=std::make_shared<detail::JobState>("job-"+std::to_string(id));
  waiting_.push_back({state,std::move(handler),policy,id,now_ms,
    policy.timeout_ms?detail::add_saturated(now_ms,policy.timeout_ms):0,0});
  return JobHandle(std::move(state));
 }
 std::size_t pending()const{
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t count=0;for(const auto& item:waiting_)if(!terminal(item))++count;
  return count;
 }
 std::size_t active()const{std::lock_guard<std::mutex> lock(mutex_);return active_;}
 void close(){
  std::vector<Pending> discarded;
  std::vector<std::shared_ptr<detail::JobState>> active_states;
  {std::lock_guard<std::mutex> lock(mutex_);closed_=true;discarded.swap(waiting_);active_states=active_states_;}
  for(auto& state:active_states)state->cancellation.cancel();
  for(auto& item:discarded){item.state->cancellation.cancel();item.state->settle(
   failure(ErrorCode::cancelled,"queue closed"),JobStatus::cancelled);}
 }
 // Host calls pump_one on its own workers. now_ms must be monotonic host time.
 // Retry delay and queued timeout are deterministic; running callbacks must
 // cooperate with Cancellation (no forced thread termination).
 Result<std::string> pump_one(std::uint64_t now_ms){
  Pending job;
  {
   std::lock_guard<std::mutex> lock(mutex_);
   prune();
   if(active_>=max_active_)return failure(ErrorCode::queue_full,"active limit reached",true);
   // Expired entries settle even if not currently eligible to execute.
   for(auto& item:waiting_)if(item.deadline && now_ms>=item.deadline){
    item.state->cancellation.cancel();
    item.state->settle(failure(ErrorCode::timeout,"job deadline exceeded"),JobStatus::failed);
   }
   prune();
   auto selected=waiting_.end();
   for(auto it=waiting_.begin();it!=waiting_.end();++it){
    if(it->ready_at>now_ms)continue;
    if(selected==waiting_.end()||it->policy.priority>selected->policy.priority||
      (it->policy.priority==selected->policy.priority&&it->order<selected->order))selected=it;
   }
   if(selected==waiting_.end())return failure(ErrorCode::not_found,"no runnable jobs");
   job=std::move(*selected);waiting_.erase(selected);++active_;active_states_.push_back(job.state);
   job.state->change(JobStatus::active);
  }
  struct ActiveGuard {JobQueue& self;std::shared_ptr<detail::JobState> state;~ActiveGuard(){{std::lock_guard<std::mutex> lock(self.mutex_);--self.active_;auto& active=self.active_states_;active.erase(std::remove(active.begin(),active.end(),state),active.end());}self.idle_.notify_all();}} guard{*this,job.state};
  const auto job_id=job.state->id;
  const auto state=job.state;
  Result<std::any> result=failure(ErrorCode::exception,"job did not execute");
  try{
   if(job.state->cancellation.cancelled())result=failure(ErrorCode::cancelled,"job cancelled");
   else result=job.handler(job.state->cancellation);
  }catch(const std::exception& e){result=failure(ErrorCode::exception,e.what());}
  catch(...){result=failure(ErrorCode::exception,"unknown job exception");}
  if(job.state->cancellation.cancelled()){
   job.state->settle(failure(ErrorCode::cancelled,"job cancelled"),JobStatus::cancelled);
  } else if(!result.ok()&&result.error().retryable&&job.attempts<job.policy.retries){
   ++job.attempts;
   std::uint64_t delay=job.policy.backoff_ms;
   if(job.policy.exponential_backoff){
    const auto shift=std::min(job.attempts-1U,63U);
    delay=(delay>(std::numeric_limits<std::uint64_t>::max()>>shift))?
      std::numeric_limits<std::uint64_t>::max():delay<<shift;
   }
   job.ready_at=detail::add_saturated(now_ms,delay);
   bool abandoned=false;
   {
    std::lock_guard<std::mutex> lock(mutex_);
    if(closed_)abandoned=true;
    else{job.state->change(JobStatus::waiting);waiting_.push_back(std::move(job));}
   }
   if(abandoned||state->cancellation.cancelled())state->settle(failure(ErrorCode::cancelled,"job cancelled or queue closed"),JobStatus::cancelled);
  }else job.state->settle(result,result.ok()?JobStatus::completed:JobStatus::failed);
  return job_id;
 }
};
} // namespace krnl
