#include <boost/test/unit_test.hpp>
#include <chrono>
#include <thread>
#include <atomic>

#include "dispatcher/abort_controller.hpp"

using namespace task_dispatcher;

BOOST_AUTO_TEST_SUITE(AbortControllerTests)

BOOST_AUTO_TEST_CASE(basic_abort_test) {
    auto controller = AbortController::create();
    auto signal = controller->signal();
    
    BOOST_TEST(!signal->aborted());
    BOOST_TEST(!controller->is_aborted());
    
    controller->abort("Test abort");
    
    BOOST_TEST(signal->aborted());
    BOOST_TEST(controller->is_aborted());
    
    auto reason = signal->reason();
    BOOST_TEST(reason.has_value());
    BOOST_TEST(reason.value() == "Test abort");
}

BOOST_AUTO_TEST_CASE(abort_with_handler_test) {
    auto controller = AbortController::create();
    auto signal = controller->signal();
    
    std::atomic<bool> handler_called{false};
    std::string received_reason;
    
    signal->add_event_listener([&](const std::string& reason) {
        handler_called = true;
        received_reason = reason;
    });
    
    controller->abort("Handler test");
    
    // Give handler time to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    BOOST_TEST(handler_called);
    BOOST_TEST(received_reason == "Handler test");
}

BOOST_AUTO_TEST_CASE(abort_with_timeout_test) {
    auto controller = AbortController::create(std::chrono::milliseconds(50));
    auto signal = controller->signal();
    
    BOOST_TEST(!signal->aborted());
    
    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    BOOST_TEST(signal->aborted());
    auto reason = signal->reason();
    BOOST_TEST(reason.has_value());
    BOOST_TEST(reason.value().find("timed out") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(abort_error_exception_test) {
    auto controller = AbortController::create();
    auto signal = controller->signal();
    
    controller->abort("Test error");
    
    try {
        signal->throw_if_aborted();
        BOOST_FAIL("Expected exception");
    } catch (const AbortError& e) {
        BOOST_TEST(std::string(e.what()) == "Test error");
    }
}

BOOST_AUTO_TEST_CASE(multiple_handlers_test) {
    auto controller = AbortController::create();
    auto signal = controller->signal();
    
    std::atomic<int> call_count{0};
    
    signal->add_event_listener([&](const std::string&) { call_count++; });
    signal->add_event_listener([&](const std::string&) { call_count++; });
    signal->add_event_listener([&](const std::string&) { call_count++; });
    
    controller->abort("Multi handler test");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    BOOST_TEST(call_count == 3);
}

BOOST_AUTO_TEST_CASE(wait_for_abort_test) {
    auto controller = AbortController::create();
    auto signal = controller->signal();
    
    std::thread t([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        controller->abort("Delayed abort");
    });
    
    signal->wait_for_abort();
    
    BOOST_TEST(signal->aborted());
    
    t.join();
}

BOOST_AUTO_TEST_CASE(wait_for_abort_with_timeout_success_test) {
    auto controller = AbortController::create();
    auto signal = controller->signal();
    
    std::thread t([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        controller->abort("Quick abort");
    });
    
    bool aborted = signal->wait_for_abort(std::chrono::milliseconds(200));
    
    BOOST_TEST(aborted);
    
    t.join();
}

BOOST_AUTO_TEST_CASE(wait_for_abort_with_timeout_failure_test) {
    auto controller = AbortController::create();
    auto signal = controller->signal();
    
    // Don't abort, should timeout
    bool aborted = signal->wait_for_abort(std::chrono::milliseconds(50));
    
    BOOST_TEST(!aborted);
    BOOST_TEST(!signal->aborted());
}

BOOST_AUTO_TEST_SUITE_END()
