CC ?= cc
BIN := build/tests/su-base-shutdown-test

check:
	mkdir -p build/tests
	$(CC) -std=gnu99 -Wall -Wextra -Werror -DOPENIMP_SU_KILL=fake_kill \
		-DOPENIMP_SU_SYNC=fake_sync tests/core/su_base_shutdown_test.c -o $(BIN)
	$(BIN)
