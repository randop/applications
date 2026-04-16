#include <seastar/core/abort_source.hh>
#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/timed_out_error.hh>
#include <seastar/core/with_timeout.hh>

#include <chrono>
#include <exception>
#include <iostream>

int main(int argc, char **argv) {
  seastar::app_template app;
  return app.run(argc, argv, []() -> seastar::future<> {
    std::cout << "timeout and abort demo" << std::endl;

    std::cout
        << "[1] Starting long-running future (sleep 10s) with 2s timeout...\n";

    auto long_running_future = seastar::sleep(std::chrono::seconds(10));

    return seastar::with_timeout(std::chrono::steady_clock::now() +
                                     std::chrono::seconds(2),
                                 std::move(long_running_future))
        .then([] { std::cout << "   → Long future completed (unexpected)\n"; })
        .handle_exception([](std::exception_ptr ep) {
          try {
            std::rethrow_exception(ep);
          } catch (const seastar::timed_out_error &e) {
            std::cout << "   → Timeout triggered as expected: " << e.what()
                      << "\n";
          } catch (...) {
            std::cout << "   → Unexpected exception in timeout path\n";
          }
        })
        .then([] {
          std::cout << "\n[2] Now demonstrating abortable future with "
                       "abort_source...\n";

          return seastar::do_with(
              seastar::abort_source{},
              [](seastar::abort_source &as) -> seastar::future<> {
                auto abortable_future =
                    seastar::sleep_abortable(std::chrono::seconds(10), as);

                // Schedule abort after 2 seconds
                return seastar::sleep(std::chrono::seconds(2))
                    .then([&as] {
                      std::cout
                          << "   → Requesting abort via abort_source...\n";
                      as.request_abort(); // Default exception:
                                          // abort_requested_exception
                    })
                    .then([fut = std::move(abortable_future)]() mutable
                              -> seastar::future<> { return std::move(fut); })
                    .then([] {
                      std::cout << "   → Aborted future completed normally "
                                   "(unexpected)\n";
                    })
                    .handle_exception([](std::exception_ptr ep) {
                      try {
                        std::rethrow_exception(ep);
                      } catch (const seastar::abort_requested_exception &e) {
                        std::cout << "   → Abort succeeded "
                                     "(abort_requested_exception): "
                                  << e.what() << "\n";
                      } catch (const seastar::sleep_aborted &e) {
                        std::cout << "   → Abort succeeded (sleep_aborted): "
                                  << e.what() << "\n";
                      } catch (...) {
                        std::cout
                            << "   → Unexpected exception in abort path\n";
                      }
                    });
              });
        })
        .then([] { std::cout << "DONE."; });
  });
}
