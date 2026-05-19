#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <semaphore>
#include <thread>

#include "tb_client.h"

// config.message_size_max - @sizeOf(vsr.Header):
#define MAX_MESSAGE_SIZE ((1024 * 1024) - 256)

// Synchronization context between the callback and the main thread.
typedef struct completion_context {
  uint8_t reply[MAX_MESSAGE_SIZE];
  int size;
  bool completed;

  pthread_mutex_t lock;
  pthread_cond_t cv;
} completion_context_t;

struct Request {
  std::binary_semaphore done{0}; // caller blocks on this
  const uint8_t *result = nullptr;
  uint32_t result_size = 0;
  TB_PACKET_STATUS status = TB_PACKET_OK;
};

void on_tb_client_completion(uintptr_t userdata, tb_packet_t *packet,
                             uint64_t timestamp, const uint8_t *result,
                             uint32_t result_size);

TB_CLIENT_STATUS send_request(tb_client_t *client, tb_packet_t *packet,
                              completion_context_t *ctx);

void completion_context_init(completion_context_t *ctx);
void completion_context_destroy(completion_context_t *ctx);
long long get_time_ms(void);
