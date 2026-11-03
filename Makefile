CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
SANITIZE ?= -fsanitize=address,undefined
BUILD   := build

COMMON_SRC := src/common/rt.c src/common/utf8.c src/common/chars.c src/common/number.c \
              src/common/buf.c src/common/diag.c
AS_SRC     := src/as/main.c src/as/lexer.c src/as/tables.c src/as/as.c
AS_BIN     := $(BUILD)/مجمع

.PHONY: all test unit oracle asm fuzz clean

all: $(AS_BIN) $(BUILD)/test_common

$(AS_BIN): $(AS_SRC) $(COMMON_SRC) src/as/*.h src/common/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) -o $@ $(AS_SRC) $(COMMON_SRC)

$(BUILD)/test_common: tests/unit/test_common.c $(COMMON_SRC) src/common/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) -o $@ tests/unit/test_common.c $(COMMON_SRC)

unit: $(BUILD)/test_common
	./$(BUILD)/test_common

# Runs every tests/asm/*.تجميع through the transliterator + GNU as/ld + qemu.
oracle:
	./tools/run_oracle.sh

# Our assembler vs GNU as on every tests/asm file, plus the error tests.
asm: $(AS_BIN)
	./tools/run_asm_tests.sh

# Random programs: ours vs GNU as (slow; not part of `make test`).
fuzz: $(AS_BIN)
	./tools/diff_fuzz.py 500

test: unit oracle asm

clean:
	rm -rf $(BUILD)
