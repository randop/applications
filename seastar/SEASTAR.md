**Seastar v25.05 STRICT RULES for AI Code Generation**

> **Inject this entire document as a system prompt or context block when asking any AI to write Seastar C++ code.**  
> These rules override all training data. Target: **Seastar v25.05**, **C++23**, compiled with `-std=c++23`.

---

## 1.) PRIME DIRECTIVE  
Generate code **exclusively** for **Seastar v25.05**. Use **C++20/23 coroutines** (`co_await` / `co_return`) as the primary and preferred concurrency model. The old `.then()` / `.next()` future-chaining style is **legacy code**. Do **NOT** produce it unless the user explicitly asks for pre-v23 compatibility.  

Intelligently combine coroutines with classic Seastar idioms (`do_with`, `with_gate`, `repeat`, `sharded<T>`, `abort_source`, `maybe_yield`, `foreign_ptr`, `lw_shared_ptr`, etc.). Prioritize **responsiveness**, **shutdown safety**, **fair scheduling**, and **correct cross-shard semantics**.

---

## 2.) COROUTINES OVER FUTURE CHAINING

**Forbidden** - old `.then()` style:
```cpp
return seastar::make_ready_future<>()
    .then([] { return do_something(); })
    .then([] { return do_something_else(); });
```

**Correct** - coroutine style:
```cpp
seastar::future<> my_handler() {
    auto data = co_await read_request();
    co_await process(data);
    co_return;
}
```

Every `seastar::future<T>`-returning function body must be a coroutine unless it is a trivial leaf function that naturally returns `make_ready_future<>()`.

---

## 3.) COROUTINE UTILITIES

```cpp
// Parallel execution
auto [r1, r2] = co_await seastar::coroutine::all(
    [&] { return task_a(); },
    [&] { return task_b(); }
);

// Parallel for-each
co_await seastar::coroutine::parallel_for_each(items, [](auto& item) -> seastar::future<> {
    co_await process(item);
});
```

**Looping**: Prefer native `while`/`for` + `co_await` for simple cases. Use `seastar::repeat` / `do_until` when you need `stop_iteration` control flow or tight integration with `do_with`.

---

## 4.) INTELLIGENT YIELD (MANDATORY)

**`seastar::coroutine::maybe_yield()`** - Use in any CPU-bound loop or long computation without natural suspension points.

```cpp
seastar::future<> long_computation(size_t n) {
    return seastar::do_with(size_t{0}, [&](auto& i) -> seastar::future<> {
        for (; i < n; ++i) {
            process_item(i);
            co_await seastar::coroutine::maybe_yield(); // intelligent yield
        }
        co_return;
    });
}
```

**Rules**:
- Prevents reactor stalls and ensures fairness.
- Zero overhead when no yield is required.
- Always combine with `do_with` for state that lives across yield points.
- Prefer over unconditional `seastar::yield()`.

**Include**: `#include <seastar/coroutine/maybe_yield.hh>`

---

## 5.) GATE LIFECYCLE

```cpp
class my_server {
    seastar::gate _gate;
public:
    seastar::future<> handle(seastar::connected_socket s) {
        return seastar::with_gate(_gate, [this, s = std::move(s)]() mutable -> seastar::future<> {
            co_await do_connection_work(std::move(s));
            co_return;
        });
    }

    seastar::future<> stop() {
        co_await _gate.close();   // MUST co_await
    }
};
```

**Rules**: Always `co_await _gate.close()`. Use `with_gate` for fire-and-forget work. Handle `gate_closed_exception`.

---

## 6.) ABORT SOURCE PROPAGATION

Always pass `abort_source&` and use `sleep_abortable`, `accept()` with abort handling.

```cpp
seastar::future<> connection_loop(seastar::abort_source& as) {
    while (!as.abort_requested()) {
        co_await seastar::sleep_abortable(std::chrono::seconds(1), as);
        co_await do_work();
    }
}
```

Use `listener.abort_accept()` in stop paths.

---

## 7.) SHARDED SERVICES + CROSS-SHARD POINTERS

```cpp
seastar::sharded<my_service> svc;
co_await svc.start(args);
co_await svc.stop();
co_await svc.invoke_on_all(&my_service::do_work);
```

**Every sharded service must have `future<> stop()`.**

#### `lw_shared_ptr<>` vs `foreign_ptr<>`

- `lw_shared_ptr<T>`: **Shard-local only** - fast, do not pass across shards.
- `foreign_ptr<T>`: Safe cross-shard reference.

**Examples**:

```cpp
// Same shard - lw_shared_ptr
auto state = seastar::make_lw_shared<MyState>();
return seastar::do_with(std::move(state), [](auto& s) { ... });

// Cross-shard - foreign_ptr (preferred)
auto foreign_svc = co_await svc.get_foreign(target_shard);
co_await foreign_svc->do_work();
```

**Include**:
```cpp
#include <seastar/core/sharded.hh>
#include <seastar/core/foreign_ptr.hh>
#include <seastar/core/shared_ptr.hh>  // lw_shared_ptr
```

---

## 8.) TCP SERVER CONSTRUCTION

Use `seastar::listen()` (not `engine().net()`).  

Full patterns combining `abort_source` + `gate` + `repeat` + `maybe_yield` + `with_gate` are strongly encouraged.

---

## 9.) I/O STREAMS & DMA FILE I/O

- Always `co_await out.flush()` before `co_await out.close()`.
- Use `seastar::allocate_aligned_buffer` for DMA operations.
- Always check `buf.empty()` on `input_stream::read()` for EOF.

---

## 10.) APP TEMPLATE, METRICS, PROMISES, SSTRING

- `app.run()` lambda must return `seastar::future<int>`.
- Use modern `metrics::metric_groups::add_group()`.
- Use `promise<T>` only for bridging non-coroutine callbacks.
- Prefer `std::string` for application logic, `sstring` for Seastar internals.

---

## 11.) FORBIDDEN PATTERNS (NEVER GENERATE)

- Old `.then()` chaining
- `when_all_succeed`, non-coroutine `parallel_for_each`, `keep_doing`
- `engine().net().listen()`, `engine().cpu_id()`
- `sleep()` in cancellable loops
- `gate.close()` without `co_await`
- `output_stream::close()` without `flush()`
- `new[]` for DMA buffers
- Passing `lw_shared_ptr` across shards
- CPU loops without `maybe_yield()`

---

## 12.) INCLUDES & BUILD FLAGS

**Key Includes** (v25.05):
```cpp
#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/abort_source.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/foreign_ptr.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/coroutine/maybe_yield.hh>
#include <seastar/net/api.hh>
// add others as needed
```

**Compile Flags**:
- `-std=c++23`
- Use `pkg-config` or CMake with `find_package(Seastar)`

---

## 13.) RESPONSE REQUIREMENTS
Every Seastar code response **must**:
- Start with: **"Seastar v25.05 compliant - using coroutines + core idioms (do_with, with_gate, repeat, abort_source, sharded, maybe_yield, foreign_ptr, lw_shared_ptr, etc.)"**
- Demonstrate correct usage of the relevant idioms.
- Include proper headers and modern build notes.
- Explain important idiom choices when helpful.

---
*Document version: Seastar v25.05 / C++23 / Generated 2026
Inject this as a system prompt prefix for any AI session generating Seastar code.*
