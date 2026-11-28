/*
 * preprocessing steps:
 *
 * 1. deimportation: insert the contents of imported files (namespacing in the future?)
 * 2. deenumeration: replace all enum statements with consts
 * 3. deparenthesisation: convert prefix expressions to postfix
 * 4. destringification: convert string literals to blocks containing immediate bytes
 * 5. delocalisation: give all variables global names from their positions
 * 6. desymbolisation: remove constants and variables (resolve to their values)
 * 7. deanonymification: extract nested anonymous blocks into the global scope
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>

#include "tokeniser.h"
#include "assembler.h"
#include "parser.h"
#include "errors.h"
#include "mmap.h"
#include "data.h"

#include "preprocessor.h"

bool deparenthesise(Tokens *toks, Error *error) {
  Tokens prefix_words = create_tokens();

  for (size_t i = 0; i < toks->count; i++) {
    Token tok = toks->items[i];

    if (tok.kind == TOKEN_LPARA) {
      if (i + 1 >= toks->count)
        GIVE_ERROR("expected word after '('", tok);

      if (!append_token(toks->items[i+1], &prefix_words)) return false;
      if (!remove_token(toks, i)) return false;
      if (!remove_token(toks, i)) return false;
      i -= 2;
    } else if (tok.kind == TOKEN_RPARA) {
      if (!remove_token(toks, i--)) return false;
      if (prefix_words.count == 0)
        GIVE_ERROR("unexpected ')'", tok);

      Token word = prefix_words.items[prefix_words.count - 1];
      if (word.kind != TOKEN_WORD)
        GIVE_ERROR("expected word after '('", word);

      if (!remove_token(&prefix_words, prefix_words.count - 1))
        return false;
      if (!insert_token(toks, i+1, word)) return false;

      i++;
    }
  }

  if (prefix_words.count > 0) {
    Token location = prefix_words.items[prefix_words.count - 1];
    GIVE_ERROR("unterminated parenthesis", location);
  }

  return true;
}

bool destringify(Arena *arena, Tokens *toks, Error *error) {
  (void) error; // doesn't actually produce any errors yet, but will

  for (size_t i = 0; i < toks->count; i++) {
    Token tok = toks->items[i];
    if (tok.kind != TOKEN_STRING) continue;
  
    if (!insert_token(toks, i++, (Token) {
        .kind = TOKEN_LBRACE,
        .start = "{",
        .length = 1,
        .location = tok.location,
    })) return false;

    if (!remove_token(toks, i--)) return false;

    const char *s = tok.start;
    bool in_escape = false;

    while (s < tok.start + tok.length) {
      char *num = arena_alloc(arena, 4);
      if (num == NULL) return false;

      char c = *s;

      if (c == '\\' && !in_escape) {
        in_escape = true;
        s++;
        continue;
      }

      if (in_escape) {
        c = escapes[(unsigned char)c];
        if (c == 0) {
          static char err_msg[256];
          snprintf(err_msg, sizeof(err_msg), "invalid escape sequence: '\\%c'", *s);
          *error = (Error) {
            .location = {
              .line = tok.location.line,
              .col = tok.location.col + s - tok.start,
              .path = tok.location.path,
            },
            .message = err_msg,
            .set = true,
          };
          return false;
        }
        if (c == -1) c = 0;
        in_escape = false;
      }

      int len = snprintf(num, 4, "%d", c);

      if (!insert_token(toks, ++i, (Token) {
          .kind = TOKEN_BYTE,
          .start = num,
          .length = len,
          .location = tok.location,
      })) return false;
      s++;
    }

    assert(OPCODE_RET == 0x00); // as long as the implicit return at
                                // the end of all blocks is opcode 0,
                                // then it serves as an automatic
                                // null-terminator, so we don't need
                                // to add one

    if (!insert_token(toks, ++i, (Token) {
        .kind = TOKEN_RBRACE,
        .start = "}",
        .length = 1,
        .location = tok.location,
    })) return false;
  }

  return true;
}

bool delocalise(Arena *arena, Tokens *toks, Error *error, size_t start,
    const char *initial_prefix, bool is_nested) {
  (void)error;

  static char buf[256];

  char prefix[1024] = "";

  if (toks->count > start && toks->items[start].kind == TOKEN_DEFINITION) {
    Token tok = toks->items[start];
    snprintf(prefix, sizeof(prefix), "%s%s%.*s", initial_prefix,
        strlen(initial_prefix) ? "." : "",
        (int)tok.length, tok.start);
  } else if (is_nested) {
    snprintf(prefix, sizeof(prefix), "%s." SCOPE_PREFIX "%zu", initial_prefix, start);
  }

  Symbols syms = {0};
  Symbols shadowed = {0};

  int nesting = 0;

  for (size_t i = start; i < toks->count; i++) {
    Token *tok = toks->items + i;

    bool has_next = toks->count > i+1;
    Token *next;
    if (has_next) next = toks->items + i+1;

    bool has_prev = i > 0;
    Token *prev;
    if (has_prev) prev = toks->items + i-1;

    if (tok->kind == TOKEN_LBRACE)
      nesting++;
    if (tok->kind == TOKEN_RBRACE)
      nesting--;
    if (nesting < 0) break;
    if (nesting == 0) shadowed = (Symbols){0};

    if (tok->kind == TOKEN_VAR && nesting == 0
        && has_next && next->kind == TOKEN_WORD) {
      bool redefined = false;
      append_token_symbol(&syms, next, 0, &redefined);
      if (redefined)
        GIVE_ERROR_WITH_LEXEME("'%.*s' is already defined", *next);

      if (is_nested) {   
        size_t len = snprintf(NULL, 0, "%s.%.*s", prefix, (int)next->length, next->start);
        char *prefixed = arena_alloc(arena, len + 1);
        if (prefixed == NULL) return false;
        snprintf(prefixed, len+1, "%s.%.*s", prefix, (int)next->length, next->start);
        next->length = len;
        next->start  = prefixed;
      }

      i++;
    } else if (tok->kind == TOKEN_WORD
        && lookup_symbol(tok->start, tok->length, &syms, NULL)
        && strlen(prefix) > 0) {

      if (has_prev && prev->kind == TOKEN_VAR) {
        bool redefined = false;
        append_token_symbol(&shadowed, tok, 0, &redefined);
        if (redefined)
          GIVE_ERROR_WITH_LEXEME("'%.*s' is already defined", *tok);
        continue;
      } else if (lookup_symbol(tok->start, tok->length, &shadowed, NULL)) {
        continue;
      }

      size_t len = snprintf(NULL, 0, "%s.%.*s", prefix, (int)tok->length, tok->start);
      char *prefixed = arena_alloc(arena, len + 1);
      if (prefixed == NULL) return false;
      snprintf(prefixed, len+1, "%s.%.*s", prefix, (int)tok->length, tok->start);
      tok->length = len;
      tok->start  = prefixed;
    }
  }

  nesting = 0;

  for (size_t i = start; i < toks->count; i++) {
    Token tok = toks->items[i];

    if (tok.kind == TOKEN_LBRACE)
      nesting++;
    if (tok.kind == TOKEN_RBRACE)
      nesting--;
    if (nesting < 0) break;

    if (tok.kind != TOKEN_LBRACE) continue;
    if (nesting == 1) delocalise(arena, toks, error, i+1, prefix, true);
  }

  return true;
}

bool substitute(Arena *arena, Tokens *toks, Symbols *syms) {
  for (size_t i = 0; i < toks->count; i++) {
    Token tok = toks->items[i];
    if (tok.kind != TOKEN_WORD) continue;

    long value;
    if (!lookup_symbol(tok.start, tok.length, syms, &value)) continue;

    int len = snprintf(NULL, 0, "%ld", value);
    char *s = arena_alloc(arena, len + 1);
    if (s == NULL) return false;
    snprintf(s, len + 1, "%ld", value);

    if (!remove_token(toks, i)) return false;
    if (!insert_token(toks, i, (Token) {
        .kind = TOKEN_NUMBER,
        .start = s,
        .length = len,
        .location = tok.location,
    })) return false;
  }
  
  return true;
}

bool deenumerate(Arena *arena, Tokens *toks, Symbols *syms, Error *error) {
  static char buf[256];

  for (size_t i = 0; i < toks->count; i++) {
    Token tok = toks->items[i];

    if (tok.kind != TOKEN_ENUM) continue;

    if (toks->count < i + 4
        || toks->items[i+1].kind != TOKEN_WORD
        || toks->items[i+2].kind != TOKEN_COLON
        || toks->items[i+3].kind != TOKEN_LPARA)
      GIVE_ERROR_WITH_LEXEME("expected enum definition after '%.*s'", tok);

    Token name = toks->items[i+1];

    size_t type_len = name.length + 2;
    char *type_name = arena_alloc(arena, type_len + 1);
    snprintf(type_name, type_len + 1, "%.*s-t", (int)name.length,
        name.start);

    size_t counter_len = type_len + 1;
    char *counter_name = arena_alloc(arena, counter_len + 1);
    snprintf(counter_name, counter_len + 1, "$%s", type_name);

    if (!append_symbol((Symbol) {
      .name = type_name,
      .length = type_len,
      .value = 2, // all enum-ts are just integer values
    }, syms, NULL)) return false;

    long enum_count;
    if (!lookup_symbol(counter_name, counter_len, syms,
        &enum_count))
      enum_count = 0;

    size_t x = i + 4;
    while (true) {
      if (x == toks->count)
        GIVE_ERROR("unterminated enum definition", tok);

      Token ele = toks->items[x];
      if (!remove_token(toks, x)) return false;

      if (ele.kind == TOKEN_RPARA) break;

      if (ele.kind != TOKEN_WORD)
          GIVE_ERROR_WITH_LEXEME("expected enum value, got '%.*s'", ele);

      size_t value_len = snprintf(NULL, 0, "%.*s.%.*s",
          (int)name.length, name.start, (int)ele.length,
          ele.start);
      char *value_name = arena_alloc(arena, value_len+1);
      snprintf(value_name, value_len+1, "%.*s.%.*s",
          (int)name.length, name.start, (int)ele.length,
          ele.start);

      bool redefined = false;
      if (!append_symbol((Symbol) {
          .name = value_name,
          .length = value_len,
          .value = enum_count++,
      }, syms, &redefined)) return false;
      if (redefined)
        GIVE_ERROR_WITH_LEXEME("duplicate enum value: '%.*s'", ele);
    }

    for (size_t y = 0; y < 4; y++)
      if (!remove_token(toks, i)) return false;

    if (!append_symbol((Symbol) {
        .name = counter_name,
        .length = counter_len,
        .value = enum_count,
    }, syms, NULL)) return false;
  }

  return substitute(arena, toks, syms);
}

bool verify_symbol_name(Token *tok, Error *error) {
  if (memchr(tok->start, '#', tok->length))
    GIVE_ERROR("'#' is not allowed in symbol names", *tok);
  if (memchr(tok->start, '$', tok->length))
    GIVE_ERROR("'$' is not allowed in symbol names", *tok);

  return true;
}

bool desymbolise(Arena *arena, Tokens *toks, Symbols *syms,
    Error *error) {
  static char buf[256]; // for lexeme error message

  size_t allocator = STATIC_ALLOC_START;

  size_t i = 0;
  while (i < toks->count) {
    Token tok = toks->items[i];

    if (tok.kind != TOKEN_CONST) {
      i++;
      continue;
    }

    if (toks->count < i + 4
        || toks->items[i+1].kind != TOKEN_WORD
        || toks->items[i+2].kind != TOKEN_ASSIGN
        || toks->items[i+3].kind != TOKEN_NUMBER)
      GIVE_ERROR("expected assignment after " CONST_WORD, tok);

    Token lval  = toks->items[i+1];
    Token rval = toks->items[i+3];

    long value = read_number(&rval, error);
    if (error->set) return false;

    if (!verify_symbol_name(&lval, error)) return false;
    bool redefined = false;
    if (!append_token_symbol(syms, &lval, value, &redefined)) return false;
    if (redefined)
      GIVE_ERROR_WITH_LEXEME("'%.*s' is already defined", lval);

    // we have to make sure to delete them _after_ any errors.
    // otherwise their locations won't exist anymore to print them.
    for (int x = 0; x < 4; x++)
      if (!remove_token(toks, i)) return false;
  }

  if (!substitute(arena, toks, syms)) return false;

  i = 0;
  while (i < toks->count) {
    Token tok = toks->items[i];

    if (tok.kind != TOKEN_VAR) {
      i++;
      continue;
    }

    bool is_array = false;

    if (toks->count >= i + 7
     && toks->items[i+1].kind == TOKEN_WORD
     && toks->items[i+2].kind == TOKEN_LSQUARE
     && toks->items[i+3].kind == TOKEN_NUMBER
     && toks->items[i+4].kind == TOKEN_RSQUARE
     && toks->items[i+5].kind == TOKEN_COLON
     && toks->items[i+6].kind == TOKEN_NUMBER)
      is_array = true;
    
    if (!is_array) {
      if (toks->count < i + 4)
        GIVE_ERROR("expected declaration after " VAR_WORD, tok);
      if (toks->items[i+1].kind != TOKEN_WORD)
        GIVE_ERROR("expected declaration after " VAR_WORD, toks->items[i+1]);
      if (toks->items[i+2].kind != TOKEN_COLON)
        GIVE_ERROR("expected declaration after " VAR_WORD, toks->items[i+2]);
      if (toks->items[i+3].kind != TOKEN_NUMBER)
        GIVE_ERROR("expected declaration after " VAR_WORD, toks->items[i+3]);
    }

    Token lval = toks->items[i+1];
    Token rval = is_array ? toks->items[i+6] : toks->items[i+3];

    long size = read_number(&rval, error);
    if (error->set) return false;
    size_t length = 1;
    if (is_array) {
      length = read_number(&toks->items[i+3], error);
      if (error->set) return false;
    }
    size *= length;

    if (allocator + size > STATIC_ALLOC_MAX)
      GIVE_ERROR("static variable allocation space is exhausted", tok);
  
    if (!verify_symbol_name(&lval, error)) return false;
    bool redefined = false;
    if (!append_token_symbol(syms, &lval, allocator, &redefined)) return false;

    if (redefined)
      GIVE_ERROR_WITH_LEXEME("'%.*s' is already defined", tok);

    char *sizeof_name = arena_alloc(arena, lval.length + 2);
    if (sizeof_name == NULL) return false;
    snprintf(sizeof_name, lval.length + 2, "#%.*s", (int)lval.length, lval.start);

    if (!append_symbol((Symbol) {
      .name = sizeof_name,
      .length = lval.length + 1,
      .value = size
    }, syms, NULL)) return false;

    char *lenof_name = arena_alloc(arena, lval.length + 2);
    if (lenof_name == NULL) return false;
    snprintf(lenof_name, lval.length + 2, "$%.*s", (int)lval.length, lval.start);
    if (!append_symbol((Symbol) {
      .name = lenof_name,
      .length = lval.length + 1,
      .value = length
    }, syms, NULL)) return false;

    allocator += size;

    for (int x = 0; x < 4; x++)
      if (!remove_token(toks, i)) return false;
    if (is_array)
      for (int x = 0; x < 3; x++)
        if (!remove_token(toks, i)) return false;
  }

  for (size_t i = 0; i < toks->count; i++) {
    Token *tok = toks->items + i;

    bool has_next = toks->count > i+1;
    Token *next;
    if (has_next) next = toks->items + i+1;

    if (tok->kind != TOKEN_WORD) continue;
    if (!has_next ||
        (strncmp(next->start, "size", next->length) != 0
         && strncmp(next->start, "len", next->length) != 0)) continue;
    
    if (!lookup_symbol(tok->start, tok->length, syms, NULL))
      GIVE_ERROR_WITH_LEXEME("undefined symbol: '%.*s'", *tok);

    char prefix = next->start[0] == 's' ? '#' : '$';
    remove_token(toks, i+1);

    size_t len = snprintf(NULL, 0, "%c%.*s", prefix, (int)tok->length, tok->start);
    char *name = arena_alloc(arena, len + 1);
    if (name == NULL) return false;
    snprintf(name, len + 1, "%c%.*s", prefix, (int)tok->length, tok->start);
    tok->start = name;
    tok->length = len;
  }

  return substitute(arena, toks, syms);
}

struct Blocks {
  Tokens *items;
  size_t count;
  size_t capacity;
};

bool append_block(Tokens block, struct Blocks *blocks) {
  if (blocks->count < blocks->capacity) {
    blocks->items[blocks->count++] = block;
    return true;
  }

  size_t new_capa = blocks->capacity;
  new_capa = new_capa > 0 ? new_capa*2 : 1;
  Tokens *new = realloc(blocks->items, sizeof(Tokens) * new_capa);
  if (new == NULL) return false;

  blocks->items = new;
  blocks->capacity = new_capa;
  blocks->items[blocks->count++] = block;

  return true;
}

static bool deanonymify_inner(Arena *arena, Tokens *toks,
    struct Blocks *blocks, size_t *anon_count, Error *error) {
  size_t i = 0;

  while (i < toks->count) {
    if (toks->items[i].kind != TOKEN_LBRACE) {
      i++;
      continue;
    }

    Token opening = toks->items[i];

    size_t depth = 1;
    size_t end = i + 1;

    while (end < toks->count && depth > 0) {
      if (toks->items[end].kind == TOKEN_LBRACE)
        depth++;
      else if (toks->items[end].kind == TOKEN_RBRACE)
        depth--;

      end++;
    }

    if (depth != 0)
      GIVE_ERROR("unterminated block", opening);

    Tokens block = create_tokens();

    for (size_t x = i + 1; x < end - 1; x++) {
      if (!append_token(toks->items[x], &block)) return false;
    }

    if (!deanonymify_inner(arena, &block, blocks, anon_count, error))
      return false;

    bool named =
      block.count > 0 &&
      block.items[0].kind == TOKEN_DEFINITION;

    if (named) {
      for (size_t x = i + 1; x < end - 1; x++)
        if (!remove_token(toks, i + 1)) return false;

      for (size_t x = 0; x < block.count; x++)
        if (!insert_token(toks, i + 1 + x, block.items[x])) return false;

      i += block.count + 2;
      continue;
    }

    int len = snprintf(
      NULL, 0, ANON_PREFIX "%zu", (*anon_count)
    );

    char *name = arena_alloc(arena, len + 1);
    if (name == NULL) return false;

    snprintf(name, len + 1, ANON_PREFIX "%zu", (*anon_count)++);
    if (!append_block(block, blocks)) return false;

    for (size_t x = i; x < end; x++)
      if (!remove_token(toks, i)) return false;

    if (!insert_token(toks, i++, (Token) {
      .kind = TOKEN_ADDR,
      .start = "&",
      .length = 1,
      .location = opening.location,
    })) return false;

    if (!insert_token(toks, i, (Token) {
      .kind = TOKEN_WORD,
      .start = name,
      .length = len,
      .location = opening.location,
    })) return false;

    i++;
  }

  return true;
}

bool deanonymify(Arena *arena, Tokens *toks, Error *error) {
  struct Blocks blocks = {0};
  size_t anon_count = 0;

  if (!deanonymify_inner(arena, toks, &blocks, &anon_count, error))
    return false;

  // add back the anonymous blocks the the end of the main
  // token stream
  for (size_t i = 0; i < blocks.count; i++) {
    int len = snprintf(NULL, 0, ANON_PREFIX "%zu", i);

    char *name = arena_alloc(arena, len + 1);
    if (name == NULL) return false;

    snprintf(name, len + 1, ANON_PREFIX "%zu", i);

    if (!append_token((Token) {
      .kind = TOKEN_LBRACE,
      .start = "{",
      .length = 1,
    }, toks)) return false;

    if (!append_token((Token) {
      .kind = TOKEN_DEFINITION,
      .start = name,
      .length = len,
    }, toks)) return false;

    for (size_t j = 0; j < blocks.items[i].count; j++)
      if (!append_token(blocks.items[i].items[j], toks)) return false;

    if (!append_token((Token) {
      .kind = TOKEN_RBRACE,
      .start = "}",
      .length = 1,
    }, toks)) return false;
  }

  return true;
}

char *read_module(Arena *arena, const char *path) {
  bool is_std = false;
  char *buf;
  size_t len;

  #define CONCAT3(a, b, c) a##b##c
  #define STD_MODULE(name) \
      if (!is_std && strcmp(path, #name ".cvm") == 0) { \
        buf = (char *)CONCAT3(src_std_, name, _cvm); \
        len = CONCAT3(src_std_, name, _cvm_len); \
        is_std = true; \
      }

  STD_MODULE(std)
  STD_MODULE(bit)
  STD_MODULE(str)
  STD_MODULE(is)
  STD_MODULE(io)

  if (is_std) {
    char *source = arena_alloc(arena, len + 1);
    memcpy(source, buf, len);
    source[len] = '\0';
    return source;
  }

  return read_file(arena, path);
}

bool deimport(Arena *arena, Tokens *toks, Error *error) {
  static char buf[256];

  Symbols imported = {0};

  size_t i = 0;
  while (i < toks->count) {
    Token tok = toks->items[i];
    Token next;
    bool has_next = i < toks->count - 1;
    if (has_next) next = toks->items[i+1];


    if (tok.kind == TOKEN_REQUIRE) {
      if (!has_next || next.kind != TOKEN_WORD)
        GIVE_ERROR_WITH_LEXEME("expected module name after '%.*s'", tok);

      if (!lookup_symbol(next.start, next.length, &imported, NULL))
        GIVE_ERROR_WITH_LEXEME("required module was never imported: '%.*s'", next);

      if (!remove_token(toks, i)) return false;
      if (!remove_token(toks, i)) return false;
      continue;
    }

    if (tok.kind != TOKEN_IMPORT) {
      i++;
      continue;
    }

    if (!has_next)
      GIVE_ERROR("expected name after import", tok);
    else if (next.kind != TOKEN_WORD)
      GIVE_ERROR("expected name after import", next);

    bool already_imported = false;
    append_token_symbol(&imported, &next, 0, &already_imported);
    if (already_imported) continue;

    char *path = arena_alloc(arena, PATH_MAX);
    if (path == NULL) return false;
    Tokens new_toks = {0};
    size_t path_len = snprintf(path, PATH_MAX, "%.*s.cvm",
        (int)next.length, next.start);

    if (!remove_token(toks, i)) return false;
    if (!remove_token(toks, i)) return false;

    bool reimported = false;
    append_symbol((Symbol) {
        .name = path,
        .length = path_len,
        .value = 0
    }, &imported, &reimported);

    if (reimported) continue;

    char *source = read_module(arena, path);
    if (source == NULL) GIVE_ERROR_WITH_LEXEME("no such file: '%.*s'", next);

    if (!lexer(source, path, &new_toks, error)) return false;

    size_t insert = i;

    for (size_t x = 0; x < new_toks.count; x++) {
      Token subtok = new_toks.items[x];
      if (!insert_token(toks, insert++, subtok)) return false;
    }
  }

  return true;
}

void deunreference_inner(Symbols *referenced, Tokens *toks,
    const char *func, size_t len) {
  if (lookup_symbol(func, len, referenced, NULL)) return;

  Token definition;
  bool found = false;
  size_t idx = 0;
  for (size_t i = 0; i < toks->count; i++) {
    definition = toks->items[i];
    if (definition.kind != TOKEN_DEFINITION) continue;
    if (definition.start[definition.length-1] == '*') definition.length--;
    if (definition.length != len
        || memcmp(definition.start, func, len) != 0)
      continue;
    found = true;
    idx = i;
    break;
  }
  if (!found) return;

  bool redefined;
  append_token_symbol(referenced, &definition, 0, &redefined);

  for (size_t i = idx; i < toks->count; i++) {
    Token tok = toks->items[i];
    if (tok.kind != TOKEN_WORD) continue;
    if (tok.kind == TOKEN_LBRACE) break;
    deunreference_inner(referenced, toks, tok.start, tok.length);
  }
}

bool preprocessor(Arena *arena, Tokens *toks, Symbols *syms,
    Error *error, int stage, bool verbose) {
  VERBOSE("preprocessor", "desugaring imports...");
  if (!deimport(arena, toks, error))          return false;
  if (stage == 1)                             return true;
  VERBOSE("preprocessor", "desugaring enums...");
  if (!deenumerate(arena, toks, syms, error)) return false;
  if (stage == 2)                             return true;
  VERBOSE("preprocessor", "desugaring parenthetical expressions...");
  if (!deparenthesise(toks, error))           return false;
  if (stage == 3)                             return true;
  VERBOSE("preprocessor", "desugaring string literals...");
  if (!destringify(arena, toks, error))       return false;
  if (stage == 4)                             return true;
  VERBOSE("preprocessor", "desugaring scopes...");
  if (!delocalise(arena, toks, error, 0, "",
        false))                               return false;
  if (stage == 5)                             return true;
  VERBOSE("preprocessor", "desugaring variables and constants...");
  if (!desymbolise(arena, toks, syms, error)) return false;
  if (stage == 6)                             return true;
  VERBOSE("preprocessor", "desugaring anonymous blocks...");
  if (!deanonymify(arena, toks, error))       return false;
  if (stage == 7)                             return true;

  return true;
}
