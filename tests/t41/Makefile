CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
TEST_BIN := $(PROJECT_DIR)/build/tests/t41-command-layout-test

.PHONY: check clean

check: $(TEST_BIN)
	$(TEST_BIN)

$(TEST_BIN): command_layout_test.c \
	$(PROJECT_DIR)/src/t40/t41_command_layout.c \
	$(PROJECT_DIR)/src/t40/t41_command_layout.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		command_layout_test.c \
		$(PROJECT_DIR)/src/t40/t41_command_layout.c \
		-o "$@"

clean:
	$(RM) "$(TEST_BIN)"
