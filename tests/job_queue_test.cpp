#include "krnl/job_queue.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <atomic>
#include <thread>
#include <future>
using namespace krnl;
int main(){
 JobQueue q(3,2);
 std::vector<int> order;
 auto low=q.enqueue([&](Cancellation){order.push_back(1);return Result<std::any>(std::any(1));},{1},100);
 auto high=q.enqueue([&](Cancellation){order.push_back(2);return Result<std::any>(std::any(2));},{5},100);
 auto equal=q.enqueue([&](Cancellation){order.push_back(3);return Result<std::any>(std::any(3));},{5},100);
 assert(low.ok()&&high.ok()&&equal.ok()&&q.pending()==3);
 assert(!q.enqueue([](Cancellation){return Result<std::any>(std::any(0));}).ok());
 assert(q.pump_one(100).ok());assert(q.pump_one(100).ok());assert(q.pump_one(100).ok());
 assert((order==std::vector<int>{2,3,1}));
 assert(high.value().result_as<int>().value()==2);
 assert(low.value().ready()&&low.value().status()==JobStatus::completed);
 auto cancelling=q.enqueue([](Cancellation){assert(false);return Result<std::any>(std::any());});
 assert(cancelling.ok());cancelling.value().cancel();assert(cancelling.value().ready());
 assert(cancelling.value().result().error().code==ErrorCode::cancelled);assert(q.pending()==0);
 int attempt=0;
 auto retry=q.enqueue([&](Cancellation)->Result<std::any>{if(++attempt<3)return failure(ErrorCode::transport,"transient",true);return std::any(42);},
  {0,2,10,true,100},200);
 assert(retry.ok());assert(q.pump_one(200).ok()&&!retry.value().ready());
 assert(!q.pump_one(209).ok());assert(q.pump_one(210).ok());
 assert(!q.pump_one(229).ok());assert(q.pump_one(230).ok());
 assert(attempt==3&&retry.value().result_as<int>().value()==42);
 auto fail=q.enqueue([](Cancellation)->Result<std::any>{return failure(ErrorCode::exception,"non-retryable");},{0,3});
 assert(fail.ok()&&q.pump_one(0).ok()&&fail.value().status()==JobStatus::failed);
 auto deadline=q.enqueue([](Cancellation){assert(false);return Result<std::any>(std::any());},{0,0,0,false,5},100);
 assert(deadline.ok());assert(!q.pump_one(105).ok());assert(deadline.value().result().error().code==ErrorCode::timeout);
 auto pending=q.enqueue([](Cancellation){assert(false);return Result<std::any>(std::any());});assert(pending.ok());q.close();
 assert(pending.value().ready()&&pending.value().status()==JobStatus::cancelled);
 assert(!q.enqueue([](Cancellation){return Result<std::any>(std::any());}).ok());
 JobQueue parallel(4,1);std::promise<void> enter,leave;auto release=leave.get_future().share();
 auto active=parallel.enqueue([&](Cancellation){enter.set_value();release.wait();return Result<std::any>(std::any(7));});
 assert(active.ok());std::thread worker([&]{assert(parallel.pump_one(0).ok());});enter.get_future().wait();
 assert(parallel.active()==1);assert(!parallel.pump_one(0).ok());leave.set_value();worker.join();
 assert(active.value().result_as<int>().value()==7);
 // A close during an active handler cooperatively cancels its outcome.
 JobQueue closing(1);std::promise<void> running,resume;
 auto resume_gate=resume.get_future().share();
 auto job=closing.enqueue([&](Cancellation cancelled){running.set_value();resume_gate.wait();
  assert(cancelled.cancelled());return Result<std::any>(std::any(100));});
 assert(job.ok());std::thread close_worker([&]{assert(closing.pump_one(0).ok());});
 running.get_future().wait();closing.close();resume.set_value();close_worker.join();
 assert(job.value().result().error().code==ErrorCode::cancelled);
 // Invalid type requests return a structured result, not std::bad_any_cast.
 assert(high.value().result_as<std::string>().error().code==ErrorCode::type_mismatch);
}
