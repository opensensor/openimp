CC ?= cc
BIN := build/tests/su-base-wkup-test

check:
	mkdir -p build/tests
	$(CC) -std=gnu99 -Wall -Wextra -Werror tests/core/su_base_wkup_test.c -o $(BIN)
	$(BIN)
