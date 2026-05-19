#pragma once

#include "tb_client.h"
#include <cstdio>
#include <cstring>
#include <thread>

void on_tb_client_completion(uintptr_t userdata, tb_packet_t *packet,
                             uint64_t timestamp, const uint8_t *result,
                             uint32_t result_size);
