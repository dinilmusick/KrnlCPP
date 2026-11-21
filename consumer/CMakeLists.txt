cmake_minimum_required(VERSION 3.16)
project(KrnlCppConsumer LANGUAGES CXX)
find_package(KrnlCpp CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE krnl::krnlcpp)

enable_testing()
add_test(NAME consumer COMMAND consumer)
