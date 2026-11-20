#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "parser.h"

#define DEFAULT_CALL_STACK_DEPTH 1024

const char *simulate(const char *s, Symbols *syms, size_t depth);

#endif // SIMULATOR_H
