CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
SANITIZE ?= -fsanitize=address,undefined
BUILD   := build

COMMON_SRC := src/common/rt.c src/common/utf8.c src/common/chars.c src/common/number.c \
              src/common/buf.c src/common/diag.c src/common/obj.c
AS_SRC     := src/as/main.c src/as/lexer.c src/as/tables.c src/as/as.c
AS_BIN     := $(BUILD)/مجمع
LD_SRC     := src/ld/main.c src/ld/ld.c
LD_BIN     := $(BUILD)/رابط
CC_SRC     := src/cc/main.c src/cc/lex.c src/cc/types.c src/cc/parse.c src/cc/check.c src/cc/gen.c \
              src/cc/iface.c src/cc/tools.c src/as/lexer.c src/as/tables.c src/as/as.c src/ld/ld.c
CC_BIN     := $(BUILD)/مترجم
OBJ_SRC    := src/obj/main.c
LIB_SRC    := src/lib/main.c
FS_SRC     := src/fs/main.c
FS_BIN     := $(BUILD)/مهيئ
LIB_BIN    := $(BUILD)/محزم
OBJ_BIN    := $(BUILD)/عارض

.PHONY: all docs diagrams test unit oracle asm link cc objfmt build-mode os fuzz spec spec-check clean

all: $(AS_BIN) $(LD_BIN) $(CC_BIN) $(OBJ_BIN) $(LIB_BIN) $(FS_BIN) $(BUILD)/test_common

$(FS_BIN): $(FS_SRC) $(COMMON_SRC) src/common/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) -o $@ $(FS_SRC) $(COMMON_SRC)

$(LIB_BIN): $(LIB_SRC) $(COMMON_SRC) src/common/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) -o $@ $(LIB_SRC) $(COMMON_SRC)

$(OBJ_BIN): $(OBJ_SRC) $(COMMON_SRC) src/common/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) -o $@ $(OBJ_SRC) $(COMMON_SRC)

$(CC_BIN): $(CC_SRC) $(COMMON_SRC) src/cc/*.h src/as/*.h src/ld/*.h src/common/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) -o $@ $(CC_SRC) $(COMMON_SRC)

$(LD_BIN): $(LD_SRC) $(COMMON_SRC) src/ld/*.h src/common/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) -o $@ $(LD_SRC) $(COMMON_SRC)

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
asm: $(AS_BIN) $(LD_BIN)
	./tools/run_asm_tests.sh

# Our linker vs GNU ld: multi-file programs, relocation coverage, errors.
link: $(AS_BIN) $(LD_BIN)
	./tools/run_link_tests.sh

# The غرة compiler: programs run under qemu, compile-error tests.
cc: $(AS_BIN) $(LD_BIN) $(CC_BIN)
	./tools/run_cc_tests.sh

# Our object format: the same programs linked from ELF and from .كائن objects
# must give byte-identical executables.
objfmt: $(AS_BIN) $(LD_BIN) $(CC_BIN) $(OBJ_BIN) $(LIB_BIN)
	./tools/run_obj_tests.sh

# مترجم building whole projects: incremental builds, stale objects, libraries.
build-mode: $(AS_BIN) $(LD_BIN) $(CC_BIN) $(LIB_BIN)
	./tools/run_build_tests.sh

# The operating system (os/): bare-metal kernels on qemu-system-riscv64.
os: $(AS_BIN) $(LD_BIN) $(CC_BIN) $(FS_BIN)
	./tools/run_os_tests.sh

# The documentation as PDF, Arabic shown as in the editor (decision 48):
# build/docs/*.pdf. Needs pandoc, weasyprint and arabic-reshaper.
docs:
	python3 tools/docs_pdf.py --self-test
	python3 tools/docs_pdf.py $(BUILD)/docs $(filter-out docs/spec-language.src.md,$(wildcard docs/*.md))

# The architecture diagrams: docs/diagrams/*.dot -> .svg and .png.
diagrams:
	python3 tools/render_diagrams.py

# Random programs: ours vs GNU as and GNU ld (slow; not part of `make test`).
fuzz: $(AS_BIN) $(LD_BIN)
	./tools/diff_fuzz.py 500

# Specs with generated file-order examples.
spec:
	python3 -I tools/gen_spec.py docs/spec-language.src.md docs/spec-language.md

spec-check:
	@mkdir -p $(BUILD)
	python3 -I tools/gen_spec.py docs/spec-language.src.md $(BUILD)/spec-language.md
	cmp -s $(BUILD)/spec-language.md docs/spec-language.md || { echo "docs/spec-language.md is stale: run make spec"; exit 1; }

test: unit oracle asm link cc objfmt build-mode os spec-check

clean:
	rm -rf $(BUILD)
