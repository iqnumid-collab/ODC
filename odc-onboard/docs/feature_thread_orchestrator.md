# Feature: Thread Orchestrator

## Overview

A custom C++20 async task execution engine with a thread pool, futures, cancellation, and continuations. It powers async operations throughout odc-onboard including event delivery, health checks, and stats collection.

## Architecture

```
ThreadOrchestrator
├── WorkerPool
│   ├── Worker 1
│   ├── Worker 2
│   ├── ...
│   └── Worker N
├── TaskQueue
└── ControlState (pause/stop/min/max)
```

## Configuration

```cpp
orch::ThreadOrchestrator orch({
    .initial_threads = 4,
    .min_threads = 2,
    .max_threads = 8,
    .on_error = [](std::exception_ptr ep) { /* handler */ }
});
```

| Field | Description | Default |
|-------|-------------|---------|
| `initial_threads` | Workers spawned at startup | `0` = hardware concurrency |
| `min_threads` | Minimum worker count | `2` |
| `max_threads` | Maximum worker count | `0` = 2x hardware concurrency |
| `on_error` | Exception handler callback | (none) |

## Task Submission

### With Cancellation Token

```cpp
auto future = orch.submit([](auto& token) -> int {
    while (!token.is_cancelled()) {
        // do work
    }
    return 42;
});
```

### Simple (No Token)

```cpp
auto future = orch.submit_simple([]() -> int {
    return 42;
});
```

## OrchestratorFuture

| Method | Description |
|--------|-------------|
| `get()` | Blocks until result is ready |
| `cancel()` | Sets cancellation flag on the task |
| `is_valid()` | Checks if future has valid state |
| `is_ready()` | Non-blocking readiness check |
| `is_cancelled()` | Checks if task was cancelled |
| `then(F, policy)` | Registers a continuation, returns chained future |

### Continuation Policies

| Policy | Behavior |
|--------|----------|
| `Pool` | Continuation runs on a worker thread (async) |
| `Inline` | Continuation runs on the completing thread (sync) |

## Control

| Method | Description |
|--------|-------------|
| `pause()` | Pauses task execution (tasks queue up) |
| `resume()` | Resumes task execution |
| `resize(n)` | Dynamically adjusts worker count (clamped to min/max) |
| `shutdown()` | Stops all workers, drains and discards pending tasks |

## Metrics

```cpp
auto m = orch.get_metrics();
// m.active_tasks, m.pending_tasks, m.completed_tasks, m.worker_count
```

## Cancellation

- `CancellationSource` creates a `CancellationToken`
- Token is passed to tasks submitted via `submit()`
- Tasks check `token.is_cancelled()` for cooperative cancellation
- Cancellation propagates through `.then()` chains

## Source Files

- `thirdparty/thread_orchestrator/orchestrator.hpp`
- `thirdparty/thread_orchestrator/future.hpp`
- `thirdparty/thread_orchestrator/cancellation_token.hpp`
- `thirdparty/thread_orchestrator/metrics.hpp`
- `thirdparty/thread_orchestrator/detail/worker_pool.hpp`
- `thirdparty/thread_orchestrator/detail/task_queue.hpp`
- `thirdparty/thread_orchestrator/detail/control_state.hpp`
- `thirdparty/thread_orchestrator/detail/unique_function.hpp`
