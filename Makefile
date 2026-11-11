CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS := -Iinclude
BIN      := miniquery
TEST_BIN := test_materialized
BENCH_BIN := miniquery_bench
CORE_SRC := src/table.c src/scan.c src/materialized.c src/aggregate.c src/join.c src/group_by.c

all: $(BIN)

$(BIN): $(CORE_SRC) src/main.c include/table.h include/materialized.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE_SRC) src/main.c -o $@

$(TEST_BIN): $(CORE_SRC) tests/test_materialized.c include/table.h include/materialized.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE_SRC) tests/test_materialized.c -o $@

$(BENCH_BIN): $(CORE_SRC) bench/benchmark.c include/table.h include/materialized.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE_SRC) bench/benchmark.c -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer" test

valgrind: $(TEST_BIN)
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./$(TEST_BIN)

bench: $(BENCH_BIN)
	./$(BENCH_BIN) smoke versioned data/funcionarios_etapa3.csv data/departamentos.csv hash 5000 5 1 header > bench/results.csv
	./$(BENCH_BIN) smoke versioned data/funcionarios_etapa3.csv data/departamentos.csv nested_loop 5000 5 1 >> bench/results.csv

experiments: $(BENCH_BIN)
	python3 bench/run_experiments.py

clean:
	rm -f $(BIN) $(TEST_BIN) $(BENCH_BIN) bench/results.csv

.PHONY: all clean test sanitize valgrind bench experiments
