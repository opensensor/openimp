CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
COMMAND_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-command-layout-test
RATE_CONTROL_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-rate-control-test
EP1_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-ep1-layout-test
TEST_BINS := $(COMMAND_TEST_BIN) $(RATE_CONTROL_TEST_BIN) $(EP1_TEST_BIN)

.PHONY: check clean

check: $(TEST_BINS)
	$(COMMAND_TEST_BIN)
	$(RATE_CONTROL_TEST_BIN)
	$(EP1_TEST_BIN)

$(COMMAND_TEST_BIN): command_layout_test.c \
	$(PROJECT_DIR)/src/t40/t41_command_layout.c \
	$(PROJECT_DIR)/src/t40/t41_command_layout.h \
	$(PROJECT_DIR)/src/t40/t41_command_builder.c \
	$(PROJECT_DIR)/src/t40/t41_command_builder.h \
	$(PROJECT_DIR)/src/t40/t41_hw_rate_control.c \
	$(PROJECT_DIR)/src/t40/t41_hw_rate_control.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		command_layout_test.c \
		$(PROJECT_DIR)/src/t40/t41_command_layout.c \
		$(PROJECT_DIR)/src/t40/t41_command_builder.c \
		$(PROJECT_DIR)/src/t40/t41_hw_rate_control.c \
		-o "$@"

$(RATE_CONTROL_TEST_BIN): rate_control_test.c \
	$(PROJECT_DIR)/src/t40/t41_rate_control.c \
	$(PROJECT_DIR)/src/t40/t41_rate_control.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		rate_control_test.c \
		$(PROJECT_DIR)/src/t40/t41_rate_control.c \
		-o "$@"

$(EP1_TEST_BIN): ep1_layout_test.c \
	$(PROJECT_DIR)/src/t40/t40_ep1.c \
	$(PROJECT_DIR)/src/t40/t40_ep1.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		ep1_layout_test.c \
		$(PROJECT_DIR)/src/t40/t40_ep1.c \
		-o "$@"

clean:
	$(RM) $(TEST_BINS)
