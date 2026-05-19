#include "client.hpp"

int main() {
  tb_client_t client{};

  const uint8_t cluster_id[16] = {}; // cluster 0
  const char *address = "127.0.0.1:3000";
  uint32_t address_len = static_cast<uint32_t>(std::strlen(address));

  TB_INIT_STATUS status =
      tb_client_init(&client, cluster_id, address, address_len,
                     /*completion_ctx=*/0, on_tb_client_completion);

  if (status != TB_INIT_SUCCESS) {
    std::printf("tb_client_init failed: %d\n", static_cast<int>(status));
    return 1;
  }

  std::printf("connected on %s\n", address);

  tb_client_deinit(&client);
  return 0;
}
