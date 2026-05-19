#include "client.hpp"

void on_tb_client_completion(uintptr_t context, tb_packet_t *packet,
                             uint64_t timestamp, const uint8_t *data,
                             uint32_t size) {
  (void)timestamp;
  (void)context;

  if (packet->status != TB_PACKET_OK) {
    std::printf("packet error: %d\n", static_cast<int>(packet->status));
  }
  // The user_data gives context to a request:
  completion_context_t *ctx = (completion_context_t *)packet->user_data;

  // Signaling the main thread we received the reply:
  pthread_mutex_lock(&ctx->lock);

  memcpy(ctx->reply, data, size);
  ctx->size = size;
  ctx->completed = true;

  pthread_cond_signal(&ctx->cv);
  pthread_mutex_unlock(&ctx->lock);
}

TB_CLIENT_STATUS send_request(tb_client_t *client, tb_packet_t *packet,
                              completion_context_t *ctx) {
  // Locks the mutex:
  if (pthread_mutex_lock(&ctx->lock) != 0) {
    printf("Failed to lock mutex\n");
    exit(-1);
  }

  // Submits the request asynchronously:
  ctx->completed = false;
  TB_CLIENT_STATUS client_status = tb_client_submit(client, packet);
  if (client_status == TB_CLIENT_OK) {
    // Uses a condvar to sync this thread with the callback:
    while (!ctx->completed) {
      if (pthread_cond_wait(&ctx->cv, &ctx->lock) != 0) {
        printf("Failed to wait condvar\n");
        exit(-1);
      }
    }
  }

  if (pthread_mutex_unlock(&ctx->lock) != 0) {
    printf("Failed to unlock mutex\n");
    exit(-1);
  }

  return client_status;
}

void completion_context_init(completion_context_t *ctx) {
  if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
    printf("Failed to initialize mutex\n");
    exit(-1);
  }

  if (pthread_cond_init(&ctx->cv, NULL) != 0) {
    printf("Failed to initialize condition var\n");
    exit(-1);
  }
}

void completion_context_destroy(completion_context_t *ctx) {
  pthread_cond_destroy(&ctx->cv);
  pthread_mutex_destroy(&ctx->lock);
}

long long get_time_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    printf("Failed to call clock_gettime\n");
    exit(-1);
  }
  return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}
