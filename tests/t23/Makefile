CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-function \
	-Wno-cast-function-type
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests
ISP_TUNING_TEST_BIN := $(BUILD)/t23-isp-tuning-test
ACODEC_TEST_BIN := $(BUILD)/t23-acodec-test
ZERO_COPY_TEST_BIN := $(BUILD)/t23-helix-zero-copy-test
FAKE_HELIXD_BIN := $(BUILD)/t23-fake-helixd
IVS_SRC := $(PROJECT_DIR)/src/t31
IVS_DEPS := $(IVS_SRC)/openimp_t31_ivs_move.c $(IVS_SRC)/openimp_t31_ivs_move.h \
	$(IVS_SRC)/openimp_t31_ivs_abi.h
IVS_DIGEST_T31 := $(BUILD)/t23-ivs-digest-t31abi
IVS_DIGEST_T23 := $(BUILD)/t23-ivs-digest-t23abi
IVS_FRAMEWORK := $(BUILD)/t23-ivs-framework-test
JPEG_QL := $(BUILD)/t23-jpeg-ql-test
HELIX_DESCRIPTOR := $(BUILD)/t23-helix-descriptor-test
HELIX_STREAM := $(BUILD)/t23-helix-stream-test
RMEM_LAYOUT := $(BUILD)/t23-rmem-layout-test
HELIX_NATIVE_SOURCES := $(PROJECT_DIR)/src/t30/t30_helix_encoder.c \
	$(PROJECT_DIR)/src/t21/t21_h264_descriptor.c \
	$(PROJECT_DIR)/src/t30/h264enc/cabac.c \
	$(PROJECT_DIR)/src/t30/h264enc/common.c \
	$(PROJECT_DIR)/src/t30/h264enc/set.c \
	$(PROJECT_DIR)/src/t30/h264enc/slice.c \
	$(PROJECT_DIR)/src/t40/t31_rate_control.c
# The IMP headers pin the 32-bit MIPS ABI with _Static_assert; the Helix
# bridge host build (x86_64, both ends of the IPC built alike) only checks
# the bridge's input path, so those layout checks are compiled out there.
HELIX_INCLUDES := -DPLATFORM_T23 '-D_Static_assert(c,m)=' -Wno-unused-result \
	-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src

.PHONY: check clean device t23tune

# The code under test passes pointers through 32-bit words, as on MIPS:
# build without PIE so the test's data lies below 4 GiB.
check: $(ISP_TUNING_TEST_BIN) $(ACODEC_TEST_BIN) $(ZERO_COPY_TEST_BIN) \
	$(FAKE_HELIXD_BIN) $(IVS_DIGEST_T31) $(IVS_DIGEST_T23) \
	$(IVS_FRAMEWORK) $(JPEG_QL) $(HELIX_DESCRIPTOR) $(HELIX_STREAM) \
	$(RMEM_LAYOUT)
	$(HELIX_DESCRIPTOR)
	$(RMEM_LAYOUT)
	$(HELIX_STREAM) encode $(BUILD)/t23-helix.h264 $(BUILD)/t23-helix.ref
	@if command -v ffmpeg >/dev/null 2>&1; then \
		ffmpeg -v error -err_detect explode -y \
			-i $(BUILD)/t23-helix.h264 -fps_mode passthrough -f rawvideo \
			-pix_fmt nv12 $(BUILD)/t23-helix.nv12 && \
		$(HELIX_STREAM) verify $(BUILD)/t23-helix.nv12 \
			$(BUILD)/t23-helix.ref; \
	else \
		echo "ffmpeg not found: T23 Helix stream not decoded"; \
	fi
	$(ISP_TUNING_TEST_BIN)
	$(ACODEC_TEST_BIN)
	$(ZERO_COPY_TEST_BIN) zero-copy "$(abspath $(FAKE_HELIXD_BIN))"
	$(ZERO_COPY_TEST_BIN) copy "$(abspath $(FAKE_HELIXD_BIN))"
	$(IVS_DIGEST_T31) > $(BUILD)/t23-ivs-digest-t31abi.txt
	$(IVS_DIGEST_T23) > $(BUILD)/t23-ivs-digest-t23abi.txt
	cat $(BUILD)/t23-ivs-digest-t23abi.txt
	cmp $(BUILD)/t23-ivs-digest-t31abi.txt $(BUILD)/t23-ivs-digest-t23abi.txt
	# pinned: the result digest of the move / base move code before the
	# claude/ivs-opt speed-ups; any change in what is detected shows here
	grep -q "digest ca3d1021f2a30c82" $(BUILD)/t23-ivs-digest-t31abi.txt
	$(IVS_FRAMEWORK)
	$(JPEG_QL)

$(ISP_TUNING_TEST_BIN): isp_tuning_test.c \
	$(PROJECT_DIR)/src/isp/isp_t23_tuning.c
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -fno-pie -no-pie -I$(PROJECT_DIR)/include \
		-I$(PROJECT_DIR)/src isp_tuning_test.c -lpthread -o "$@"

# The shared AENC/ADEC (src/audio) with the T23 behaviour: G.726 at
# 32 kbit/s, wall-clock stream time stamps. Same test source as tests/t31.
$(ACODEC_TEST_BIN): ../t31/acodec_test.c \
	$(PROJECT_DIR)/src/audio/openimp_audio_enc_dec.c \
	$(PROJECT_DIR)/src/audio/openimp_audio_codec.c \
	$(PROJECT_DIR)/src/audio/openimp_audio_codec.h \
	$(PROJECT_DIR)/include/imp/imp_audio.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -DPLATFORM_T23 -I$(PROJECT_DIR)/include \
		-I$(PROJECT_DIR)/src ../t31/acodec_test.c \
		$(PROJECT_DIR)/src/audio/openimp_audio_enc_dec.c \
		$(PROJECT_DIR)/src/audio/openimp_audio_codec.c \
		-lpthread -lm -o "$@"

$(ZERO_COPY_TEST_BIN): helix_zero_copy_test.c \
	$(PROJECT_DIR)/src/t23/openimp_t23_helix_bridge.c \
	$(PROJECT_DIR)/src/t23/openimp_t23_helix_bridge.h \
	$(PROJECT_DIR)/src/t23/openimp_t23_helix_ipc.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(HELIX_INCLUDES) helix_zero_copy_test.c \
		$(PROJECT_DIR)/src/t23/openimp_t23_helix_bridge.c -o "$@" -lpthread

$(FAKE_HELIXD_BIN): fake_helixd.c $(PROJECT_DIR)/src/t23/openimp_t23_helix_ipc.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(HELIX_INCLUDES) fake_helixd.c -o "$@"

# IVS with the T23 ABI: the T23 move / base move results must equal the
# T31 ones bit for bit (same vendor scalar code, only the parameter layout
# differs), and the T23 IVS framework must deliver them through the T23
# capture record and ABI.
$(IVS_DIGEST_T31): ivs_move_digest.c $(IVS_DEPS)
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(IVS_SRC) ivs_move_digest.c \
		$(IVS_SRC)/openimp_t31_ivs_move.c -o "$@"

$(IVS_DIGEST_T23): ivs_move_digest.c $(IVS_DEPS)
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -DPLATFORM_T23 -I$(IVS_SRC) ivs_move_digest.c \
		$(IVS_SRC)/openimp_t31_ivs_move.c -o "$@"

$(IVS_FRAMEWORK): ivs_framework_test.c $(IVS_SRC)/openimp_t31_ivs.c \
	$(IVS_SRC)/openimp_t31_ivs.h $(IVS_DEPS)
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -DPLATFORM_T23 -I$(PROJECT_DIR)/include \
		-I$(PROJECT_DIR)/src -I$(IVS_SRC) ivs_framework_test.c \
		$(IVS_SRC)/openimp_t31_ivs.c $(IVS_SRC)/openimp_t31_ivs_move.c \
		-pthread -o "$@"

# IMP_Encoder_SetJpegeQl tables in the software JPEG encoder. hw_encoder.c
# carries helpers other platforms use (-Wno-unused-function is in CFLAGS);
# -no-pie keeps the heap below 4 GiB for its 32-bit buffer addresses.
$(JPEG_QL): jpeg_ql_test.c $(PROJECT_DIR)/src/hw_encoder.c \
	$(PROJECT_DIR)/src/hw_encoder.h $(PROJECT_DIR)/src/jpeg_dct.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -Wno-unused-function -DPLATFORM_T23 \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src jpeg_ql_test.c \
		$(PROJECT_DIR)/src/hw_encoder.c -no-pie -o "$@"

# Native Helix H.264 (T21-family command list with the T23 deltas): the
# command list against the OEM T23 register order, and the whole encoder
# against a fake /dev/soc_vpu whose CABAC output a stock decoder checks.
$(HELIX_DESCRIPTOR): helix_descriptor_test.c \
	$(PROJECT_DIR)/src/t21/t21_h264_descriptor.c \
	$(PROJECT_DIR)/src/t21/t21_h264_descriptor.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -DPLATFORM_T23 -I$(PROJECT_DIR)/src \
		helix_descriptor_test.c \
		$(PROJECT_DIR)/src/t21/t21_h264_descriptor.c -o "$@"

$(HELIX_STREAM): helix_stream_test.c $(HELIX_NATIVE_SOURCES) \
	$(PROJECT_DIR)/src/t30/t30_helix_encoder.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(HELIX_INCLUDES) -U_FORTIFY_SOURCE \
		-D_FORTIFY_SOURCE=0 -Wno-missing-field-initializers \
		helix_stream_test.c $(HELIX_NATIVE_SOURCES) -no-pie \
		-Wl,--wrap=open,--wrap=close,--wrap=ioctl -o "$@"

# Reserved-memory layout with the native encoder: OpenIMP's arena and the
# real encoder buffer sizes through timps' idle/re-enable cycles.
$(RMEM_LAYOUT): rmem_layout_test.c $(HELIX_NATIVE_SOURCES) \
	$(PROJECT_DIR)/src/rmem_arena.h $(PROJECT_DIR)/src/t30/t30_helix_encoder.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(HELIX_INCLUDES) -U_FORTIFY_SOURCE \
		-D_FORTIFY_SOURCE=0 -Wno-missing-field-initializers \
		rmem_layout_test.c $(HELIX_NATIVE_SOURCES) -no-pie \
		-Wl,--wrap=open,--wrap=close,--wrap=ioctl -o "$@"

clean:
	rm -f $(HELIX_DESCRIPTOR) $(HELIX_STREAM) $(RMEM_LAYOUT) $(BUILD)/t23-helix.h264 \
		$(BUILD)/t23-helix.ref $(BUILD)/t23-helix.nv12
	rm -f $(ISP_TUNING_TEST_BIN) $(ACODEC_TEST_BIN) $(DEVICE_TEST_BIN) \
		$(ZERO_COPY_TEST_BIN) $(FAKE_HELIXD_BIN) $(IVS_DIGEST_T31) \
		$(IVS_DIGEST_T23) $(IVS_FRAMEWORK) $(JPEG_QL) \
		$(BUILD)/t23-ivs-digest-t31abi.txt $(BUILD)/t23-ivs-digest-t23abi.txt

# On-device test (static MIPS binary); see isp_tuning_device_test.c.
CROSS_COMPILE ?= mipsel-linux-
DEVICE_TEST_BIN := $(BUILD)/t23_isp_tuning_device_test

device: $(DEVICE_TEST_BIN)

$(DEVICE_TEST_BIN): isp_tuning_device_test.c \
	$(PROJECT_DIR)/src/isp/isp_t23_tuning.c
	mkdir -p "$(dir $@)"
	$(CROSS_COMPILE)gcc -static -std=gnu99 -O2 -Wall -Wextra -Werror \
		-Wno-cast-function-type -DPLATFORM_T23 \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		isp_tuning_device_test.c -lpthread -o "$@"
	$(CROSS_COMPILE)strip "$@"

# t23tune: on-camera CLI test tool for the new T23 tuning CIDs; see
# t23tune.c.  Same build recipe as the device test.
T23TUNE_BIN := $(BUILD)/t23tune

t23tune: $(T23TUNE_BIN)

$(T23TUNE_BIN): t23tune.c $(PROJECT_DIR)/src/isp/isp_t23_tuning.c
	mkdir -p "$(dir $@)"
	$(CROSS_COMPILE)gcc -static -std=gnu99 -O2 -Wall -Wextra -Werror \
		-Wno-cast-function-type -DPLATFORM_T23 \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		t23tune.c -lpthread -o "$@"
	$(CROSS_COMPILE)strip "$@"
