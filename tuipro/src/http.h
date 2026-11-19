#ifndef TUIPRO_HTTP_H
#define TUIPRO_HTTP_H

#include <stdbool.h>

struct lua_State;

/* Register the native fetch backend as the preloaded module tuipro.http_native. */
int luaopen_tuipro_http(struct lua_State *L);

/* True while at least one request still owns a libuv handle or Lua callback. */
bool tp_http_active(void);

/* Drive the libuv loop together with the terminal input fd.
 *
 * Returns a bitmask: 1 when stdin is readable, 2 when the signal pipe is
 * readable, 0 on timeout or when a network event was processed.  timeout_ms
 * < 0 waits indefinitely; 0 polls without blocking. */
int tp_http_wait(int stdin_fd, int signal_fd, int timeout_ms);

/* Drive the libuv loop for scripts (no terminal fds).
 * Returns 1 when all requests finished, 0 on timeout/activity, -1 when called
 * re-entrantly from inside a libuv callback. */
int tp_http_run(int timeout_ms);

/* Abort every live request, releasing Lua registry references before the Lua
 * state is closed.  Safe to call more than once. */
void tp_http_shutdown(struct lua_State *L);

/* Release the loop, TLS context and any remaining native resources.  Call
 * after lua_close(). */
void tp_http_free(void);

#endif /* TUIPRO_HTTP_H */
