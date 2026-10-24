include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

function(add_orch_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE thread_orchestrator GTest::gtest_main)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_orch_test(test_unique_function)
add_orch_test(test_cancellation_token)
add_orch_test(test_future)
add_orch_test(test_task_queue)
add_orch_test(test_orchestrator)
add_orch_test(test_stress)
