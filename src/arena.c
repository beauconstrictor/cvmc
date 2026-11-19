#include "stdlib.h"
#include "errno.h"

#include "arena.h"

void *arena_alloc(Arena *arena, size_t size) {
  void *buf = malloc(size);
  ArenaAllocation *alloc = malloc(sizeof(ArenaAllocation));
  if (buf == NULL || alloc == NULL) {
    free(buf);
    errno = ENOMEM;
    return NULL;
  }

  alloc->prev = arena->latest;
  alloc->next = NULL;
  alloc->buf = buf;

  if (arena->latest != NULL) arena->latest->next = alloc;
  arena->latest = alloc;

  return buf;
}

void arena_free(Arena *arena) {
  ArenaAllocation *alloc = arena->latest;
  while (alloc != NULL) {
    free(alloc->buf);
    ArenaAllocation *prev = alloc->prev;
    free(alloc);
    alloc = prev;
  }
  arena->latest = NULL;
}
