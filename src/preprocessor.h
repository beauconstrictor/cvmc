#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include "tokeniser.h"
#include "parser.h"
#include "errors.h"

#define ANON_PREFIX "___inline_"
#define SCOPE_PREFIX "___scope_"

bool preprocessor(Arena *arena, Tokens *toks, Symbols *syms,
    Error *error, int stage);

#endif // PREPROCESSOR_H
