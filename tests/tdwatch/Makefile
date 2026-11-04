CC ?= cc
BUILD := ../../build/tests/tdwatch

.PHONY: check clean

# apitest teardown stall watch (tools/apitest/at_tdwatch.c, issue #198): a faked stall writes ONE block, a normal teardown nothing
check: tdwatch_test.c ../../tools/apitest/at_tdwatch.c
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -I../../tools/apitest \
		tdwatch_test.c ../../tools/apitest/at_tdwatch.c -lpthread -lrt -o $(BUILD)/tdwatch-test
	$(BUILD)/tdwatch-test $(BUILD)

clean:
	$(RM) -r $(BUILD)
