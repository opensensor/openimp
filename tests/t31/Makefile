CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
STREAM_LAYOUT_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-stream-layout-test
RATE_CONTROL_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-rate-control-test
READY_EVENT_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-ready-event-test
ACODEC_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-acodec-test
FRAME_PIN_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-frame-pin-test

.PHONY: check clean

check: $(STREAM_LAYOUT_TEST_BIN) $(RATE_CONTROL_TEST_BIN) \
	$(READY_EVENT_TEST_BIN) $(ACODEC_TEST_BIN) $(FRAME_PIN_TEST_BIN)
	$(STREAM_LAYOUT_TEST_BIN)
	$(RATE_CONTROL_TEST_BIN)
	$(READY_EVENT_TEST_BIN)
	$(ACODEC_TEST_BIN)
	$(FRAME_PIN_TEST_BIN)

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

$(READY_EVENT_TEST_BIN): ready_event_test.c \
	$(PROJECT_DIR)/src/openimp_ready_event.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		ready_event_test.c -o "$@" -lpthread

$(ACODEC_TEST_BIN): acodec_test.c \
	$(PROJECT_DIR)/src/audio/openimp_audio_enc_dec.c \
	$(PROJECT_DIR)/src/audio/openimp_audio_codec.c \
	$(PROJECT_DIR)/src/audio/openimp_audio_codec.h \
	$(PROJECT_DIR)/include/imp/imp_audio.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -std=gnu99 -I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		acodec_test.c \
		$(PROJECT_DIR)/src/audio/openimp_audio_enc_dec.c \
		$(PROJECT_DIR)/src/audio/openimp_audio_codec.c \
		-lpthread -lm -o "$@"

$(FRAME_PIN_TEST_BIN): frame_pin_test.c \
	$(PROJECT_DIR)/src/t40/p2_frame_pin.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		frame_pin_test.c -o "$@" -lpthread

clean:
	$(RM) $(STREAM_LAYOUT_TEST_BIN) $(RATE_CONTROL_TEST_BIN) \
		$(READY_EVENT_TEST_BIN) $(ACODEC_TEST_BIN) $(FRAME_PIN_TEST_BIN)
