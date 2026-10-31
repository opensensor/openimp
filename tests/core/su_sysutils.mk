CC ?= cc
BIN := build/tests/su-sysutils-test

check:
	mkdir -p build/tests
	$(CC) -std=gnu99 -D_GNU_SOURCE -Wall -Wextra -Werror tests/core/su_sysutils_test.c -o $(BIN) -lpthread
	$(BIN)
