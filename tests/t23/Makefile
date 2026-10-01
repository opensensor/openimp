CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-function \
	-Wno-cast-function-type
PROJECT_DIR := ../..
ISP_TUNING_TEST_BIN := $(PROJECT_DIR)/build/tests/t23-isp-tuning-test
ACODEC_TEST_BIN := $(PROJECT_DIR)/build/tests/t23-acodec-test

.PHONY: check clean device

# The code under test passes pointers through 32-bit words, as on MIPS:
# build without PIE so the test's data lies below 4 GiB.
check: $(ISP_TUNING_TEST_BIN) $(ACODEC_TEST_BIN)
	$(ISP_TUNING_TEST_BIN)
	$(ACODEC_TEST_BIN)

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

clean:
	rm -f $(ISP_TUNING_TEST_BIN) $(ACODEC_TEST_BIN) $(DEVICE_TEST_BIN)

# On-device test (static MIPS binary); see isp_tuning_device_test.c.
CROSS_COMPILE ?= mipsel-linux-
DEVICE_TEST_BIN := $(PROJECT_DIR)/build/tests/t23_isp_tuning_device_test

device: $(DEVICE_TEST_BIN)

$(DEVICE_TEST_BIN): isp_tuning_device_test.c \
	$(PROJECT_DIR)/src/isp/isp_t23_tuning.c
	mkdir -p "$(dir $@)"
	$(CROSS_COMPILE)gcc -static -std=gnu99 -O2 -Wall -Wextra -Werror \
		-Wno-cast-function-type -DPLATFORM_T23 \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		isp_tuning_device_test.c -lpthread -o "$@"
	$(CROSS_COMPILE)strip "$@"
