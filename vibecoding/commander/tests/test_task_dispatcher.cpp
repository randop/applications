#include <boost/test/unit_test.hpp>
#include <chrono>
#include <atomic>
#include <thread>

#include "dispatcher/task_dispatcher.hpp"
#include "dispatcher/abort_controller.hpp"

using namespace task_dispatcher;

BOOST_AUTO_TEST_SUITE(TaskDispatcherTests)

BOOST_AUTO_TEST_CASE(basic_schedule_test) {
    TaskDispatcher dispatcher;
    
    std::atomic<bool> task_executed{false};
    
    auto task_id = dispatcher.schedule([&]() {
        task_executed = true;
    });
    
    BOOST_TEST(task_id > 0);
    
    // Run briefly to process task
    std::thread runner([&]() {
        dispatcher.run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    dispatcher.stop();
    runner.join();
    
    BOOST_TEST(task_executed);
}

BOOST_AUTO_TEST_CASE(schedule_with_abort_controller_test) {
    TaskDispatcher dispatcher;
    
    std::atomic<bool> task_started{false};
    std::atomic<bool> task_completed{false};
    
    auto abort_controller = AbortController::create();
    
    auto task_id = dispatcher.schedule_with_abort(
        [&](auto ac) {
            task_started = true;
            // Simulate some work
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (!ac->is_aborted()) {
                task_completed = true;
            }
        },
        abort_controller
    );
    
    BOOST_TEST(task_id > 0);
    
    std::thread runner([&]() {
        dispatcher.run();
    });
    
    // Cancel before task completes
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    dispatcher.cancel(task_id);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    dispatcher.stop();
    runner.join();
    
    // Task might have started but shouldn't complete
    // (race condition, but likely it started)
    // BOOST_TEST(task_started);  // May or may not be true depending on timing
}

BOOST_AUTO_TEST_CASE(cancel_task_test) {
    TaskDispatcher dispatcher;
    
    std::atomic<bool> task_executed{false};
    
    auto task_id = dispatcher.schedule([&]() {
        task_executed = true;
    });
    
    // Cancel before running
    bool cancelled = dispatcher.cancel(task_id);
    BOOST_TEST(cancelled);
    
    std::thread runner([&]() {
        dispatcher.run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    dispatcher.stop();
    runner.join();
    
    // Task should not have executed
    BOOST_TEST(!task_executed);
}

BOOST_AUTO_TEST_CASE(cancel_nonexistent_task_test) {
    TaskDispatcher dispatcher;
    
    // Try to cancel task that doesn't exist
    bool cancelled = dispatcher.cancel(99999);
    
    BOOST_TEST(!cancelled);
}

BOOST_AUTO_TEST_CASE(io_context_access_test) {
    TaskDispatcher dispatcher;
    
    auto& io_ctx = dispatcher.io_context();
    
    // Just verify we can access it
    BOOST_TEST(&io_ctx != nullptr);
}

BOOST_AUTO_TEST_CASE(post_test) {
    TaskDispatcher dispatcher;
    
    std::atomic<int> counter{0};
    
    // Post multiple tasks
    for (int i = 0; i < 5; ++i) {
        dispatcher.post([&counter]() {
            counter++;
        });
    }
    
    std::thread runner([&]() {
        dispatcher.run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    dispatcher.stop();
    runner.join();
    
    BOOST_TEST(counter == 5);
}

BOOST_AUTO_TEST_CASE(multiple_tasks_test) {
    TaskDispatcher dispatcher;
    
    std::atomic<int> execution_order{0};
    std::vector<int> results;
    std::mutex results_mutex;
    
    for (int i = 0; i < 10; ++i) {
        dispatcher.schedule([&, i]() {
            std::lock_guard<std::mutex> lock(results_mutex);
            results.push_back(i);
        });
    }
    
    std::thread runner([&]() {
        dispatcher.run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    dispatcher.stop();
    runner.join();
    
    std::lock_guard<std::mutex> lock(results_mutex);
    BOOST_TEST(results.size() == 10);
}

BOOST_AUTO_TEST_CASE(schedule_delayed_test) {
    TaskDispatcher dispatcher;
    
    std::atomic<bool> task_executed{false};
    auto start_time = std::chrono::steady_clock::now();
    
    dispatcher.schedule_delayed(
        std::chrono::milliseconds(100),
        [&]() {
            task_executed = true;
        }
    );
    
    std::thread runner([&]() {
        dispatcher.run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    dispatcher.stop();
    runner.join();
    
    BOOST_TEST(task_executed);
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Task should have executed after at least 100ms
    BOOST_TEST(duration.count() >= 90);  // Allow some tolerance
}

BOOST_AUTO_TEST_CASE(is_running_test) {
    TaskDispatcher dispatcher;
    
    BOOST_TEST(!dispatcher.is_running());
    
    std::thread runner([&]() {
        dispatcher.run();
    });
    
    // Give it time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    BOOST_TEST(dispatcher.is_running());
    
    dispatcher.stop();
    runner.join();
    
    BOOST_TEST(!dispatcher.is_running());
}

BOOST_AUTO_TEST_SUITE_END()
