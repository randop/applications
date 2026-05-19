#include "client.hpp"

void on_tb_client_completion(uintptr_t userdata, tb_packet_t *packet,
                             uint64_t timestamp, const uint8_t *result,
                             uint32_t result_size) {
  (void)userdata;
  (void)timestamp;
  (void)result;
  (void)result_size;

  if (packet->status != TB_PACKET_OK) {
    std::printf("packet error: %d\n", static_cast<int>(packet->status));
  } else {
    std::printf("operation complete, result bytes: %u\n", result_size);
  }
}
