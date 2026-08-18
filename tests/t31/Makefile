CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
STREAM_LAYOUT_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-stream-layout-test
RATE_CONTROL_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-rate-control-test

.PHONY: check clean

check: $(STREAM_LAYOUT_TEST_BIN) $(RATE_CONTROL_TEST_BIN)
	$(STREAM_LAYOUT_TEST_BIN)
	$(RATE_CONTROL_TEST_BIN)

$(STREAM_LAYOUT_TEST_BIN): stream_layout_test.c \
	$(PROJECT_DIR)/src/t40/t31_stream_layout.c \
	$(PROJECT_DIR)/src/t40/t31_stream_layout.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		stream_layout_test.c \
		$(PROJECT_DIR)/src/t40/t31_stream_layout.c \
		-o "$@"

$(RATE_CONTROL_TEST_BIN): rate_control_test.c \
	$(PROJECT_DIR)/src/t40/t31_rate_control.c \
	$(PROJECT_DIR)/src/t40/t31_rate_control.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		rate_control_test.c \
		$(PROJECT_DIR)/src/t40/t31_rate_control.c \
		-o "$@"

clean:
	$(RM) $(STREAM_LAYOUT_TEST_BIN) $(RATE_CONTROL_TEST_BIN)
