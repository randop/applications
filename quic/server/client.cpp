/*
 * QUIC Echo client with liburing (io_uring)
 *
 * Run:
 *   ./client.bin 0.0.0.0 4433
 */
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <lsquic.h>

static lsquic_stream_ctx_t *on_stream(void *, lsquic_stream_t *s) {
  std::cout << "[client] STREAM CREATED" << std::endl;

  const char *msg = "hello world";
  lsquic_stream_write(s, msg, strlen(msg));
  lsquic_stream_shutdown(s, 1);

  lsquic_stream_wantread(s, 1);

  return nullptr;
}

static void on_read(lsquic_stream_t *, lsquic_stream_ctx_t *) {
  std::cout << "[client] STREAM READ EVENT" << std::endl;
}

static const lsquic_stream_if stream_if = {.on_new_stream = on_stream,
                                           .on_read = on_read};

static int packets_out(void *, const lsquic_out_spec *, unsigned n) {
  std::cout << "[client] packets_out " << n << std::endl;
  return n;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    return 1;
  }

  lsquic_global_init(LSQUIC_GLOBAL_CLIENT);

  lsquic_engine_settings settings;
  lsquic_engine_init_settings(&settings, LSENG_SERVER);

  lsquic_engine_api api{};
  api.ea_stream_if = &stream_if;
  api.ea_packets_out = packets_out;
  api.ea_settings = &settings;

  auto *engine = lsquic_engine_new(LSENG_SERVER, &api);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(atoi(argv[2]));
  inet_pton(AF_INET, argv[1], &addr.sin_addr);

  lsquic_engine_connect(engine, LSQVER_I001, nullptr, (sockaddr *)&addr,
                        nullptr, nullptr, nullptr, 0, nullptr, 0, nullptr, 0);

  std::cout << "[client] RUNNING" << std::endl;

  while (true) {
    lsquic_engine_process_conns(engine);
  }
}
