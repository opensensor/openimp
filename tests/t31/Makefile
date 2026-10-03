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
NV12_ROTATE_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-nv12-rotate-test
LAMBDA_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-lambda-test
AI_LIFECYCLE_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-ai-lifecycle-test
FAKE_AUDIO_PROCESS_DIR := $(PROJECT_DIR)/build/tests/fake-audio-process
FAKE_AUDIO_PROCESS := $(FAKE_AUDIO_PROCESS_DIR)/libaudioProcess.so
OSD_DRAW_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-osd-draw-test
P2_JPEG_SOURCE_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-p2-jpeg-source-test
P2_RC_MODE_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-p2-rc-mode-test
MASK_RGB2YUV_TEST_BIN := $(PROJECT_DIR)/build/tests/t31-mask-rgb2yuv-test

.PHONY: check clean sanitize p2-jpeg-source

check: $(STREAM_LAYOUT_TEST_BIN) $(RATE_CONTROL_TEST_BIN) \
	$(READY_EVENT_TEST_BIN) $(ACODEC_TEST_BIN) $(FRAME_PIN_TEST_BIN) \
	$(AO_SENDFRAME_TEST_BIN) $(HEVC_HEADERS_TEST_BIN) $(AEC_TEST_BIN) $(NV12_ROTATE_TEST_BIN) \
	$(LAMBDA_TEST_BIN) $(AI_LIFECYCLE_TEST_BIN) $(FAKE_AUDIO_PROCESS) $(OSD_DRAW_TEST_BIN) $(P2_JPEG_SOURCE_TEST_BIN) \
	$(P2_RC_MODE_TEST_BIN) $(MASK_RGB2YUV_TEST_BIN)
	$(STREAM_LAYOUT_TEST_BIN)
	$(RATE_CONTROL_TEST_BIN)
	$(READY_EVENT_TEST_BIN)
	$(ACODEC_TEST_BIN)
	$(FRAME_PIN_TEST_BIN)
	$(AO_SENDFRAME_TEST_BIN)
	$(HEVC_HEADERS_TEST_BIN)
	$(AEC_TEST_BIN)
	$(NV12_ROTATE_TEST_BIN)
	$(LAMBDA_TEST_BIN)
	LD_LIBRARY_PATH=$(abspath $(FAKE_AUDIO_PROCESS_DIR)) $(AI_LIFECYCLE_TEST_BIN)
	$(OSD_DRAW_TEST_BIN)
	$(P2_JPEG_SOURCE_TEST_BIN)
	$(P2_RC_MODE_TEST_BIN)
	$(MASK_RGB2YUV_TEST_BIN)

$(MASK_RGB2YUV_TEST_BIN): mask_rgb2yuv_test.c $(PROJECT_DIR)/src/isp/isp_mask_rgb2yuv.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src mask_rgb2yuv_test.c -o "$@"

$(OSD_DRAW_TEST_BIN): osd_draw_test.c $(PROJECT_DIR)/src/t31/openimp_t31_osd_draw.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src osd_draw_test.c -o "$@"

$(LAMBDA_TEST_BIN): lambda_test.c $(PROJECT_DIR)/src/t40/t40_lambda.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src lambda_test.c -o "$@"

$(STREAM_LAYOUT_TEST_BIN): stream_layout_test.c \
	$(PROJECT_DIR)/src/t40/t31_stream_layout.c \
	$(PROJECT_DIR)/src/t40/t31_stream_layout.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		stream_layout_test.c \
		$(PROJECT_DIR)/src/t40/t31_stream_layout.c \
		-o "$@"

$(NV12_ROTATE_TEST_BIN): nv12_rotate_test.c \
	$(PROJECT_DIR)/src/framesource/nv12_rotate.c \
	$(PROJECT_DIR)/src/framesource/nv12_rotate.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		nv12_rotate_test.c \
		$(PROJECT_DIR)/src/framesource/nv12_rotate.c \
		-o "$@"

$(RATE_CONTROL_TEST_BIN): rate_control_test.c \
	$(PROJECT_DIR)/src/t40/t31_rate_control.c \
	$(PROJECT_DIR)/src/t40/t31_rate_control.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		rate_control_test.c \
		$(PROJECT_DIR)/src/t40/t31_rate_control.c \
		-o "$@" -lm

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
	$(PROJECT_DIR)/src/t31/openimp_t31_audio.c \
	$(PROJECT_DIR)/src/audio/openimp_aec.c
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -std=gnu99 '-D_Static_assert(c,m)=' -Wno-unused-result \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		-I$(PROJECT_DIR)/src/audio \
		ao_sendframe_test.c $(PROJECT_DIR)/src/t31/openimp_t31_audio.c \
		$(PROJECT_DIR)/src/audio/openimp_aec.c \
		-Wl,--wrap=open,--wrap=ioctl,--wrap=close -lpthread -ldl -lm -o "$@"

# AI/AO device lifecycle (same fake /dev/dsp approach) with a fake
# libaudioProcess.so for the HPF/NS/AGC effects.
AI_LIFECYCLE_SOURCES := ai_lifecycle_test.c \
	$(PROJECT_DIR)/src/t31/openimp_t31_audio.c \
	$(PROJECT_DIR)/src/audio/openimp_aec.c
AI_LIFECYCLE_FLAGS := -std=gnu99 '-D_Static_assert(c,m)=' -Wno-unused-result \
	-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
	-I$(PROJECT_DIR)/src/audio \
	-Wl,--wrap=open,--wrap=ioctl,--wrap=close

$(FAKE_AUDIO_PROCESS): fake_audio_process.c
	mkdir -p "$(dir $@)"
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -shared -fPIC \
		fake_audio_process.c -o "$@"

$(AI_LIFECYCLE_TEST_BIN): $(AI_LIFECYCLE_SOURCES)
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(AI_LIFECYCLE_FLAGS) $(AI_LIFECYCLE_SOURCES) \
		-lpthread -ldl -lm -o "$@"

# The AI lifecycle under ASan+UBSan and TSan.
sanitize: $(AI_LIFECYCLE_SOURCES) $(FAKE_AUDIO_PROCESS)
	$(CC) -O1 -g -Wall -Wextra -Werror $(AI_LIFECYCLE_FLAGS) \
		-fsanitize=address,undefined -fno-sanitize-recover=undefined \
		$(AI_LIFECYCLE_SOURCES) -lpthread -ldl -lm \
		-o $(AI_LIFECYCLE_TEST_BIN)-asan
	LD_LIBRARY_PATH=$(abspath $(FAKE_AUDIO_PROCESS_DIR)) \
		$(AI_LIFECYCLE_TEST_BIN)-asan
	$(CC) -O1 -g -Wall -Wextra -Werror $(AI_LIFECYCLE_FLAGS) \
		-fsanitize=thread $(AI_LIFECYCLE_SOURCES) -lpthread -ldl -lm \
		-o $(AI_LIFECYCLE_TEST_BIN)-tsan
	LD_LIBRARY_PATH=$(abspath $(FAKE_AUDIO_PROCESS_DIR)) \
		TSAN_OPTIONS=halt_on_error=1 $(AI_LIFECYCLE_TEST_BIN)-tsan

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
		$(AO_SENDFRAME_TEST_BIN) $(HEVC_HEADERS_TEST_BIN) $(AEC_TEST_BIN) $(NV12_ROTATE_TEST_BIN) \
		$(LAMBDA_TEST_BIN) $(AI_LIFECYCLE_TEST_BIN) $(MASK_RGB2YUV_TEST_BIN) \
		$(AI_LIFECYCLE_TEST_BIN)-asan $(AI_LIFECYCLE_TEST_BIN)-tsan \
		$(FAKE_AUDIO_PROCESS) \
		$(P2_JPEG_SOURCE_TEST_BIN) $(P2_RC_MODE_TEST_BIN)

# A JPEG channel that is the only one on its framesource (timps' jpeg.*
# channel) gets frames from PollingStream (p2_jpeg_frames_from_fanout), and
# one next to a receiving video channel still gets the fan-out; a skipped
# JPEG is replaced by the last one.  -no-pie: the 32-bit stream addresses of
# the p2 ABI must hold the test's (static and small heap) pointers.
$(P2_JPEG_SOURCE_TEST_BIN): p2_jpeg_source_test.c \
	$(PROJECT_DIR)/src/t40/openimp_p2_encoder.c
	mkdir -p "$(dir $@)"
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -Werror -DPLATFORM_T31 \
		'-D_Static_assert(c,m)=' -Wno-unused-result \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		-I$(PROJECT_DIR)/src/t40 p2_jpeg_source_test.c \
		$(PROJECT_DIR)/src/t40/openimp_p2_encoder.c \
		$(PROJECT_DIR)/src/openimp_profile.c \
		$(PROJECT_DIR)/src/t40/t31_stream_layout.c \
		-no-pie -lpthread -o $@

p2-jpeg-source: $(P2_JPEG_SOURCE_TEST_BIN)
	$(P2_JPEG_SOURCE_TEST_BIN)

# CappedVBR/CappedQuality reach the codec's PSNR cap, read back as given.
$(P2_RC_MODE_TEST_BIN): p2_rc_mode_test.c \
	$(PROJECT_DIR)/src/t40/openimp_p2_encoder.c
	mkdir -p "$(dir $@)"
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -Werror -DPLATFORM_T31 \
		'-D_Static_assert(c,m)=' -Wno-unused-result \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		-I$(PROJECT_DIR)/src/t40 p2_rc_mode_test.c \
		$(PROJECT_DIR)/src/t40/openimp_p2_encoder.c \
		$(PROJECT_DIR)/src/openimp_profile.c \
		$(PROJECT_DIR)/src/t40/t31_stream_layout.c \
		-no-pie -lpthread -o $@
