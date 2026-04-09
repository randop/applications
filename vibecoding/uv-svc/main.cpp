#include <uv.h>
#include <thread>
#include <atomic>
#include <iostream>
#include <chrono>
#include <vector>
#include <mutex>
#include <sstream>      // <-- ADD THIS

std::mutex cout_mutex;

// Helper to print from any thread (fixed thread id handling)
void safe_print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << msg << std::endl;
}

// Helper to convert thread::id to string
std::string thread_id_str() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

// Structure to hold per-loop data
struct LoopData {
    uv_loop_t* loop = nullptr;
    uv_async_t* stop_async = nullptr;
    std::thread* thread = nullptr;
    std::atomic<bool> running{true};
    std::string name;
};

// Background loop worker function
void background_loop_worker(LoopData* data) {
    safe_print("[" + thread_id_str() + "] " + data->name + " background loop started");

    data->loop = uv_loop_new();
    if (!data->loop) {
        safe_print("[" + thread_id_str() + "] " + data->name + " failed to create loop");
        return;
    }

    // Create stop async handle
    data->stop_async = new uv_async_t;
    uv_async_init(data->loop, data->stop_async, [](uv_async_t* handle) {
        LoopData* d = static_cast<LoopData*>(handle->data);
        safe_print("[" + thread_id_str() + "] Stop signal received in " + d->name);
        uv_stop(d->loop);
    });
    data->stop_async->data = data;

    // Example periodic timer
    uv_timer_t timer;
    uv_timer_init(data->loop, &timer);
    timer.data = data;   // pass LoopData

    uv_timer_start(&timer, [](uv_timer_t* t) {
        LoopData* d = static_cast<LoopData*>(t->data);
        safe_print("[" + thread_id_str() + "] " + d->name + " timer tick");
    }, 1000, 1000);

    // Run the loop
    while (data->running.load()) {
        uv_run(data->loop, UV_RUN_DEFAULT);
    }

    safe_print("[" + thread_id_str() + "] " + data->name + " loop stopping...");

    // Cleanup
    uv_timer_stop(&timer);
    uv_close((uv_handle_t*)&timer, nullptr);
    uv_close((uv_handle_t*)data->stop_async, [](uv_handle_t* h) { delete h; });

    uv_loop_close(data->loop);
    uv_loop_delete(data->loop);

    safe_print("[" + thread_id_str() + "] " + data->name + " background loop finished");
}

// Main thread loop
void main_loop() {
    safe_print("[" + thread_id_str() + "] Main loop started");

    uv_loop_t* loop = uv_default_loop();

    uv_timer_t main_timer;
    uv_timer_init(loop, &main_timer);
    uv_timer_start(&main_timer, [](uv_timer_t*) {
        safe_print("[" + thread_id_str() + "] Main loop timer tick");
    }, 1500, 1500);

    uv_run(loop, UV_RUN_DEFAULT);

    uv_timer_stop(&main_timer);
    uv_close((uv_handle_t*)&main_timer, nullptr);
    uv_loop_close(loop);

    safe_print("[" + thread_id_str() + "] Main loop finished");
}

int main() {
    safe_print("[" + thread_id_str() + "] Program starting...");

    std::vector<LoopData*> background_loops(2);

    for (int i = 0; i < 2; ++i) {
        background_loops[i] = new LoopData();
        background_loops[i]->name = "Background-" + std::to_string(i + 1);
        background_loops[i]->thread = new std::thread(background_loop_worker, background_loops[i]);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    main_loop();

    // Graceful shutdown
    safe_print("[" + thread_id_str() + "] Shutting down background loops...");

    for (auto* data : background_loops) {
        data->running = false;
        if (data->stop_async) {
            uv_async_send(data->stop_async);
        }
    }

    for (auto* data : background_loops) {
        if (data->thread && data->thread->joinable()) {
            data->thread->join();
        }
        delete data->thread;
        delete data;
    }

    safe_print("[" + thread_id_str() + "] All loops stopped. Program exiting.");
    return 0;
}
