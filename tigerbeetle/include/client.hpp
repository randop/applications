#pragma once

#include <seastar/core/alien.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/future.hh>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <tb_client.h>
}

struct completion_context_t {
  seastar::promise<std::vector<uint8_t>> promise;
  seastar::alien::instance *alien_instance{nullptr};
  unsigned shard_id{0};
};

void on_tb_client_completion(uintptr_t context, tb_packet_t *packet,
                             uint64_t timestamp, const uint8_t *data,
                             uint32_t size);

seastar::future<std::vector<uint8_t>> send_request(tb_client_t *client,
                                                   tb_packet_t *packet,
                                                   completion_context_t *ctx);
