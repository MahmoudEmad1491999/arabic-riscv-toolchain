CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
BUILD   := build

COMMON_SRC := src/common/rt.c src/common/utf8.c src/common/chars.c src/common/number.c

.PHONY: all test unit oracle clean

all: $(BUILD)/test_common

$(BUILD)/test_common: tests/unit/test_common.c $(COMMON_SRC) src/common/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/unit/test_common.c $(COMMON_SRC)

unit: $(BUILD)/test_common
	./$(BUILD)/test_common

# Runs every tests/asm/*.تجميع through the transliterator + GNU as/ld + qemu.
oracle:
	./tools/run_oracle.sh

test: unit oracle

clean:
	rm -rf $(BUILD)
