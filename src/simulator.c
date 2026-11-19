#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "assembler.h"
#include "parser.h"

#include "simulator.h"

// #define TRACER

int hex_nibble(char c) {
  if (c >= '0' && c <= '9')
      return c - '0';
  if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
  return -1;
}

uint8_t read_hex(const char *s) {
  // we don't actually check it's valid hex because this it always is
  // as it comes directly from the assembler
  int val = hex_nibble(*s++) << 4;
  val |= hex_nibble(*s);
  return val; 
}

uint16_t read_hex_addr(const char *s) {
  // we don't actually check it's valid hex because this it always is
  // as it comes directly from the assembler
  int val = read_hex(s) << 8;
  s += 2;
  return val | read_hex(s);
}

typedef struct {
  uint16_t data[1024];
  size_t head;
} DStack;

bool dstack_push(DStack *dstack, uint16_t val) {
  if (dstack->head >= 1024) return false;
  dstack->data[dstack->head++] = val;
  return true;
}

bool dstack_pop(DStack *dstack, uint16_t *val) {
  if (dstack->head == 0) return false;
  *val = dstack->data[--dstack->head];
  return true;
}

typedef enum {
  DEBUGGER_CONTINUE,
  DEBUGGER_STEP
} DebuggerMode;

typedef struct {
  const char *mnemonic;
  uint16_t a;
  uint16_t b;
  uint16_t c;
  bool a_is_callable;
  bool b_is_callable;
  bool c_is_callable;
  int op_count;
} Instruction;

void dump_stack(FILE *f, DStack *dstack) {
  Colours cols = get_colours();

  fprintf(f, "%s<", cols.bold);
  for (size_t i = 0; i < dstack->head; i++)
    fprintf(f, "%d ", dstack->data[i]);
  if (dstack->head > 0) fprintf(f, "\b");
  fprintf(f, ">%s\n", cols.reset);
}


const char *get_name(uint16_t addr, int *len, Symbols *syms) {
  const char *name = "unknown";

  for (size_t i = 0; i < syms->count; i++) {
    Symbol s = syms->items[i];
    if (s.value != addr) continue;
    name = s.name;
    *len = s.length;
    break
  }

  return name;
}

void dump_inst(FILE *f, Instruction *inst, Symbols *syms) {
  Colours cols = get_colours();

  #define PRINT_OPERAND(letter) \
    do { \
      if (inst->letter##_is_callable) {\
        name = get_name(inst->letter, &len, syms); \
        fprintf(f, "%.*s ", len, name); \
      } else { \
        fprintf(f, "%d ", inst->letter); \
      } \
    } while (false)

  int len;
  const char *name;

  fprintf(f, "%s%s%s ", cols.yellow, inst->mnemonic, cols.reset);
  if (inst->op_count > 0) PRINT_OPERAND(a);
  if (inst->op_count > 1) PRINT_OPERAND(b);
  if (inst->op_count > 2) PRINT_OPERAND(c);
}

DebuggerMode debugger(Instruction inst, DStack *dstack, Symbols *syms) {
  #define COMMAND(name) \
    (strcmp(buf, name) == 0 || (buf[0] == name[0] && buf[1] == '\0'))

  fflush(stdout);

  Colours cols = get_colours();
 
  dump_inst(stderr, &inst, syms);
  fprintf(stderr, "\t%sresulting stack: %s", cols.dim, cols.reset);
  dump_stack(stderr, dstack);

  fprintf(stderr, "%stype 'help' for help %s%s(dbg) %s", cols.dim,
      cols.reset, cols.bold, cols.reset);

  static char buf[256];
  char inp[256];
  fgets(inp, sizeof(inp), stdin);
  inp[strcspn(inp, "\n")] = '\0';
  if (strlen(inp) > 0) strcpy(buf, inp);

  if (COMMAND("help")) {
    fprintf(stderr, "help - show this help\n");
    fprintf(stderr, "step - execute a single instruction\n");
    fprintf(stderr, "continue - resume program\n");
    fprintf(stderr, "abort - exit to command line\n");
    return debugger(inst, dstack, syms);
  } else if (COMMAND("step")) {
    return DEBUGGER_STEP;
  } else if (COMMAND("continue")) {
    return DEBUGGER_CONTINUE;
  } else if (COMMAND("abort")) {
    cli_error("abort", "dbg");
    exit(0);
    return DEBUGGER_CONTINUE;
  } else {
    fprintf(stderr, "%s%sdbg: %sunknown command\n", cols.red, cols.bold, cols.reset);
    return debugger(inst, dstack, syms);
  }
}

const char *execute(DStack *dstack, uint16_t addr, uint8_t *memory,
    int memsize, Symbols *syms) {
  static char err_msg[256];

  #define POP(var) \
        if (!dstack_pop(dstack, &var)) return "stack underflow";
  #define PUSH(var) \
        if (!dstack_push(dstack, var)) return "stack overflow";

  #define BINARY_OPERATOR(opcode, operation) \
        case OPCODE_##opcode: { \
          uint16_t ub; \
          POP(ub); \
          uint16_t ua; \
          POP(ua); \
          int16_t a = ua; \
          int16_t b = ub; \
          PUSH(operation); \
          inst = (Instruction){ \
            .mnemonic = #operation, \
            .a = a, \
            .b = b, \
            .op_count = 2, \
          }; \
        } break;

  #define CALL(addr) \
    do { \
        const char *error = execute(dstack, addr, memory, \
            memsize, syms); \
        if (error) return error; \
    } while (false)

  bool single_step = false;

    Instruction inst = {
      .mnemonic = "call",
      .a = addr,
      .b = 0,
      .c = 0,
      .a_is_callable = true,
      .b_is_callable = false,
      .c_is_callable = false,
      .op_count = 1,
    };

  for (;;) {
    if (addr >= memsize) return "segmentation fault";
    uint8_t opcode = memory[addr];

    switch (opcode) {
      case OPCODE_CALL: {
        uint16_t subroutine;
        POP(subroutine);

        inst = (Instruction){
          .mnemonic = "call",
          .a = subroutine,
          .a_is_callable = true,
          .op_count = 1,
        };

        CALL(subroutine);
      } break;

      case OPCODE_PUSH: {
        if (addr >= memsize - 2) return "segmentation fault";
        uint16_t val = (memory[addr+1] << 8) | memory[addr + 2];
        addr += 2;
        PUSH(val);

        inst = (Instruction){
          .mnemonic = "number",
          .a = val,
          .op_count = 1,
        };
      } break;

      case OPCODE_RET: {
        inst = (Instruction){
          .mnemonic = "ret",
        };

        return NULL;
      } break;

      case OPCODE_IF: {
        uint16_t false_block;
        POP(false_block);
        uint16_t true_block;
        POP(true_block);
        uint16_t condition;
        POP(condition);

        inst = (Instruction){
          .mnemonic = "if-else",
          .a = condition,
          .b = true_block,
          .c = false_block,
          .a_is_callable = 1,
          .op_count = 3,
        };

        if (condition == 0)
          CALL(false_block);
        else
          CALL(true_block);
      } break;

      case OPCODE_WHIL: {
        uint16_t subroutine;
        POP(subroutine);
        uint16_t checker;
        POP(checker);
        uint16_t condition;
 
        inst = (Instruction){
          .mnemonic = "while",
          .a = checker,
          .b = subroutine,
          .a_is_callable = 1,
          .op_count = 2,
        };

        CALL(checker);
        POP(condition);

        while (condition) {
          CALL(subroutine);
          CALL(checker);
          POP(condition);
        }
      } break;

      BINARY_OPERATOR(ADD, a + b)
      BINARY_OPERATOR(SUB, a - b)

      BINARY_OPERATOR(GT,  a > b)

      case OPCODE_DUP: {
        uint16_t val;
        POP(val);
        PUSH(val);
        PUSH(val);

        inst = (Instruction){
          .mnemonic = "dup",
          .a = val,
          .op_count = 1,
        };
      } break;

      case OPCODE_DROP: {
        uint16_t val;
        POP(val);

        inst = (Instruction){
          .mnemonic = "drop",
          .a = val,
          .op_count = 1,
        };
      } break;

      case OPCODE_SWAP: {
        uint16_t b;
        POP(b);
        uint16_t a;
        POP(a);
        PUSH(b);
        PUSH(a);

        inst = (Instruction){
          .mnemonic = "swap",
          .a = a,
          .b = b,
          .op_count = 2,
        };
      } break;

      case OPCODE_ROT: {
        uint16_t c;
        POP(c);
        uint16_t b;
        POP(b);
        uint16_t a;
        POP(a);
        PUSH(b);
        PUSH(c);
        PUSH(a);

        inst = (Instruction){
          .mnemonic = "rot",
          .a = a,
          .b = b,
          .c = c,
          .op_count = 3,
        };
      } break;

      case OPCODE_LOAD: {
        uint16_t a;
        POP(a);
        PUSH(memory[a]);

        inst = (Instruction){
          .mnemonic = "load",
          .a = a,
          .op_count = 1,
        };
      } break;

      case OPCODE_STORE: {
        uint16_t a;
        POP(a);
        uint16_t val;
        POP(val);
        memory[a] = val & 0xff;
        
        inst = (Instruction){
          .mnemonic = "store",
          .a = a,
          .b = val,
          .op_count = 2,
        };
      } break;

      case OPCODE_GET: {
        uint16_t a;
        POP(a);

        if (a == 65535)
          return "memory read out of bounds";

        PUSH((memory[a] << 8) | memory[a+1]);

        inst = (Instruction){
          .mnemonic = "get",
          .a = a,
          .op_count = 1,
        };
      } break;

      case OPCODE_PUT: {
        uint16_t a;
        POP(a);
        uint16_t val;
        POP(val);

        if (a == 65535)
          return "memory write out of bounds";

        memory[a] = val >> 8;
        memory[a+1] = val & 0xff;

        inst = (Instruction){
          .mnemonic = "set",
          .a = a,
          .b = val,
          .op_count = 2,
        };
      } break;

      case OPCODE_PUTC: {
        uint16_t val;
        POP(val);
        putchar(val);

        inst = (Instruction){
          .mnemonic = "putc",
          .a = val,
          .op_count = 1,
        };
      } break;

      case OPCODE_GETS: {
        uint16_t size;
        POP(size);
        uint16_t addr;
        POP(addr);
        if (addr > 65535-size)
          return "gets buffer too large";
        assert(sizeof(char) == sizeof(uint8_t));
        char *buf = (char *)(memory + addr);
        if (fgets(buf, size, stdin) == NULL && size > 0)
          buf[0] = '\0';
        else
          buf[strcspn(buf, "\n")] = '\0';

        inst = (Instruction){
          .mnemonic = "gets",
          .a = addr,
          .b = size,
          .op_count = 2,
        };

      } break;

      case OPCODE_BREAK: {
        Colours cols = get_colours();
        int name_len;
        const char *name = get_name(addr, &name_len, syms);

        fprintf(stderr, "%s%sdbg: %sbreakpoint hit in '%s%.*s%s' (address %d) \n",
            cols.red, cols.bold, cols.reset,
            cols.yellow, name_len, name, cols.reset,
            addr);

        DebuggerMode mode = debugger(inst, dstack, syms);

        if (mode == DEBUGGER_STEP) 
          single_step = true;

      } break;

      default: {
        inst = (Instruction){
          .mnemonic = "unknown",
          .a = 0,
          .b = 0,
          .c = 0,
          .a_is_callable = false,
          .b_is_callable = false,
          .c_is_callable = false,
          .op_count = 0,
        };

        snprintf(err_msg, sizeof(err_msg),
            "unsupported opcode: 0x%02x @ %d (0x%04x)", opcode,
            addr, addr);
        return err_msg;
      } break;
    }

    if (single_step && memory[addr] != OPCODE_BREAK) {
      DebuggerMode mode = debugger(inst, dstack, syms);
      single_step = mode == DEBUGGER_STEP;
    }

    addr++;
  }

  #undef PUSH
  #undef POP
}

const char *simulate(const char *s, Symbols *syms) {
  int addr = -1;

  uint8_t memory[65536] = {0};

  DStack dstack = {0};

  while (*s) {
    char c = *s++;

    if (strchr("\n T", c)) continue;

    if (c == ';') {
      addr = -1;
    } else if (addr >= 0) {
      memory[addr++] = read_hex(s - 1);
      s += 1;
    } else if (c == ':') {
      addr = read_hex_addr(s);
      s += 4;
    } else if (c == '!') {
      const char *error = execute(&dstack, read_hex_addr(s), memory,
          sizeof(memory), syms);
      if (error) return error;
    }
  }

  return NULL;
}
