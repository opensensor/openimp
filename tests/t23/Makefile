CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-function \
	-Wno-cast-function-type
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests
ISP_TUNING_TEST_BIN := $(BUILD)/t23-isp-tuning-test
ACODEC_TEST_BIN := $(BUILD)/t23-acodec-test
ZERO_COPY_TEST_BIN := $(BUILD)/t23-helix-zero-copy-test
FAKE_HELIXD_BIN := $(BUILD)/t23-fake-helixd
# The IMP headers pin the 32-bit MIPS ABI with _Static_assert; the Helix
# bridge host build (x86_64, both ends of the IPC built alike) only checks
# the bridge's input path, so those layout checks are compiled out there.
HELIX_INCLUDES := -DPLATFORM_T23 '-D_Static_assert(c,m)=' -Wno-unused-result \
	-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src

.PHONY: check clean device

# The code under test passes pointers through 32-bit words, as on MIPS:
# build without PIE so the test's data lies below 4 GiB.
check: $(ISP_TUNING_TEST_BIN) $(ACODEC_TEST_BIN) $(ZERO_COPY_TEST_BIN) \
	$(FAKE_HELIXD_BIN)
	$(ISP_TUNING_TEST_BIN)
	$(ACODEC_TEST_BIN)
	$(ZERO_COPY_TEST_BIN) zero-copy "$(abspath $(FAKE_HELIXD_BIN))"
	$(ZERO_COPY_TEST_BIN) copy "$(abspath $(FAKE_HELIXD_BIN))"

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

clean:
	rm -f $(ISP_TUNING_TEST_BIN) $(ACODEC_TEST_BIN) $(DEVICE_TEST_BIN) \
		$(ZERO_COPY_TEST_BIN) $(FAKE_HELIXD_BIN)

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
