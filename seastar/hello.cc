#include "seastar/core/app-template.hh"
#include "seastar/core/reactor.hh"
#include "seastar/core/sstring.hh"
#include <seastar/core/seastar.hh>
#include <seastar/util/log.hh>

using namespace seastar;

future<sstring> compute_something_asynchronously() {
  // pretend some complex computation has taken place
  return make_ready_future<sstring>("world");
}

using namespace seastar;
logger applog("app");

int main(int argc, char **argv) {
  app_template app;
  app.run(argc, argv, []() -> future<> {
    compute_something_asynchronously()
        .then([](sstring who) {
          // function callback
          applog.info("hello, {}", who);
        })
        .handle_exception([](std::exception_ptr) {
          // ignore error
        });

    return make_ready_future<>();
  });
}
