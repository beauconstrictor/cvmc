#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "tokeniser.h"
#include "parser.h"
#include "arena.h"

const int escapes[256] = {
  ['\\'] = '\\',
  ['b'] =  '\b',
  ['n'] =  '\n',
  ['r'] =  '\r',
  ['0'] =  -1,
  ['f'] =  '\f',
  ['t'] =  '\t',
  ['e'] =  '\033',
};

bool append_symbol(Symbol sym, Symbols *syms, bool *redefined) {
  if (lookup_symbol(sym.name, sym.length, syms, NULL) && redefined != NULL)
    *redefined = true;


  if (syms->count < syms->capacity) {
    syms->items[syms->count++] = sym;
    syms->lookup_ready = false;
    return true;
  }

  size_t new_capa = syms->capacity;
  new_capa = new_capa > 0 ? new_capa*2 : 1;
  Symbol *new = realloc(syms->items, sizeof(Symbol) * new_capa);
  if (new == NULL) {
    errno = ENOMEM;
    return false;
  }

  syms->items = new;
  syms->capacity = new_capa;
  syms->items[syms->count++] = sym;
  syms->lookup_ready = false;

  return true;
}

bool append_token_symbol(Symbols *syms, Token *tok, long value, bool *redefined) {
  return append_symbol((Symbol) {
    .name   = tok->start,
    .length = tok->length,
    .value  = value,
  }, syms, redefined);
}

int cmp_syms(const void *a, const void *b) {
    const Symbol *sym1 = a;
    const Symbol *sym2 = b;

    size_t min_len = sym1->length < sym2->length ? sym1->length : sym2->length;
    int result = strncmp(sym1->name, sym2->name, min_len);

    if (result != 0) return result;

    return (sym1->length > sym2->length) - (sym1->length < sym2->length);
}

void begin_lookups(Symbols *syms) {
  qsort(syms->items, syms->count, sizeof(Symbol), cmp_syms);
  syms->lookup_ready = true;
}

bool lookup_symbol(const char *s, size_t length, Symbols *syms, long *value) {
    if (!syms->lookup_ready) begin_lookups(syms);
  
    size_t lo = 0;
    size_t hi = syms->count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        Symbol *sym = &syms->items[mid];
        size_t min_len = length < sym->length ? length : sym->length;
        int cmp = strncmp(s, sym->name, min_len);

        if (cmp == 0) {
            if (length < sym->length) cmp = -1;
            else if (length > sym->length) cmp = 1;
        }

        if (cmp == 0) {
            if (value != NULL) *value = sym->value;
            return true;
        }

        if (cmp < 0) hi = mid;
        else lo = mid + 1;
    }

    return false;
}

long read_number(const Token *tok, Error *error) {
  char buf[256];

  snprintf(buf, sizeof(buf), "%.*s", (int)tok->length, tok->start);
  char *end;
  errno = 0;
  long value = strtol(buf, &end, 0);

  if (tok->length == 3 && tok->start[0] == '\'' && tok->start[2] == '\'')
    return (long)tok->start[1];
  if (tok->length == 4 && tok->start[0] == '\''
      && tok->start[1] == '\\' && tok->start[3] == '\'') {
    char c = escapes[(unsigned char)tok->start[2]];
    if (c == 0) {
      *error = (Error) {
        .location = tok->location,
        .message = "invalid escape sequence",
        .set = true,
      };
      return -1;
    }
    if (c == -1) c = 0;
    return c;
  }

  if (end != buf + tok->length) {
    *error = (Error) {
      .location = tok->location,
      .message = "expected a number",
      .set = true,
    };
    return -1;
  }

  if (errno == ERANGE) {
    *error = (Error) {
      .location = tok->location,
      .message = "number out of range",
      .set = true,
    };
    return -1;
  }
  
  return value;
}

char *read_file(Arena *arena, const char *path) {
  FILE *file = fopen(path, "rb");

  if (file == NULL)
    return NULL;

  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  rewind(file);

  if (size < 0) {
    fclose(file);
    return NULL;
  }

  char *contents = arena_alloc(arena, (size_t)size + 1);

  if (contents == NULL) {
    fclose(file);
    return NULL;
  }

  size_t bytes_read = fread(contents, 1, (size_t)size, file);

  fclose(file);

  if (bytes_read != (size_t)size) {
    free(contents);
    return NULL;
  }

  contents[bytes_read] = '\0';

  if (bytes_read > 0 && contents[bytes_read - 1] == '\n') {
    contents[--bytes_read] = '\0';
  }

  return contents;
}

bool colour_allowed(void) {
  const char *no_color = getenv("NO_COLOR");

  if (no_color != NULL && no_color[0] != '\0')
      return false;
  if (!isatty(STDERR_FILENO))
      return 0;

  const char *term = getenv("TERM");

  if (term == NULL || strcmp(term, "dumb") == 0)
    return false;

  return true;
}

Colours get_colours(void) {
    bool colour = colour_allowed();
    return (Colours) {
        .red     = colour ? "\033[31m" : "",
        .bold    = colour ? "\033[1m"  : "",
        .yellow  = colour ? "\033[33m" : "",
        .magenta = colour ? "\033[35m" : "",
        .dim     = colour ? "\033[2m"  : "",
        .reset   = colour ? "\033[0m"  : "",
    };
}

