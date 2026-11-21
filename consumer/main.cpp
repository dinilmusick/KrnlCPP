#include <krnl/kernel.hpp>
#include <krnl/job_queue.hpp>
#include <cassert>
#include <string>
int main(){
  krnl::Service service({"consumer","Consumer","1",{},{} });
  auto result=service.add_operation<int,std::string>({"format","",{},{}},[](int input,const krnl::InvocationContext&,krnl::Cancellation){return krnl::Result<std::string>(std::to_string(input));});
  if(!result.ok()||!service.initialize().ok()||!service.start().ok())return 1;
  auto response=service.invoke<int,std::string>("format",42);
  if(!response.ok()||response.value()!="42")return 2;
  krnl::JobQueue host(2,1);
  auto job=host.enqueue([](krnl::Cancellation cancellation)->krnl::Result<std::any>{
    if(cancellation.cancelled())return krnl::failure(krnl::ErrorCode::cancelled,"cancelled");
    return std::any(84);
  },{},10);
  if(!job.ok()||!host.pump_one(10).ok())return 3;
  auto value=job.value().result_as<int>();
  return value.ok()&&value.value()==84?0:4;
}
