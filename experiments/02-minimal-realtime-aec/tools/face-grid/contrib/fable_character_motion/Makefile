CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -Wshadow \
    -Wdouble-promotion -Wfloat-equal
OUT := out

LIB_SRC := src/fable_ease.c src/fable_motion.c src/fable_studies.c

.PHONY: all test golden bench ppm poison clean

all: test

$(OUT):
	mkdir -p $(OUT)

$(OUT)/test_ease: tests/test_ease.c $(LIB_SRC) | $(OUT)
	$(CC) $(CFLAGS) -o $@ tests/test_ease.c $(LIB_SRC)

$(OUT)/test_motion: tests/test_motion.c $(LIB_SRC) | $(OUT)
	$(CC) $(CFLAGS) -o $@ tests/test_motion.c $(LIB_SRC)

$(OUT)/test_studies: tests/test_studies.c $(LIB_SRC) | $(OUT)
	$(CC) $(CFLAGS) -o $@ tests/test_studies.c $(LIB_SRC)

$(OUT)/bench_studies: tests/bench_studies.c $(LIB_SRC) | $(OUT)
	$(CC) $(CFLAGS) -o $@ tests/bench_studies.c $(LIB_SRC)

$(OUT)/render_ppm: tools/render_ppm.c $(LIB_SRC) | $(OUT)
	$(CC) $(CFLAGS) -o $@ tools/render_ppm.c $(LIB_SRC)

test: $(OUT)/test_ease $(OUT)/test_motion $(OUT)/test_studies
	$(OUT)/test_ease
	$(OUT)/test_motion
	$(OUT)/test_studies

golden: $(OUT)/test_studies
	FABLE_GOLDEN_UPDATE=1 $(OUT)/test_studies

bench: $(OUT)/bench_studies
	$(OUT)/bench_studies

ppm: $(OUT)/render_ppm | $(OUT)
	$(OUT)/render_ppm $(OUT) 10 700

# The library must stay float-free: poison the tokens and recompile.
poison: | $(OUT)
	$(CC) $(CFLAGS) -include tests/poison.h -fsyntax-only $(LIB_SRC)

clean:
	rm -rf $(OUT)
