cmake_minimum_required(VERSION 3.16)
project(ThreadOrchestrator VERSION 1.0.0 LANGUAGES CXX)

add_library(thread_orchestrator INTERFACE)
target_include_directories(thread_orchestrator INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(thread_orchestrator INTERFACE cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(thread_orchestrator INTERFACE Threads::Threads)

option(THREAD_ORCHESTRATOR_BUILD_TESTS "Build tests" OFF)
option(THREAD_ORCHESTRATOR_BUILD_EXAMPLES "Build examples" OFF)

if(THREAD_ORCHESTRATOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(THREAD_ORCHESTRATOR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
