/*
 * libuv logger heavily tuned for slow storage like SD cards / flash.
 *
 * Optimizations:
 *   - Much larger chunks (256 KiB) for better sequential write performance
 *   - Larger total arena (~4 MiB) to absorb bursts while a write is pending
 *   - Relaxed backpressure: drop only when arena is completely exhausted
 *   - Keeps writes strictly sequential and large
 */

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#define CHUNK_SIZE (256 * 1024) /* 256 KiB - sweet spot for SD cards */
#define NUM_CHUNKS 16           /* ~4 MiB total arena */
/* Usually 1 is enough; keep small because writes are slow */
#define MAX_PENDING 4

typedef struct Logger Logger;
typedef struct LogChunk LogChunk;

struct LogChunk {
  char *data; /* points into arena */
  size_t used;
  uv_fs_t fs_req;
  Logger *owner;
  LogChunk *next;
};

struct Logger {
  uv_loop_t *loop;
  uv_file fd;
  uv_fs_t open_req;

  char *arena;
  size_t arena_size;

  LogChunk chunks[NUM_CHUNKS];
  LogChunk *free_list;
  LogChunk *current;

  LogChunk *pending_head;
  LogChunk *pending_tail;
  int pending_count;

  char *filename;
};

#define container_of(ptr, type, member)                                        \
  ((type *)((char *)(ptr) - offsetof(type, member)))

static void on_open(uv_fs_t *req);
static void on_write(uv_fs_t *req);
static void start_async_write(Logger *l);
static void queue_for_write(Logger *l, LogChunk *chunk);

Logger *logger_create(uv_loop_t *loop, const char *filename) {
  Logger *l = (Logger *)malloc(sizeof(Logger));
  if (!l) {
    return NULL;
  }
  memset(l, 0, sizeof(*l));

  l->loop = loop;
  l->fd = -1;
  l->filename = strdup(filename);

  l->arena_size = (size_t)CHUNK_SIZE * NUM_CHUNKS;
  l->arena = (char *)malloc(l->arena_size);
  if (!l->arena) {
    free(l->filename);
    free(l);
    return NULL;
  }

  /* Initialize chunks pointing into contiguous arena */
  for (int i = 0; i < NUM_CHUNKS; ++i) {
    l->chunks[i].data = l->arena + (size_t)i * CHUNK_SIZE;
    l->chunks[i].used = 0;
    l->chunks[i].owner = l;
    l->chunks[i].next = l->free_list;
    l->free_list = &l->chunks[i];
  }

  /* Open for append. For extreme perf you could try UV_FS_O_DIRECT (bypass
   * kernel cache) */
  int flags = UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_APPEND;
  /* flags |= UV_FS_O_DIRECT; */ /* Uncomment only if you handle alignment +
                                    errors well */

  int r = uv_fs_open(loop, &l->open_req, filename, flags, 0644, on_open);
  if (r < 0) {
    fprintf(stderr, "uv_fs_open failed: %s\n", uv_strerror(r));
    free(l->arena);
    free(l->filename);
    free(l);
    return NULL;
  }

  return l;
}

static void on_open(uv_fs_t *req) {
  Logger *l = container_of(req, Logger, open_req);
  uv_fs_req_cleanup(req);
  if (req->result < 0) {
    fprintf(stderr, "Failed to open log '%s': %s\n", l->filename,
            uv_strerror((int)req->result));
    l->fd = -1;
    return;
  }
  l->fd = (uv_file)req->result;
}

int logger_log(Logger *l, const char *msg) {
  if (!l || l->fd < 0 || !msg || !*msg) {
    return -1;
  }

  size_t len = strlen(msg);
  size_t to_append = len + (msg[len - 1] != '\n' ? 1 : 0);

  /* Very relaxed backpressure: only drop when we have NO room left at all */
  if (l->pending_count >= MAX_PENDING && l->free_list == NULL &&
      (!l->current || l->current->used + to_append > CHUNK_SIZE)) {
    /* Optional: implement drop-oldest or circular buffer policy here */
    fprintf(stderr, "BACKPRESSURE: log dropped\n");
    return -1;
  }

  if (!l->current) {
    if (l->free_list) {
      l->current = l->free_list;
      l->free_list = l->free_list->next;
      l->current->used = 0;
    } else {
      return -1;
    }
  }

  if (l->current->used + to_append > CHUNK_SIZE) {
    queue_for_write(l, l->current);
    l->current = NULL;
    return logger_log(l, msg); /* recurse at most once */
  }

  memcpy(l->current->data + l->current->used, msg, len);
  l->current->used += len;
  if (msg[len - 1] != '\n') {
    l->current->data[l->current->used++] = '\n';
  }

  return 0;
}

static void queue_for_write(Logger *l, LogChunk *chunk) {
  assert(chunk->used > 0);
  chunk->next = NULL;
  if (l->pending_tail) {
    l->pending_tail->next = chunk;
  } else {
    l->pending_head = chunk;
  }
  l->pending_tail = chunk;
  l->pending_count++;

  if (l->pending_count == 1) {
    start_async_write(l);
  }
}

static void start_async_write(Logger *l) {
  LogChunk *chunk = l->pending_head;
  uv_buf_t iov = uv_buf_init(chunk->data, (unsigned int)chunk->used);

  int r = uv_fs_write(l->loop, &chunk->fs_req, l->fd, &iov, 1, -1, on_write);
  if (r < 0) {
    fprintf(stderr, "uv_fs_write failed: %s\n", uv_strerror(r));
    on_write(&chunk->fs_req);
  }
}

static void on_write(uv_fs_t *req) {
  LogChunk *chunk = container_of(req, LogChunk, fs_req);
  Logger *l = chunk->owner;

  uv_fs_req_cleanup(req);

  if (req->result < 0) {
    fprintf(stderr, "write error: %s (bytes attempted: %zu)\n",
            uv_strerror((int)req->result), chunk->used);
  }

  /* Dequeue */
  l->pending_head = chunk->next;
  if (!l->pending_head) {
    l->pending_tail = NULL;
  }
  l->pending_count--;

  /* Recycle chunk */
  chunk->used = 0;
  chunk->next = l->free_list;
  l->free_list = chunk;

  if (l->pending_count > 0) {
    start_async_write(l);
  }
}

void logger_flush(Logger *l) {
  if (!l || !l->current || l->current->used == 0) {
    return;
  }
  queue_for_write(l, l->current);
  l->current = NULL;
}

void logger_destroy(Logger *l) {
  if (!l) {
    return;
  }
  logger_flush(l);

  /* Drain remaining writes (important on slow media) */
  while (l->pending_count > 0) {
    uv_run(l->loop, UV_RUN_ONCE);
  }

  if (l->arena) {
    free(l->arena);
  }
  if (l->filename) {
    free(l->filename);
  }
  free(l);
}

/* ------------------------------------------------------------------ */
/* Demo */
int main(void) {
  uv_loop_t *loop = uv_default_loop();

  Logger *log = logger_create(loop, "test.log");
  if (!log) {
    return 1;
  }

  uv_run(loop, UV_RUN_ONCE); /* finish open */

  printf("Starting high-rate logging to optimized buffer...\n");

  for (int i = 0; i < 10000; ++i) {
    char msg[200];
    snprintf(msg, sizeof(msg),
             "Optimized log #%d at %d - large buffers help with slow flash "
             "storage",
             i, (int)time(NULL));
    if (logger_log(log, msg) < 0) {
      printf("Backpressure hit at %d\n", i);
      uv_run(loop, UV_RUN_ONCE); /* give disk time */
    }
  }

  logger_flush(log);
  printf("Flushed. Draining writes (this may take time on slow storage "
         "device)...\n");

  uv_run(loop, UV_RUN_DEFAULT);

  logger_destroy(log);
  uv_loop_close(loop);

  printf("Done. Check test.log\n");
  return 0;
}
