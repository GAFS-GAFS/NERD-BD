CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
LDFLAGS :=

TARGET := miniquery
SRC := src/main.c src/table.c src/scan.c src/materialized.c src/aggregate.c
TEST_TARGET := tests/test_materialized
TEST_SRC := tests/test_materialized.c src/table.c src/scan.c src/materialized.c src/aggregate.c

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRC) include/table.h include/materialized.h
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -o $(TARGET)

$(TEST_TARGET): $(TEST_SRC) include/table.h include/materialized.h
	$(CC) $(CFLAGS) $(TEST_SRC) $(LDFLAGS) -o $(TEST_TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(TARGET).exe $(TEST_TARGET).exe
