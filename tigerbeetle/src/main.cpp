#include "client.hpp"

#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/when_all.hh>
#include <seastar/util/log.hh>

#include <memory>
#include <string_view>
#include <tuple>

static seastar::logger logger("app");

struct tb_client_guard {
  tb_client_t client{};

  tb_client_guard() = default;
  tb_client_guard(const tb_client_guard &) = delete;
  tb_client_guard &operator=(const tb_client_guard &) = delete;

  ~tb_client_guard() { tb_client_deinit(&client); }
};

// Issues a single-account lookup on whichever shard this coroutine runs on.
static seastar::future<std::vector<uint8_t>> lookup_account(tb_client_t *client,
                                                            tb_uint128_t id) {
  completion_context_t ctx;
  ctx.alien_instance = &seastar::engine().alien();
  ctx.shard_id = seastar::this_shard_id();

  tb_packet_t packet{};
  packet.operation = TB_OPERATION_LOOKUP_ACCOUNTS;
  packet.data = &id;
  packet.data_size = static_cast<uint32_t>(sizeof(tb_uint128_t));
  packet.user_data = &ctx;
  packet.status = TB_PACKET_OK;

  co_return co_await send_request(client, &packet, &ctx);
}

seastar::future<> run() {
  constexpr std::string_view address = "127.0.0.1:3000";
  constexpr uint8_t cluster_id[16] = {};

  auto guard = std::make_unique<tb_client_guard>();

  const TB_INIT_STATUS init_status =
      tb_client_init(&guard->client, cluster_id, address.data(),
                     static_cast<uint32_t>(address.size()),
                     /*completion_ctx=*/0, on_tb_client_completion);

  if (init_status != TB_INIT_SUCCESS) {
    throw std::runtime_error("tb_client_init failed: " +
                             std::to_string(static_cast<int>(init_status)));
  }
  logger.info("connected on {}", address);

  tb_client_t *client = &guard->client;

  // Dispatch each lookup to a separate shard.  shard % smp::count keeps
  // this correct even when started with -c 1.
  const unsigned shard0 = 0 % seastar::smp::count;
  const unsigned shard1 = 1 % seastar::smp::count;

  auto [reply1, reply2] = co_await seastar::when_all_succeed(
      seastar::smp::submit_to(shard0,
                              [client] { return lookup_account(client, 1); }),
      seastar::smp::submit_to(shard1,
                              [client] { return lookup_account(client, 2); }));

  for (const auto &[label, reply] :
       {std::pair{"id=1", reply1}, std::pair{"id=2", reply2}}) {
    if (reply.empty()) {
      logger.info("{}: not found", label);
      continue;
    }
    const auto *acc = reinterpret_cast<const tb_account_t *>(reply.data());
    logger.info("{}: ledger={} code={} credits_posted={} debits_posted={}",
                label, acc->ledger, acc->code, acc->credits_posted,
                acc->debits_posted);
  }
}

int main(int argc, char **argv) {
  seastar::app_template app;
  return app.run(argc, argv, [] { return run(); });
}
