CC = clang
CFLAGS = -Wall -Wextra -Werror -g
DEPFLAGS = -MMD -MP
TARGET = build/cvmc

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c,build/%.o,$(SRCS))
STD_SRCS = $(wildcard src/std/*.cvm)
STDS = $(patsubst src/std/%.cvm,build/%.h,$(STD_SRCS))

.PHONY: all
all: $(TARGET)

build:
	mkdir -p build

build/%.h: src/std/%.cvm | build
	xxd -i $< $@

build/%.o: src/%.c $(STDS) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -Isrc -c $< -o $@

$(TARGET): build/std.h $(OBJS)
	$(CC) -g $(OBJS) -o $@

.PHONY: clean
clean:
	rm -rf build/

.PHONY: run
run: $(TARGET)
	$(TARGET) -i test.cvm

.PHONY: test
test: $(TARGET) | build
	stdbuf -oL $(TARGET) -i demos/tests.cvm 2>&1 | tee build/tests.txt

.PHONY: dbg
dbg: $(TARGET)
	gdb $(TARGET)

DEPS = $(OBJS:.o=.d)
-include $(DEPS)
