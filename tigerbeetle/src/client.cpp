#include "client.hpp"

#include <seastar/core/alien.hh>
#include <seastar/core/future.hh>

// ---------------------------------------------------------------------------
// on_tb_client_completion
//
// TigerBeetle invokes this on its own internal thread — NOT on the Seastar
// reactor thread.  Touching a seastar::promise from a foreign thread is
// undefined behaviour, so we use seastar::alien::submit_to() to schedule a
// lambda on shard 0 of the reactor.  The lambda then safely fulfils the
// promise, waking the co_await in send_request().
//
// The reply buffer (data/size) is only guaranteed valid for the duration of
// this callback, so we copy it into a std::vector<uint8_t> before returning.
// ---------------------------------------------------------------------------
void on_tb_client_completion(uintptr_t context, tb_packet_t *packet,
                             uint64_t timestamp, const uint8_t *data,
                             uint32_t size) {
  (void)context;
  (void)timestamp;

  auto *ctx = reinterpret_cast<completion_context_t *>(packet->user_data);
  const auto pkt_status = packet->status;

  // Copy before the callback frame unwinds.
  std::vector<uint8_t> buf(data, data + size);

  // Re-enter the Seastar reactor from the TB worker thread.
  seastar::alien::submit_to(
      *ctx->alien_instance, /*shard=*/0,
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

// ---------------------------------------------------------------------------
// send_request
//
// Coroutine wrapper around tb_client_submit().  Extracts the future from the
// context's promise *before* submitting (the callback may fire and fulfil the
// promise before tb_client_submit() even returns on a fast path), then
// suspends until the reactor-side lambda above resolves it.
// ---------------------------------------------------------------------------
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
