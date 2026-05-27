#include "client.hpp"

#include <seastar/core/alien.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/coroutine/maybe_yield.hh>

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
  auto fut = ctx->promise.get_future();

  const TB_CLIENT_STATUS status = tb_client_submit(client, packet);
  if (status != TB_CLIENT_OK) {
    throw std::runtime_error("tb_client_submit failed: " +
                             std::to_string(static_cast<int>(status)));
  }

  co_return co_await std::move(fut);
}

seastar::future<> tb_client_service::start(const uint8_t cluster_id[16],
                                           std::string_view address) {
  return seastar::with_gate(
      _gate, [this, cluster_id, address]() -> seastar::future<> {
        const TB_INIT_STATUS init_status = tb_client_init(
            &_client, cluster_id, address.data(),
            static_cast<uint32_t>(address.size()), 0, on_tb_client_completion);

        if (init_status != TB_INIT_SUCCESS) {
          throw std::runtime_error(
              "tb_client_init failed: " +
              std::to_string(static_cast<int>(init_status)));
        }
        co_return;
      });
}

seastar::future<> tb_client_service::stop() {
  co_await _gate.close();
  co_return;
}

seastar::future<std::vector<uint8_t>>
tb_client_service::lookup_account(tb_uint128_t id) {
  return seastar::with_gate(
      _gate, [this, id]() -> seastar::future<std::vector<uint8_t>> {
        completion_context_t ctx;
        ctx.alien_instance = &seastar::engine().alien();
        ctx.shard_id = seastar::this_shard_id();

        tb_packet_t packet{};
        packet.operation = TB_OPERATION_LOOKUP_ACCOUNTS;
        packet.data = const_cast<void *>(static_cast<const void *>(&id));
        packet.data_size = static_cast<uint32_t>(sizeof(tb_uint128_t));
        packet.user_data = &ctx;
        packet.status = TB_PACKET_OK;

        co_return co_await send_request(&_client, &packet, &ctx);
      });
}
