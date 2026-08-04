# Standalone build for the fable_robot_eyes contribution.
#
#   make test    build + run the unit/behavior suite
#   make dump    build the frame-dump inspection tool
#   make strict  rebuild at -O0 and -O2 and compare frame hashes
#   make clean

CC ?= cc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wshadow -Wconversion \
	-Wno-sign-conversion
BUILD := build

CORE_SRC := fre_draw.c fre_behavior.c fre_profiles.c
CORE_HDR := fable_robot_eyes.h fre_internal.h

$(BUILD)/%.o: %.c $(CORE_HDR)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

CORE_OBJ := $(CORE_SRC:%.c=$(BUILD)/%.o)

$(BUILD)/test_fable_robot_eyes: tests/test_fable_robot_eyes.c $(CORE_OBJ) \
		$(CORE_HDR)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I. tests/test_fable_robot_eyes.c $(CORE_OBJ) -o $@

$(BUILD)/fre_dump: tools/fre_dump.c $(CORE_OBJ) $(CORE_HDR)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I. tools/fre_dump.c $(CORE_OBJ) -o $@

.PHONY: test dump strict clean

test: $(BUILD)/test_fable_robot_eyes
	./$(BUILD)/test_fable_robot_eyes

dump: $(BUILD)/fre_dump

# Byte-identity across optimization levels approximates the WASM
# determinism requirement: the render must not depend on evaluation
# order, fast-math, or implementation-defined shifts.
strict:
	@rm -rf $(BUILD)/o0 $(BUILD)/o2
	@mkdir -p $(BUILD)/o0 $(BUILD)/o2
	$(CC) -std=c11 -O0 -Wall -Wextra -Werror -I. \
		$(CORE_SRC) tools/fre_dump.c -o $(BUILD)/o0/fre_dump
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -I. \
		$(CORE_SRC) tools/fre_dump.c -o $(BUILD)/o2/fre_dump
	./$(BUILD)/o0/fre_dump hash > $(BUILD)/o0/hashes.txt
	./$(BUILD)/o2/fre_dump hash > $(BUILD)/o2/hashes.txt
	cmp $(BUILD)/o0/hashes.txt $(BUILD)/o2/hashes.txt
	@echo "strict: -O0 and -O2 frames are byte-identical"

clean:
	rm -rf $(BUILD)
