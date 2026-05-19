#include <iostream>
#include <format>
#include <thread>
#include <cstdio>
#include <cstring>

#include "tb_client.h"

static void on_completion(
    uintptr_t userdata,
    tb_packet_t* packet,
    uint64_t timestamp,
    const uint8_t* result,
    uint32_t result_size
) {
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

int main() {
    tb_client_t client{};

    const uint8_t cluster_id[16] = {};  // cluster 0
    const char*   address         = "127.0.0.1:3000";
    uint32_t      address_len     = static_cast<uint32_t>(std::strlen(address));

    TB_INIT_STATUS status = tb_client_init(
        &client,
        cluster_id,
        address,
        address_len,
        /*completion_ctx=*/ 0,
        on_completion
    );

    if (status != TB_INIT_SUCCESS) {
        std::printf("tb_client_init failed: %d\n", static_cast<int>(status));
        return 1;
    }

    std::printf("connected to TigerBeetle at %s\n", address);

    tb_client_deinit(&client);
    return 0;
}

