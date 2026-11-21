#include <krnl/kernel.hpp>
#include <cassert>
#include <string>
int main(){
  krnl::Service service({"consumer","Consumer","1",{},{} });
  auto result=service.add_operation<int,std::string>({"format","",{},{}},[](int input,const krnl::InvocationContext&,krnl::Cancellation){return krnl::Result<std::string>(std::to_string(input));});
  if(!result.ok()||!service.initialize().ok()||!service.start().ok())return 1;
  auto response=service.invoke<int,std::string>("format",42);
  return response.ok()&&response.value()=="42"?0:2;
}
