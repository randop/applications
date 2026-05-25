#include "client.hpp"

#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/util/log.hh>

#include <cstring>
#include <memory>
#include <string_view>

static seastar::logger logger("app");

struct tb_client_guard {
  tb_client_t client{};

  tb_client_guard() = default;
  tb_client_guard(const tb_client_guard &) = delete;
  tb_client_guard &operator=(const tb_client_guard &) = delete;

  ~tb_client_guard() { tb_client_deinit(&client); }
};

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

  constexpr size_t ACCOUNTS_LEN = 2;
  tb_uint128_t ids[ACCOUNTS_LEN] = {1, 2};

  // Capture the alien handle now of the Seastar thread.
  completion_context_t ctx;
  ctx.alien_instance = &seastar::engine().alien();

  tb_packet_t packet{};
  packet.operation = TB_OPERATION_LOOKUP_ACCOUNTS;
  packet.data = ids;
  packet.data_size = static_cast<uint32_t>(sizeof(tb_uint128_t) * ACCOUNTS_LEN);
  packet.user_data = &ctx;
  packet.status = TB_PACKET_OK;

  auto reply = co_await send_request(&guard->client, &packet, &ctx);

  if (reply.empty()) {
    logger.info("no accounts found");
    co_return;
  }

  const size_t n = reply.size() / sizeof(tb_account_t);
  const auto *accounts = reinterpret_cast<const tb_account_t *>(reply.data());

  logger.info("found {} account(s)", n);
  for (size_t i = 0; i < n; ++i) {
    // tb_uint128_t: print the low 64-bit word for logging purposes.
    const uint64_t id_lo =
        static_cast<uint64_t>(accounts[i].id); // truncates to low word
    logger.info("  account[{}]: id={} ledger={} code={} "
                "credits_posted={} debits_posted={}",
                i, id_lo, accounts[i].ledger, accounts[i].code,
                accounts[i].credits_posted, accounts[i].debits_posted);
  }
}

int main(int argc, char **argv) {
  seastar::app_template app;
  return app.run(argc, argv, [] { return run(); });
}
