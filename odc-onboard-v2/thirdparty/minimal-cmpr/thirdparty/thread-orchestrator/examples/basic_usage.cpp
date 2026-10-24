#include <thread_orchestrator/thread_orchestrator.hpp>

#include <chrono>
#include <iostream>

int main() {
    orch::Config cfg;
    cfg.initial_threads = 4;
    cfg.min_threads = 2;
    cfg.max_threads = 8;
    cfg.on_error = [](std::exception_ptr ep) {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) { std::cerr << "Error: " << e.what() << "\n"; }
    };

    orch::ThreadOrchestrator pool(cfg);

    // Simple submit
    auto f1 = pool.submit_simple([] { return 42; });
    std::cout << "Result: " << f1.get() << "\n";

    // Submit with cancellation token
    auto f2 = pool.submit([](orch::CancellationToken token) {
        for (int i = 0; i < 100 && !token.is_cancelled(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return std::string("done");
    });

    // .then() continuation
    auto f3 = pool.submit_simple([] { return 10; })
        .then([](int v) { return v * 2; })
        .then([](int v) { return v + 1; });
    std::cout << "Chain: " << f3.get() << "\n"; // 21

    // Cancel
    f2.cancel();
    try { f2.get(); }
    catch (const orch::CancelledException&) { std::cout << "Task cancelled\n"; }

    // Metrics
    auto m = pool.get_metrics();
    std::cout << "Completed: " << m.completed_tasks << ", Workers: " << m.worker_count << "\n";

    // Resize
    pool.resize(6);
    std::cout << "Resized to 6 workers\n";

    // Pause/resume
    pool.pause();
    auto f4 = pool.submit_simple([] { return 99; });
    std::cout << "Paused — task pending\n";
    pool.resume();
    std::cout << "Resumed — result: " << f4.get() << "\n";

    return 0;
}
