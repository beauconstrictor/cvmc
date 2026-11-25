#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "parser.h"
#include "arena.h"

#define DEFAULT_CALL_STACK_DEPTH 1024

bool simulate(Arena *arena, const char *s, Symbols *syms, size_t depth);

#endif // SIMULATOR_H
