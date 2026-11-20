#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>

#include "parser.h"
#include "tokeniser.h"
#include "assembler.h"
#include "simulator.h"
#include "preprocessor.h"

/*
 * TODO: remove duplicated subroutines (by content, not name)
 */

#define USAGE "Usage: %s [OPTION]... [MODE] FILE \n" \
              "Try passing '-h' for more information.\n"

#define HELP "Usage: %s [OPTION]... FILE \n" \
             "Compile CVM programs.\n" \
             "\n" \
             "Options:\n" \
             "\t-h  Show this message and exit\n" \
             "\t-v  Show version number and license information\n" \
             "\t-V  Show verbose compiler output\n" \
             "\t-t  Format IR as raw tokens\n" \
             "\t-s  File to write a table of function addresses to\n" \
             "\t-o  File to write binary or IR to\n" \
             "\t-d  Maximum allowed call stack depth in the interpreter\n" \
             "\t-DX Pass 'MNEMONIC=OPCODE to define a new opcode\n" \
             "\n" \
             "Modes:\n" \
             "\t-l  Stop compilation after lexing\n" \
             "\t-pX Stop compilation after a specific preprocessing stage (0 for all)\n" \
             "\t-c  Fully compile program (default)\n" \
             "\t-i  Immediately interpret program\n" \
             "\n" \
             "Use '-' in place of paths to use the standard output.\n" \

#define VERSION "%s (Compact Virtual Machine Compiler) version 0.1.0\n" \
                "Licensed under the FOSS MIT license.\n" \
                "\n" \
                "Written by Beau Constrictor; see\n" \
                "<https://github.com/beauconstrictor/cvmc>\n"

typedef enum {
  MODE_LEXER,
  MODE_PREPROCESS,
  MODE_COMPILE,
  MODE_INTERPRET,
} CompilationMode;

void show_help(const char *binary) {
  printf(HELP, binary);
}

void show_version(const char *binary) {
  printf(VERSION, binary);
}

void show_ir(Tokens *toks, CompilationMode mode, bool raw_tokens,
    bool dump_syms, const char *binary, bool verbose) {
  if (dump_syms) {
    if (mode == MODE_LEXER)
      cli_error("cannot generate a symbol table in lexing mode", binary);
    if (mode == MODE_PREPROCESS)
      cli_error("cannot generate a symbol table in preprocessing mode", binary);
  } else if (raw_tokens) {
    for (size_t i = 0; i < toks->count; i++) {
      Token tok = toks->items[i];
      printf("%s %.*s @ %s:%zu:%zu\n", token_kind_name(tok.kind),
          (int)tok.length, tok.start, tok.location.path,
          tok.location.line, tok.location.col);
    }

  } else {
    VERBOSE("lexer", "serialising source code...");
    delexer(toks, stdout);
    printf("\n");
  }
}

FILE *get_handle(const char *path, const char *binary, bool binmode) {
  if (strcmp(path, "-") == 0) return stdout;

  FILE *handle = fopen(path, binmode ? "wb" : "w");
  if (handle == NULL) {
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s: %s", binary, path);
    perror(buf);
    exit(1);
  }
  return handle;
}

void dump_symbols(Symbols *syms, FILE *f) {
  for (size_t i = 0; i < syms->count; i++) {
    Symbol sym = syms->items[i];
    if (strncmp(sym.name, ANON_PREFIX, sizeof(ANON_PREFIX)-1) == 0)
      continue;
    fprintf(f, "%.*s\t%ld\n", (int)sym.length, sym.name, sym.value);
  }
}

int main(int argc, char **argv) {
  int opt;

  char output[PATH_MAX] = "a.out";
  char symtable[PATH_MAX];
  CompilationMode mode = MODE_COMPILE;
  Symbols custom_opcodes = {0};
  Arena arena = {0};
  bool raw_tokens = false;
  bool dump_syms  = false;
  bool verbose    = true;
  int pp_stage   = 0;
  size_t call_stack_depth = DEFAULT_CALL_STACK_DEPTH;

  while ((opt = getopt(argc, argv, "hvo:tp:lcis:D:V")) != -1) {
    switch (opt) {
      case 'h': {
        show_help(argv[0]);
        return 0;
      } break;
      case 'v': {
        show_version(argv[0]);
        return 0;
      } break;
      case 'o': {
        snprintf(output, sizeof(output), "%s", optarg);
      } break;
      case 'l': {
        mode = MODE_LEXER;
      } break;
      case 'p': {
        mode = MODE_PREPROCESS;
        if (strlen(optarg) > 0)
          pp_stage = atoi(optarg);
        else
          cli_error(USAGE, argv[0]);
      } break;
      case 'c': {
        mode = MODE_COMPILE;
      } break;
      case 'i': {
        mode = MODE_INTERPRET;
      } break;
      case 't': {
        raw_tokens = true;
      } break;
      case 'V': {
        verbose = true;
      } break;
      case 'd': {
        mode = MODE_PREPROCESS;
        if (strlen(optarg) > 0)
          call_stack_depth = atoi(optarg);
        else
          cli_error(USAGE, argv[0]);
      } break;
      case 's': {
        dump_syms = true;
        snprintf(symtable, sizeof(symtable), "%s", optarg);
      } break;
      case 'D': {
        char *mnemonic = arena_alloc(&arena, 32);
        int opcode;
        bool redefined;
        if (sscanf(optarg, "%31[^1=]=%d", mnemonic, &opcode) != 2)
          cli_error(USAGE, argv[0]);
        if (opcode < 0)
          cli_error("defined opcode must be positive", argv[0]);
        if (opcode > 0xff)
          cli_error("defined opcode must be a single byte", argv[0]);
        append_symbol((Symbol){
            .name = mnemonic,
            .length = strlen(mnemonic),
            .value = opcode,
        }, &custom_opcodes, &redefined);
        if (redefined)
          cli_error("an instruction cannoth have multiple definitions", argv[0]);
      } break;
    }
  }

  if (argc - optind != 1)
    cli_error(USAGE, argv[0]);
  char *filename = argv[optind];
  char *source = read_file(&arena, filename);

  if (source == NULL) cli_error(strerror(errno), argv[0]);

  Tokens toks = create_tokens();
  Error error = {0};

  VERBOSE("lexer", "tokenising source code...");
  if (!lexer(source, filename, &toks, &error)) check_error(&error);
  if (mode == MODE_LEXER) {
    show_ir(&toks, mode, raw_tokens, dump_syms, argv[0], verbose);
    return 0;
  }
  Symbols vars = {0};
  if (!preprocessor(&arena, &toks, &vars, &error, pp_stage, verbose)) check_error(&error);
  if (mode == MODE_PREPROCESS) {
    show_ir(&toks, mode, raw_tokens, dump_syms, argv[0], verbose);
    return 0;
  }

  if (!verify_syntax(&toks, &error, &custom_opcodes)) check_error(&error);

  Symbols syms = {0};
  FILE *out = get_handle(output, argv[0], false);

  if (!assemble(out, &syms, &vars, &toks, &error, &custom_opcodes,
        verbose))
    check_error(&error);

  long main_addr;
  if (!lookup_symbol(ENTRY_POINT, sizeof(ENTRY_POINT)-1, &syms, &main_addr))
    cli_error("missing 'main' entry point", argv[0]);

  if (dump_syms) {
    FILE *syms_out = get_handle(symtable, argv[0], false);
    if (syms_out == out) fprintf(out, "\n");
    dump_symbols(&vars, syms_out);
    fprintf(syms_out, "\n");
    dump_symbols(&syms, syms_out);
  }
  
  if (out != stdout) fclose(out);

  if (mode == MODE_INTERPRET) {
    char *tape = read_file(&arena, output);
    if (tape == NULL) cli_error("no such file", argv[0]);
    const char *error = simulate(tape, &syms, call_stack_depth);
    if (error) cli_error(error, argv[0]);
  }

  arena_free(&arena);

  return 0;
}
