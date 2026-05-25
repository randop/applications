#include "client.hpp"

#include <seastar/core/alien.hh>
#include <seastar/core/future.hh>

void on_tb_client_completion(uintptr_t context, tb_packet_t *packet,
                             uint64_t timestamp, const uint8_t *data,
                             uint32_t size) {
  (void)context;
  (void)timestamp;

  auto *ctx = reinterpret_cast<completion_context_t *>(packet->user_data);
  const auto pkt_status = packet->status;
  std::vector<uint8_t> buf(data, data + size);

  seastar::alien::submit_to(
      *ctx->alien_instance, ctx->shard_id,
      [ctx, buf = std::move(buf), pkt_status]() mutable -> seastar::future<> {
        if (pkt_status != TB_PACKET_OK) {
          ctx->promise.set_exception(std::make_exception_ptr(std::runtime_error(
              "TB packet error: " +
              std::to_string(static_cast<int>(pkt_status)))));
        } else {
          ctx->promise.set_value(std::move(buf));
        }
        return seastar::make_ready_future<>();
      });
}

seastar::future<std::vector<uint8_t>> send_request(tb_client_t *client,
                                                   tb_packet_t *packet,
                                                   completion_context_t *ctx) {
  // Future must be extracted before submit; the callback can fire
  // and fulfil the promise before tb_client_submit() returns.
  auto fut = ctx->promise.get_future();

  const TB_CLIENT_STATUS status = tb_client_submit(client, packet);
  if (status != TB_CLIENT_OK) {
    throw std::runtime_error("tb_client_submit failed: " +
                             std::to_string(static_cast<int>(status)));
  }

  co_return co_await std::move(fut);
}
