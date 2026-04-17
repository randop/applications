#include <array>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/file.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/timer.hh>
#include <seastar/util/log.hh>
#include <string>

using namespace seastar;

logger applog("app");

seastar::future<> write_file(size_t core_num) {
  struct alignas(512) WriteState {
    file f;
    uint64_t pos;
    std::array<char, 512> buffer;
    WriteState(file &&f_in, size_t core_num) : f(std::move(f_in)), pos(0) {
      std::fill(buffer.begin(), buffer.end(), 'A' + (core_num % 26));
    }
  };
  std::string filename =
      std::string("file_") + std::to_string(core_num) + ".dat";
  return open_file_dma(filename, open_flags::create | open_flags::truncate |
                                     open_flags::rw)
      .then([core_num](file f) {
        const size_t total_size = 10ULL * 1024 * 1024; // 10MB
        const size_t chunk_size = 512; // must be multiple of 512 for DMA
        return seastar::do_with(
            WriteState(std::move(f), core_num),
            [total_size, chunk_size](WriteState &state) {
              return seastar::do_until(
                         [&state, total_size] {
                           return state.pos >= total_size;
                         },
                         [&state, chunk_size]() {
                           size_t to_write =
                               std::min(chunk_size, total_size - state.pos);
                           return state.f
                               .dma_write(state.pos, state.buffer.data(),
                                          to_write, nullptr)
                               .then([&state, to_write](size_t result) {
                                 if (result == 0) {
                                   // Avoid infinite loop if write returns 0
                                   state.pos = total_size;
                                 } else {
                                   state.pos += result;
                                 }
                               });
                         })
                  .then([&state]() { return state.f.close(); });
            });
      });
}

seastar::future<> run() {
  applog.info("starting parallel file writes");
  return seastar::when_all_succeed(
             smp::submit_to(1, [=] { return write_file(1); }),
             smp::submit_to(2, [=] { return write_file(2); }),
             smp::submit_to(3, [=] { return write_file(3); }))
      .then([](std::tuple<> _t) {
        applog.info("all file writes completed");
        return seastar::make_ready_future<>();
      });
}

int main(int argc, char **argv) {
  seastar::app_template app;
  return app.run(argc, argv, run);
}
