#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/timer.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/smp.hh>
#include <seastar/util/log.hh>
#include <iostream>
#include <chrono>
#include <cstdio>
#include <string>

using namespace seastar;

logger applog("app");

seastar::future<> start_delayed_writer() {
    return seastar::do_with(
        seastar::timer{},
        seastar::promise<>{},
        [](auto& t, auto& p) {
            t.set_callback([&p]() {
                std::FILE* fp = popen("./benchmark.py 10", "r");
                if (fp) {
                    char buffer[256];
                    while (std::fgets(buffer, sizeof(buffer), fp)) {
                        applog.info("{}", buffer);
                    }
                    pclose(fp);
                }
                p.set_value();
            });
            t.arm(std::chrono::seconds(1));
            return p.get_future();
        }
    );
}

seastar::future<int> execute_task_on_core(int core_id, int duration) {
    return seastar::smp::submit_to(core_id, [core_id, duration] {
        applog.info("executing task on core {}", core_id);
        std::string cmd = "./benchmark.py " + std::to_string(duration);
        std::FILE* fp = popen(cmd.c_str(), "r");
        int line_count = 0;
        if (fp) {
            char buffer[256];
            while (std::fgets(buffer, sizeof(buffer), fp)) {
                applog.info("{}", buffer);
                line_count++;
            }
            pclose(fp);
        }
        applog.info("completed, lines: {}", line_count);
        return line_count;
    });
}

seastar::future<int> execute_remote_task_collect_result() {
    return seastar::when_all(
        execute_task_on_core(1, 300),
        execute_task_on_core(2, 300),
        execute_task_on_core(3, 300)
    ).then([](std::tuple<seastar::future<int>, seastar::future<int>, seastar::future<int>>&& results) {
        int total = 0;
        auto& [f1, f2, f3] = results;
        if (f1.available() && !f1.failed()) {
            total += f1.get();
        }
        if (f2.available() && !f2.failed()) {
            total += f2.get();
        }
        if (f3.available() && !f3.failed()) {
            total += f3.get();
        }
        return total;
    });
}

seastar::future<> run() {
    applog.info("starting application");
    
    int result = co_await execute_remote_task_collect_result();
    applog.info("received result: {}", result);
    
    applog.info("application finished");
}

int main(int argc, char** argv) {
    seastar::app_template app;
    return app.run(argc, argv, run);
}
