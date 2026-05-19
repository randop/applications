#include "client.hpp"

using namespace std;

int main() {
  tb_client_t client{};

  const uint8_t cluster_id[16] = {}; // cluster 0
  const char *address = "127.0.0.1:3000";
  uint32_t address_len = static_cast<uint32_t>(std::strlen(address));

  TB_INIT_STATUS status =
      tb_client_init(&client, cluster_id, address, address_len,
                     /*completion_ctx=*/0, on_tb_client_completion);

  if (status != TB_INIT_SUCCESS) {
    printf("tb_client_init failed: %d\n", static_cast<int>(status));
    return 1;
  }

  printf("connected on %s\n", address);

  constexpr size_t ACCOUNTS_LEN = 2;
  constexpr size_t ACCOUNTS_SIZE = sizeof(tb_account_t) * ACCOUNTS_LEN;

  tb_account_t accounts[ACCOUNTS_LEN];
  memset(&accounts, 0, ACCOUNTS_SIZE);

  accounts[0].id = 1;
  accounts[0].code = 10;
  accounts[0].ledger = 100;

  accounts[1].id = 2;
  accounts[1].code = 10;
  accounts[1].ledger = 100;

  printf("Looking up accounts ...\n");
  tb_uint128_t ids[ACCOUNTS_LEN] = {accounts[0].id, accounts[1].id};

  completion_context_t ctx;
  completion_context_init(&ctx);

  tb_packet_t packet;
  packet.operation = TB_OPERATION_LOOKUP_ACCOUNTS;
  packet.data = ids;
  packet.data_size = sizeof(tb_uint128_t) * ACCOUNTS_LEN;
  packet.user_data = &ctx;
  packet.status = TB_PACKET_OK;

  TB_CLIENT_STATUS client_status = send_request(&client, &packet, &ctx);
  if (client_status != TB_CLIENT_OK) {
    printf("Failed to send the request\n");
    exit(-1);
  }
  if (packet.status != TB_PACKET_OK) {
    // Checking if the request failed:
    printf("Error calling lookup_accounts (ret=%d)\n", packet.status);
    exit(-1);
  }

  if (ctx.size == 0) {
    printf("No accounts found\n");
  }
  completion_context_destroy(&ctx);
  tb_client_deinit(&client);
  return 0;
}
