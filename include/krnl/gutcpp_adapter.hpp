#pragma once
// Optional integration adapter: include only when pinned GutCPP is on include path.
#include "krnl/kernel.hpp"
#include <gut/result.hpp>
namespace krnl {
// Consumer supplies error classification when GutCPP string codes cannot map 1:1.
template<class T> Result<T> from_gut(gut::Result<T> result,ErrorCode default_code=ErrorCode::exception){
 if(result.ok())return result.value();
 return failure(default_code,result.error().code+": "+result.error().message);
}
}
