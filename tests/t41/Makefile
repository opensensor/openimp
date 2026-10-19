CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
COMMAND_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-command-layout-test
RATE_CONTROL_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-rate-control-test
EP1_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-ep1-layout-test
STREAM_LAYOUT_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-stream-layout-test
TUNING_AWB_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-tuning-awb-test
TUNING_POLICY_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-tuning-policy-test
TUNING_STARTUP_TEST_BIN := $(PROJECT_DIR)/build/tests/tuning-startup-test
JPEG_DCT_TEST_BIN := $(PROJECT_DIR)/build/tests/jpeg-dct-test
CORE_LEASE_TEST_BIN := $(PROJECT_DIR)/build/tests/core-lease-test
IVS_FRAMEWORK_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-ivs-framework-test
IVS_SRC := $(PROJECT_DIR)/src/t31
RMEM_FIT_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-rmem-fit-test
RMEM_FIT_T40_TEST_BIN := $(PROJECT_DIR)/build/tests/t40-rmem-first-fit-test
JPEG_QL_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-jpeg-ql-test
SNAP_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-snap-test
I2D_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-i2d-test
JPEG_PITCH_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-jpeg-pitch-test
JPEG_RANGE_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-jpeg-range-test
TEST_BINS := $(IVS_FRAMEWORK_TEST_BIN) $(CORE_LEASE_TEST_BIN) $(COMMAND_TEST_BIN) $(RATE_CONTROL_TEST_BIN) $(EP1_TEST_BIN) \
	$(STREAM_LAYOUT_TEST_BIN) $(TUNING_AWB_TEST_BIN) $(TUNING_STARTUP_TEST_BIN) $(TUNING_POLICY_TEST_BIN) $(JPEG_DCT_TEST_BIN) \
	$(RMEM_FIT_TEST_BIN) $(RMEM_FIT_T40_TEST_BIN) $(JPEG_QL_TEST_BIN) $(SNAP_TEST_BIN) $(I2D_TEST_BIN) $(JPEG_PITCH_TEST_BIN) $(JPEG_RANGE_TEST_BIN)

P3_CONTROLS_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-p3-controls-test

P2_BUFSHARE_TEST_BIN := $(PROJECT_DIR)/build/tests/t41-p2-bufshare-test
.PHONY: check clean p3-controls p3-ao-mute

check: $(TEST_BINS) $(P2_BUFSHARE_TEST_BIN)
	$(MAKE) p3-controls
	# CappedVBR/CappedQuality run the Allegro core (tests/t40 frame-rc)
	grep -q 't31_hevc_headers t31_al_rc' $(PROJECT_DIR)/build-t41.sh
	$(MAKE) p3-ao-mute
	$(RMEM_FIT_TEST_BIN)
	$(RMEM_FIT_T40_TEST_BIN)
	$(IVS_FRAMEWORK_TEST_BIN)
	$(CORE_LEASE_TEST_BIN)
	$(COMMAND_TEST_BIN)
	$(RATE_CONTROL_TEST_BIN)
	$(EP1_TEST_BIN)
	$(STREAM_LAYOUT_TEST_BIN)
	$(TUNING_AWB_TEST_BIN)
	$(TUNING_STARTUP_TEST_BIN)
	$(TUNING_POLICY_TEST_BIN)
	$(JPEG_DCT_TEST_BIN)
	$(JPEG_QL_TEST_BIN)
	$(SNAP_TEST_BIN)
	$(I2D_TEST_BIN)
	$(JPEG_PITCH_TEST_BIN)
	$(JPEG_RANGE_TEST_BIN)
	$(P2_BUFSHARE_TEST_BIN)

# IMP_Encoder_SetJpegeQl tables in the T41 software JPEG (the test source is
# shared with tests/t23; -no-pie keeps the 32-bit buffer addresses valid).
$(JPEG_QL_TEST_BIN): ../t23/jpeg_ql_test.c $(PROJECT_DIR)/src/hw_encoder.c \
	$(PROJECT_DIR)/src/hw_encoder.h $(PROJECT_DIR)/include/imp/imp_encoder.h
	mkdir -p "$(dir $@)"
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-function -DPLATFORM_T41 \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src ../t23/jpeg_ql_test.c \
		$(PROJECT_DIR)/src/hw_encoder.c -no-pie -lm -o "$@"

# IVS framework and move algorithm with the T41 1.2.0 ABI (IMPFrameInfo
# 0x30, timestamp at 0x28); the test source is shared with tests/t23.
$(IVS_FRAMEWORK_TEST_BIN): ../t23/ivs_framework_test.c $(IVS_SRC)/openimp_t31_ivs.c \
	$(IVS_SRC)/openimp_t31_ivs_move.c $(IVS_SRC)/openimp_ivs_move_v2.c $(IVS_SRC)/openimp_t31_ivs_abi.h
	mkdir -p "$(dir $@)"
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-function -DPLATFORM_T41 \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src -I$(IVS_SRC) \
		../t23/ivs_framework_test.c $(IVS_SRC)/openimp_t31_ivs.c \
		$(IVS_SRC)/openimp_t31_ivs_move.c $(IVS_SRC)/openimp_ivs_move_v2.c -pthread $(PROJECT_DIR)/src/core/imp_log_emit.c -o "$@"

$(CORE_LEASE_TEST_BIN): core_lease_test.c $(PROJECT_DIR)/src/t40/openimp_core_lease.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $< -pthread -o "$@"

# Software JPEG of an I2D output (line pitch wider than the width).
$(JPEG_PITCH_TEST_BIN): jpeg_pitch_test.c $(PROJECT_DIR)/src/hw_encoder.c \
	$(PROJECT_DIR)/src/hw_encoder.h
	mkdir -p "$(dir $@)"
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-function -DPLATFORM_T41 \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src jpeg_pitch_test.c \
		$(PROJECT_DIR)/src/hw_encoder.c -no-pie -lm -o "$@"

$(JPEG_RANGE_TEST_BIN): jpeg_range_test.c $(PROJECT_DIR)/src/hw_encoder.c \
	$(PROJECT_DIR)/src/hw_encoder.h
	mkdir -p "$(dir $@)"
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-function -DPLATFORM_T41 \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src jpeg_range_test.c \
		$(PROJECT_DIR)/src/hw_encoder.c -no-pie -lm -o "$@"

$(JPEG_DCT_TEST_BIN): jpeg_dct_test.c $(PROJECT_DIR)/src/jpeg_dct.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $< -lm -o "$@"

$(TUNING_POLICY_TEST_BIN): tuning_policy_test.c $(PROJECT_DIR)/src/openimp_tuning.c
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -DPLATFORM_T41 -I$(PROJECT_DIR)/include $< -pthread -o "$@"

$(TUNING_STARTUP_TEST_BIN): tuning_startup_test.c $(PROJECT_DIR)/tools/tuning_startup.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/include $< -o "$@"

$(TUNING_AWB_TEST_BIN): tuning_awb_test.c $(PROJECT_DIR)/src/openimp_tuning.c
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -DPLATFORM_T41 -I$(PROJECT_DIR)/include \
		tuning_awb_test.c -pthread -o "$@"

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

$(STREAM_LAYOUT_TEST_BIN): stream_layout_test.c \
	$(PROJECT_DIR)/src/t40/t41_stream_layout.c \
	$(PROJECT_DIR)/src/t40/t41_stream_layout.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		stream_layout_test.c \
		$(PROJECT_DIR)/src/t40/t41_stream_layout.c \
		-o "$@"

$(SNAP_TEST_BIN): snap_test.c $(PROJECT_DIR)/src/t40/t41_snap.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src $< -o "$@"

# FrameSource I2D (hardware rotate/flip/mirror): geometry, ioctl block,
# NV12 layout (shared with tests/t40).
$(I2D_TEST_BIN): i2d_test.c $(PROJECT_DIR)/src/t40/p1_i2d.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src $< -o "$@"

clean:
	$(RM) $(TEST_BINS)

# The P2 DMA ledger with the T41 split arena and with the T40 first fit.
$(RMEM_FIT_TEST_BIN): rmem_fit_test.c $(PROJECT_DIR)/src/t40/openimp_p2_dma.c
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -Wno-unused-function -D_GNU_SOURCE -DPLATFORM_T41 \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src $< -o "$@"

$(RMEM_FIT_T40_TEST_BIN): rmem_fit_test.c $(PROJECT_DIR)/src/t40/openimp_p2_dma.c
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -Wno-unused-function -D_GNU_SOURCE \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src $< -o "$@"

# ISP tuning pass-through (vendor control IDs and envelope).  Needs the
# vendor T41 1.2.0 headers; skipped when T41_HEADERS is not set.
p3-controls:
ifneq ($(T41_HEADERS),)
	mkdir -p "$(dir $(P3_CONTROLS_TEST_BIN))"
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -DPLATFORM_T41 -I$(T41_HEADERS) \
		p3_controls_test.c $(PROJECT_DIR)/src/t40/openimp_p3_controls.c \
		-o "$(P3_CONTROLS_TEST_BIN)"
	$(P3_CONTROLS_TEST_BIN)
else
	@echo "t41 p3 controls test skipped (set T41_HEADERS)"
endif

# IMP_Encoder_SetbufshareChn (vendor argument checks, return codes, stored
# value over CreateChn / DestroyChn); the test source is shared with tests/t31.
$(P2_BUFSHARE_TEST_BIN): ../t31/p2_bufshare_test.c \
	$(PROJECT_DIR)/src/t40/openimp_p2_encoder.c
	mkdir -p "$(dir $@)"
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -Werror -DPLATFORM_T41 \
		'-D_Static_assert(c,m)=' -Wno-unused-result \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		-I$(PROJECT_DIR)/src/t40 ../t31/p2_bufshare_test.c \
		$(PROJECT_DIR)/src/openimp_profile.c \
		$(PROJECT_DIR)/src/t40/t31_stream_layout.c \
		-no-pie -lpthread $(PROJECT_DIR)/src/core/imp_log_emit.c -o $@

# software-mute IMP_AO_SendFrame from two threads
p3-ao-mute: p3_ao_mute_test.c $(PROJECT_DIR)/src/t40/openimp_p3_audio.c
	mkdir -p "$(PROJECT_DIR)/build/tests"
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -Wno-unused-function -DPLATFORM_T41 \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		p3_ao_mute_test.c -pthread -ldl -o "$(PROJECT_DIR)/build/tests/t41-p3-ao-mute-test"
	$(PROJECT_DIR)/build/tests/t41-p3-ao-mute-test
