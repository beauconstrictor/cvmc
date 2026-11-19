#ifndef ARENA_H
#define ARENA_H

struct ArenaAllocation {
  struct ArenaAllocation *next;
  struct ArenaAllocation *prev;
  void *buf;
};

typedef struct ArenaAllocation ArenaAllocation;

typedef struct {
  ArenaAllocation *latest;
} Arena;

void *arena_alloc(Arena *arena, size_t size);
void arena_free(Arena *arena);

#endif // ARENA_H
