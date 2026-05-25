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

// ---------------------------------------------------------------------------
// Per-request completion context.
//
// Replaces the original pthread mutex + condvar approach.  TigerBeetle's
// internal thread fires on_tb_client_completion(), which uses
// seastar::alien::submit_to() to marshal the result back to the Seastar
// reactor (shard 0) and fulfill the promise.  The calling coroutine simply
// co_awaits the associated future.
//
// Lifetime: one context per in-flight request; must remain valid until the
// co_await in send_request() returns.
// ---------------------------------------------------------------------------
struct completion_context_t {
  seastar::promise<std::vector<uint8_t>> promise;

  // Set to &seastar::engine().alien() from the Seastar thread *before*
  // calling send_request().  Used by the TB callback thread to re-enter
  // the reactor.
  seastar::alien::instance *alien_instance{nullptr};
};

// Declared here so main.cpp can pass it to tb_client_init().
void on_tb_client_completion(uintptr_t context, tb_packet_t *packet,
                             uint64_t timestamp, const uint8_t *data,
                             uint32_t size);

// Submits *packet* asynchronously and suspends the calling coroutine until
// the completion callback fires.  Returns the raw reply bytes on success;
// throws on TB_CLIENT_* or TB_PACKET_* errors.
seastar::future<std::vector<uint8_t>> send_request(tb_client_t *client,
                                                   tb_packet_t *packet,
                                                   completion_context_t *ctx);
