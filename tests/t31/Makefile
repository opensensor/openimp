CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
STREAM_LAYOUT_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-stream-layout-test
RATE_CONTROL_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-rate-control-test
READY_EVENT_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-ready-event-test
ACODEC_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-acodec-test
FRAME_PIN_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-frame-pin-test
AO_SENDFRAME_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-ao-sendframe-test
HEVC_HEADERS_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-hevc-headers-test
AEC_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-aec-test

.PHONY: check clean

check: $(STREAM_LAYOUT_TEST_BIN) $(RATE_CONTROL_TEST_BIN) \
	$(READY_EVENT_TEST_BIN) $(ACODEC_TEST_BIN) $(FRAME_PIN_TEST_BIN) \
	$(AO_SENDFRAME_TEST_BIN) $(HEVC_HEADERS_TEST_BIN) $(AEC_TEST_BIN)
	$(STREAM_LAYOUT_TEST_BIN)
	$(RATE_CONTROL_TEST_BIN)
	$(READY_EVENT_TEST_BIN)
	$(ACODEC_TEST_BIN)
	$(FRAME_PIN_TEST_BIN)
	$(AO_SENDFRAME_TEST_BIN)
	$(HEVC_HEADERS_TEST_BIN)
	$(AEC_TEST_BIN)

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

$(AEC_TEST_BIN): aec_test.c $(PROJECT_DIR)/src/audio/openimp_aec.c \
	$(PROJECT_DIR)/src/audio/openimp_aec.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src -I$(PROJECT_DIR)/src/audio \
		aec_test.c $(PROJECT_DIR)/src/audio/openimp_aec.c \
		-lpthread -lm -o "$@"

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

# IMP_AO_SendFrame against a fake OSS2 /dev/dsp (open/ioctl/close wrapped).
# The 32-bit driver-ABI asserts do not hold on the 64-bit host.
$(AO_SENDFRAME_TEST_BIN): ao_sendframe_test.c \
	$(PROJECT_DIR)/src/t31/openimp_t31_audio.c
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -std=gnu99 '-D_Static_assert(c,m)=' -Wno-unused-result \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		ao_sendframe_test.c $(PROJECT_DIR)/src/t31/openimp_t31_audio.c \
		-Wl,--wrap=open,--wrap=ioctl,--wrap=close -lpthread -ldl -lm -o "$@"

$(HEVC_HEADERS_TEST_BIN): hevc_headers_test.c \
	$(PROJECT_DIR)/src/t40/t31_hevc_headers.c \
	$(PROJECT_DIR)/src/t40/t31_hevc_headers.h \
	$(PROJECT_DIR)/src/t40/t31_stream_layout.c \
	$(PROJECT_DIR)/src/t40/t31_stream_layout.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		hevc_headers_test.c \
		$(PROJECT_DIR)/src/t40/t31_hevc_headers.c \
		$(PROJECT_DIR)/src/t40/t31_stream_layout.c \
		-o "$@"

clean:
	$(RM) $(STREAM_LAYOUT_TEST_BIN) $(RATE_CONTROL_TEST_BIN) \
		$(READY_EVENT_TEST_BIN) $(ACODEC_TEST_BIN) $(FRAME_PIN_TEST_BIN) \
		$(AO_SENDFRAME_TEST_BIN) $(HEVC_HEADERS_TEST_BIN) $(AEC_TEST_BIN)
