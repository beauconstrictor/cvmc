#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "errors.h"

void print_error(const Error *error) {
  Colours cols = get_colours();

  fprintf(stderr, "%s%s:%zu:%zu: %serror: %s%s\n", cols.bold,
      error->location.path, error->location.line,
      error->location.col, cols.red, cols.reset, error->message);

  FILE *f = fopen(error->location.path, "r");

  // if we can't show the error line for whatever reason, we just
  // don't bother
  if (f == NULL) {
    fprintf(stderr, "\n");
    return;
  }

  char line[4096];

  for (size_t i = 1; i <= error->location.line; i++) {
    if (fgets(line, sizeof(line), f) == NULL) {
      fclose(f);
      fprintf(stderr, "\n");
      return;
    }
  }

  int col = error->location.col;

  fprintf(stderr, "%5zu %s|%s %s", error->location.line, cols.dim,
      cols.reset, line);
  fprintf(stderr, "%s%5s | %s", cols.dim, "", cols.reset);
  if (col > 0) {
    for (int i = 0; i < col-1; i++)
      fputc(' ', stderr);
    fprintf(stderr, "%s^%s\n\n", cols.yellow, cols.reset);
  }

  fclose(f);
}

void check_error(const Error *error) {
  if (error->set) {
    print_error(error);
  } else {
    perror("cvmc");
  }
  exit(1);
}

void cli_error(const char *s, const char *binary) {
  Colours cols = get_colours();

  fprintf(stderr, "%s%s%s: %s", cols.red, cols.bold, binary, cols.reset);
  fprintf(stderr, "%s", s);
  fprintf(stderr, "\n");
  exit(1);
}

