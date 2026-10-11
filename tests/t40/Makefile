CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
STREAM_LAYOUT_TEST_BIN := $(PROJECT_DIR)/build/tests/t40-stream-layout-test

DENOISE_TEST_BIN := $(PROJECT_DIR)/build/tests/t40-denoise-test

.PHONY: check clean abi-layout

CAMERA_INPUT_TEST_BIN := $(PROJECT_DIR)/build/tests/t40-camera-input-test

IVS_SRC := $(PROJECT_DIR)/src/t31
IVS_FRAMEWORK_TEST_BIN := $(PROJECT_DIR)/build/tests/t40-ivs-framework-test

check: $(STREAM_LAYOUT_TEST_BIN) $(DENOISE_TEST_BIN) $(CAMERA_INPUT_TEST_BIN) $(IVS_FRAMEWORK_TEST_BIN) p3-controls abi-layout frame-ts frame-rc vendor-cmd
	$(STREAM_LAYOUT_TEST_BIN)
	$(DENOISE_TEST_BIN)
	$(CAMERA_INPUT_TEST_BIN)
	$(IVS_FRAMEWORK_TEST_BIN)

# IVS framework and move algorithm with the T40 1.3.1 ABI (IMPFrameInfo 0x30,
# pool at 0x20, timestamp at 0x28); the test source is shared with tests/t23.
$(IVS_FRAMEWORK_TEST_BIN): ../t23/ivs_framework_test.c $(IVS_SRC)/openimp_t31_ivs.c \
	$(IVS_SRC)/openimp_t31_ivs_move.c $(IVS_SRC)/openimp_ivs_move_v2.c $(IVS_SRC)/openimp_t31_ivs_abi.h
	mkdir -p "$(dir $@)"
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-function -DPLATFORM_T40 \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src -I$(IVS_SRC) \
		../t23/ivs_framework_test.c $(IVS_SRC)/openimp_t31_ivs.c \
		$(IVS_SRC)/openimp_t31_ivs_move.c $(IVS_SRC)/openimp_ivs_move_v2.c -pthread -o "$@"

.PHONY: p3-controls
P3_CONTROLS_TEST_BIN := $(PROJECT_DIR)/build/tests/t40-p3-controls-test

# ISP tuning controls with the vendor T40 control numbers (request envelope,
# pass-through, CCM conversion).  Needs the vendor T40 1.3.1 headers;
# skipped when T40_HEADERS is not set.
p3-controls:
ifneq ($(T40_HEADERS),)
	mkdir -p "$(dir $(P3_CONTROLS_TEST_BIN))"
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -DPLATFORM_T40 -I$(T40_HEADERS) \
		p3_controls_test.c $(PROJECT_DIR)/src/t40/openimp_p3_controls.c \
		-o "$(P3_CONTROLS_TEST_BIN)"
	$(P3_CONTROLS_TEST_BIN)
else
	@echo "t40 p3 controls test skipped (set T40_HEADERS)"
endif

# host-only: private ioctl fixture, no camera
$(CAMERA_INPUT_TEST_BIN): camera_input_test.c $(PROJECT_DIR)/src/t40/openimp_p1.c
	mkdir -p "$(dir $@)"
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -DPLATFORM_T40 \
		-I$(PROJECT_DIR)/src -I$(PROJECT_DIR)/include \
		camera_input_test.c -o "$@"

$(STREAM_LAYOUT_TEST_BIN): stream_layout_test.c \
	$(PROJECT_DIR)/src/t40/t40_stream_layout.c \
	$(PROJECT_DIR)/src/t40/t40_stream_layout.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src \
		stream_layout_test.c \
		$(PROJECT_DIR)/src/t40/t40_stream_layout.c \
		-o "$@"

$(DENOISE_TEST_BIN): denoise_test.c $(PROJECT_DIR)/src/t40/p2_denoise.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src denoise_test.c -o "$@"

clean:
	$(RM) $(STREAM_LAYOUT_TEST_BIN) $(DENOISE_TEST_BIN) $(CAMERA_INPUT_TEST_BIN) $(P3_CONTROLS_TEST_BIN) $(IVS_FRAMEWORK_TEST_BIN)

# Struct sizes and offsets against the vendor T40 1.3.1 headers and OpenIMP's
# own T40 ABI headers.  Compile-only with a 32-bit MIPS compiler; skipped when
# T40_CC or T40_HEADERS is missing.
T40_CC ?= mipsel-linux-gcc
abi-layout:
ifeq ($(shell command -v $(T40_CC) 2>/dev/null),)
	@echo "t40 abi layout test skipped ($(T40_CC) not found; set T40_CC to a 32-bit MIPS compiler)"
else
	mkdir -p "$(PROJECT_DIR)/build/tests"
	$(T40_CC) -std=gnu99 -Wall -Wextra -Werror -DPLATFORM_T40 -I$(PROJECT_DIR)/src \
		-c abi_internal_test.c -o "$(PROJECT_DIR)/build/tests/t40-abi-internal.o"
ifneq ($(T40_HEADERS),)
	$(T40_CC) -std=gnu99 -Wall -Wextra -Werror -I$(T40_HEADERS) \
		-c abi_layout_test.c -o "$(PROJECT_DIR)/build/tests/t40-abi-layout.o"
	@echo "t40 abi layout: vendor headers and OpenIMP headers agree"
else
	@echo "t40 abi layout vs vendor headers skipped (set T40_HEADERS)"
endif
endif

.PHONY: frame-ts
frame-ts:
	mkdir -p $(PROJECT_DIR)/build/tests
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -DPLATFORM_T40 -I$(PROJECT_DIR)/src \
		codec_frame_ts_test.c -o $(PROJECT_DIR)/build/tests/t40-frame-ts-test
	$(PROJECT_DIR)/build/tests/t40-frame-ts-test

# Frame-level rate control: the T31 Allegro core driven as codec-t40.c
# drives it (AU size feedback, no status registers); the T40 build must
# link it, or the library silently ships with unresolved RC symbols.
.PHONY: vendor-cmd
vendor-cmd:
	mkdir -p $(PROJECT_DIR)/build/tests
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -DPLATFORM_T40 -I$(PROJECT_DIR)/src \
		vendor_cmd_test.c $(PROJECT_DIR)/src/t40/t40_ep1.c \
		-o $(PROJECT_DIR)/build/tests/t40-vendor-cmd-test
	$(PROJECT_DIR)/build/tests/t40-vendor-cmd-test

.PHONY: frame-rc
frame-rc:
	mkdir -p $(PROJECT_DIR)/build/tests
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -DPLATFORM_T40 -I$(PROJECT_DIR)/src \
		frame_rc_test.c $(PROJECT_DIR)/src/t40/t31_al_rc.c -lm \
		-o $(PROJECT_DIR)/build/tests/t40-frame-rc-test
	$(PROJECT_DIR)/build/tests/t40-frame-rc-test
	grep -q 't31_rate_control t31_al_rc' $(PROJECT_DIR)/build-t40.sh
