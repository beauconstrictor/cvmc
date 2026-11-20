#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "tokeniser.h"
#include "parser.h"
#include "mmap.h"

#include "assembler.h"

/*
 * TODO: deduplicate subroutines with the same instructions
 */

#define X(mnemonic, opcode) \
  [opcode] = mnemonic,
const char *mnemonics[] = {
  [OPCODE_PUSH] = "_push",
  [OPCODE_RET] = "_ret",
  INSTRUCTIONS
};
#undef X

int get_opcode(const char *name, size_t len, Symbols *custom_opcodes) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%.*s", (int)len, name);

  long opcode;
  if (lookup_symbol(name, len, custom_opcodes, &opcode))
    return opcode;

  #define X(mnemonic, opcode) \
      if (strcmp(buf, mnemonic) == 0) return opcode;
  INSTRUCTIONS
  #undef X

  return -1;
}


int get_opcode_tok(Token *tok, Symbols *custom_opcodes) {
  return get_opcode(tok->start, tok->length, custom_opcodes);
}

bool verify_syntax(Tokens *toks, Error *error, Symbols *custom_opcodes) {
  static char buf[256];

  int nesting = 0;
  Token definition_tok;

  for (size_t i = 0; i < toks->count; i++) {
    Token tok = toks->items[i];

    Token next;
    bool has_next = i < toks->count - 1;
    if (has_next) next = toks->items[i+1];

    if (tok.kind != TOKEN_LBRACE && nesting < 1)
      GIVE_ERROR_WITH_LEXEME("unexpected word outside of definition: '%.*s'", tok);

    switch (tok.kind) {
      case TOKEN_LSQUARE:
      case TOKEN_RSQUARE:
      case TOKEN_STRING:
      case TOKEN_ASSIGN:
      case TOKEN_IMPORT:
      case TOKEN_CONST:
      case TOKEN_LPARA:
      case TOKEN_RPARA:
      case TOKEN_COLON:
      case TOKEN_VAR: {
        GIVE_ERROR_WITH_LEXEME("unexpected '%.*s'", tok);
      } break;

      case TOKEN_LBRACE: {
        nesting++;

        if (!has_next) 
          GIVE_ERROR("expected definition name after '{'", tok);
        else if (has_next && next.kind != TOKEN_DEFINITION)
          GIVE_ERROR("expected definition name after '{'", next);
      } break;

      case TOKEN_DEFINITION: {
        if (tok.start[tok.length-1] != '*' && get_opcode_tok(&tok, custom_opcodes) != -1)
          GIVE_ERROR_WITH_LEXEME("'%.*s' is already defined as an instruction", tok);
        definition_tok = tok;
      } break;

      case TOKEN_RBRACE: {
        nesting--;
      } break;

      case TOKEN_ADDR: {
       if (!has_next)
         GIVE_ERROR_WITH_LEXEME("expected word after '%.*s'", tok);
       else if (has_next && next.kind != TOKEN_WORD)
         GIVE_ERROR_WITH_LEXEME("expected word after '%.*s'", next);
      } break;

      default: break;
    }

    if (nesting > 1)
      GIVE_ERROR("nested defintions are not allowed", tok);
  }

  if (nesting > 0)
    GIVE_ERROR("unterminated definition", definition_tok);

  return true;
}

void find_referenced_inner(Symbols *referenced, Tokens *toks,
    const char *func, size_t len, Symbols *custom_opcodes) {
  if (lookup_symbol(func, len, referenced, NULL)) return;
  if (get_opcode(func, len, custom_opcodes) != -1) return;

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
    if (tok.kind == TOKEN_LBRACE) break;
    if (tok.kind != TOKEN_WORD) continue;
    find_referenced_inner(referenced, toks, tok.start, tok.length,
        custom_opcodes);
  }
}

Symbols find_referenced(Tokens *toks, Symbols *custom_opcodes) {
  Symbols referenced = {0};
  find_referenced_inner(&referenced, toks, ENTRY_POINT,
      sizeof(ENTRY_POINT)-1, custom_opcodes);
  return referenced;
}

bool arrange_definitions(Symbols *syms, Symbols *vars, Tokens *toks,
    Error *error, Symbols *custom_opcodes, bool verbose) {
  static char buf[256];

  uint16_t pc = CODE_MIN;
  bool is_addr_of = false;

  VERBOSE("assembler", "eliminating unreferenced functions...");
  Symbols referenced = find_referenced(toks, custom_opcodes);

  for (size_t i = 0; i < toks->count; i++) {
    Token tok = toks->items[i];

    if (pc > CODE_MAX) {
      if (i + 1 >= toks->count)
        GIVE_ERROR("insufficient space for code allocation", tok);
    }

    switch (tok.kind) {
      // all eliminated by the preprocessor
      case TOKEN_IMPORT:
      case TOKEN_LSQUARE:
      case TOKEN_RSQUARE:
      case TOKEN_LBRACE:
      case TOKEN_STRING:
      case TOKEN_ASSIGN:
      case TOKEN_CONST:
      case TOKEN_LPARA:
      case TOKEN_RPARA:
      case TOKEN_COLON:
      case TOKEN_ADDR:
      case TOKEN_VAR:
        break;

      case TOKEN_DEFINITION: {
        bool redefined = false;
        if (tok.start[tok.length-1] == '*') tok.length--;

        if (!lookup_symbol(tok.start, tok.length, &referenced, NULL)) break;
        if (!append_token_symbol(syms, &tok, pc, &redefined)) return false;
        if (redefined || lookup_symbol(tok.start, tok.length, vars, NULL))
          GIVE_ERROR_WITH_LEXEME("'%.*s' is already defined", tok);
      } break;

      case TOKEN_NUMBER: {
        // 1. push opcode - one byte
        // 2. (16-bit) operand - two bytes
        pc += 3;
      } break;

      case TOKEN_WORD: {
        int opcode = get_opcode_tok(&tok, custom_opcodes);
        if (opcode != -1) {
          pc++;
          break;
        }
        // 1. push opcode - one byte
        // 2. (16-bit) operand - two bytes
        pc += 3;
        // 3. call opcode - one byte
        if (!is_addr_of)
          pc++;
      } break;

      case TOKEN_RBRACE:
      case TOKEN_BYTE: {
        // inserts RAW bytes, not to be interpreted as opcodes
        pc++;
      } break;
    }

    is_addr_of = tok.kind == TOKEN_ADDR;
  }

  return true;
}

bool assemble(FILE *f, Symbols *syms, Symbols *vars, Tokens *toks,
    Error *error, Symbols *custom_opcodes, bool verbose) {
  #define EMIT_BYTE(byte) fprintf(f,"%02x ", byte &   0xff);
  #define EMIT_WORD(word) fprintf(f,"%02x %02x ", (word & 0xff00) >> 8, word & 0xff);

  VERBOSE("assembler", "arranging subroutine addresses");
  if (!arrange_definitions(syms, vars, toks, error, custom_opcodes,
        verbose))
    return false;

  static char buf[256];
  long pc;
  bool should_emit = true;

  VERBOSE("assembler", "emitting tape image...");

  fprintf(f, "T\n");

  for (size_t i = 0; i < toks->count; i++) {
    Token tok = toks->items[i];

    Token next;
    bool has_next = i < toks->count - 1;
    if (has_next) next = toks->items[i+1];

    if (!should_emit) {
      if (tok.kind == TOKEN_RBRACE) should_emit = true;
      continue;
    }
    switch (tok.kind) {
      case TOKEN_DEFINITION: {
        if (tok.start[tok.length-1] == '*') tok.length--;
        should_emit = lookup_symbol(tok.start, tok.length, syms, &pc);
        if (should_emit) fprintf(f, ":%04x ", (unsigned)pc);
      } break;

      case TOKEN_RBRACE: {
        fprintf(f, "%02x ;\n", OPCODE_RET);
      } break;

      case TOKEN_BYTE: {
        long byte = read_number(&tok, error);
        if (error->set) return false;
        EMIT_BYTE((int)byte);
      } break;

      case TOKEN_ADDR: {
        long addr;
        if (!lookup_symbol(next.start, next.length, syms, &addr)) {
          if (get_opcode_tok(&next, custom_opcodes) != -1) {
            GIVE_ERROR_WITH_LEXEME("'%.*s' is an instruction, not a function and does not have an address", next);
          } else
            GIVE_ERROR_WITH_LEXEME("undefined symbol: '%.*s'", next);
        }
        if (error->set) return false;
        EMIT_BYTE(OPCODE_PUSH);
        EMIT_WORD((int)addr);
        i++; // skip past the following word
      } break;

      case TOKEN_NUMBER: {
        long word = read_number(&tok, error);
        EMIT_BYTE(OPCODE_PUSH);
        EMIT_WORD((int)word);
      } break;

      case TOKEN_WORD: {
        int opcode = get_opcode_tok(&tok, custom_opcodes);
        if (opcode >= 0) {
          EMIT_BYTE(opcode);
          break;
        }

        long addr;
        if (!lookup_symbol(tok.start, tok.length, syms, &addr))
          GIVE_ERROR_WITH_LEXEME("undefined symbol: '%.*s'", tok);
        if (error->set) return false;
        EMIT_BYTE(OPCODE_PUSH);
        EMIT_WORD((int)addr);
        EMIT_BYTE(OPCODE_CALL);
      } break;

      default: break;
    }
  }
  
  long main_addr;
  if (lookup_symbol(ENTRY_POINT, sizeof(ENTRY_POINT)-1, syms, &main_addr))
    fprintf(f, "!%04x\nT\n", (unsigned)main_addr);

  #undef EMIT_BYTE
  #undef EMIT_WORD

  return true;
}
