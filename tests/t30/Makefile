CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Werror

ROOT := ../..
INCLUDES := -I$(ROOT)/src/t30 -I$(ROOT)/src/t30/h264enc
SOURCES := \
	descriptor_test.c \
	$(ROOT)/src/t30/t30_h264_descriptor.c \
	$(ROOT)/src/t30/h264enc/cabac.c \
	$(ROOT)/src/t30/h264enc/common.c \
	$(ROOT)/src/t30/h264enc/set.c

.PHONY: check clean ivs-bench

descriptor_test: $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SOURCES)

annexb_test: annexb_test.c $(ROOT)/src/t30/t30_annexb.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ annexb_test.c

level_test: level_test.c $(ROOT)/src/t30/t30_h264_level.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ level_test.c

# Runs the real wrapper against fake /dev/soc_vpu and rmem.  Its ABI keeps
# addresses in 32-bit fields: low mappings, no PIE, no mmap-backed malloc.
HELIX_SOURCES := \
	helix_encoder_test.c \
	$(ROOT)/src/t30/t30_helix_encoder.c \
	$(ROOT)/src/t30/t30_h264_descriptor.c \
	$(ROOT)/src/t30/h264enc/cabac.c \
	$(ROOT)/src/t30/h264enc/common.c \
	$(ROOT)/src/t30/h264enc/set.c \
	$(ROOT)/src/t30/h264enc/slice.c \
	$(ROOT)/src/t40/t31_rate_control.c

helix_encoder_test: $(HELIX_SOURCES) $(ROOT)/src/t30/t30_annexb.h $(ROOT)/src/t30/t30_h264_level.h
	$(CC) $(CFLAGS) -DPLATFORM_T30 -fno-pie -no-pie \
		-I$(ROOT)/include -I$(ROOT)/src $(INCLUDES) \
		-Wl,--wrap=open,--wrap=close,--wrap=ioctl \
		-o $@ $(HELIX_SOURCES)

HELIX_T21_SOURCES := $(filter-out %/t30_h264_descriptor.c,$(HELIX_SOURCES)) \
	$(ROOT)/src/t21/t21_h264_descriptor.c

helix_encoder_test_t21: $(HELIX_T21_SOURCES) $(ROOT)/src/t30/t30_annexb.h $(ROOT)/src/t30/t30_h264_level.h
	$(CC) $(CFLAGS) -DPLATFORM_T21 -fno-pie -no-pie \
		-I$(ROOT)/include -I$(ROOT)/src $(INCLUDES) \
		-Wl,--wrap=open,--wrap=close,--wrap=ioctl \
		-o $@ $(HELIX_T21_SOURCES)

# T20 builds the T21 userspace with PLATFORM_T20 and the T30 descriptor.
helix_encoder_test_t20: $(HELIX_SOURCES) $(ROOT)/src/t30/t30_annexb.h $(ROOT)/src/t30/t30_h264_level.h
	$(CC) $(CFLAGS) -DPLATFORM_T20 -DPLATFORM_T21 -fno-pie -no-pie \
		-I$(ROOT)/include -I$(ROOT)/src $(INCLUDES) \
		-Wl,--wrap=open,--wrap=close,--wrap=ioctl \
		-o $@ $(HELIX_SOURCES)

# IVS framework and move algorithm with the legacy T20/T21/T30 ABI; the test
# source is shared with tests/t23. The frame record carries 32-bit
# addresses: no PIE.
IVS_SRC := $(ROOT)/src/t31
IVS_SOURCES := $(ROOT)/tests/t23/ivs_framework_test.c \
	$(IVS_SRC)/openimp_t31_ivs.c $(IVS_SRC)/openimp_t31_ivs_move.c
IVS_DEPS := $(IVS_SOURCES) $(IVS_SRC)/openimp_t31_ivs.h \
	$(IVS_SRC)/openimp_t31_ivs_abi.h $(IVS_SRC)/openimp_t31_ivs_move.h

ivs_framework_test_t21: $(IVS_DEPS)
	$(CC) $(CFLAGS) -Wno-unused-function -DPLATFORM_T21 -fno-pie -no-pie \
		-I$(ROOT)/include -I$(ROOT)/src -I$(IVS_SRC) \
		-o $@ $(IVS_SOURCES) -pthread

ivs_framework_test_t20: $(IVS_DEPS)
	$(CC) $(CFLAGS) -Wno-unused-function -DPLATFORM_T20 -DPLATFORM_T21 \
		-fno-pie -no-pie -I$(ROOT)/include -I$(ROOT)/src -I$(IVS_SRC) \
		-o $@ $(IVS_SOURCES) -pthread

# Move algorithm: bit-identity against the frozen reference copy
# (tests/t23/ivs_move_ref.c), legacy T20/T21/T30 parameter layout. The bench
# target times reference and current code at 1080p and 360p.
IVS_BENCH_SOURCES := $(ROOT)/tests/t23/ivs_move_bench.c \
	$(ROOT)/tests/t23/ivs_move_ref.c $(IVS_SRC)/openimp_t31_ivs_move.c

ivs_move_bench_t20: $(IVS_BENCH_SOURCES) $(IVS_SRC)/openimp_t31_ivs_move.h \
	$(IVS_SRC)/openimp_t31_ivs_abi.h
	$(CC) $(CFLAGS) -DPLATFORM_T20 -DPLATFORM_T21 -I$(IVS_SRC) \
		-o $@ $(IVS_BENCH_SOURCES)

ivs-bench: ivs_move_bench_t20
	./ivs_move_bench_t20 bench

check: descriptor_test annexb_test level_test helix_encoder_test \
	helix_encoder_test_t21 helix_encoder_test_t20 \
	ivs_framework_test_t21 ivs_framework_test_t20 ivs_move_bench_t20
	./descriptor_test
	./annexb_test
	./level_test
	./helix_encoder_test
	./helix_encoder_test_t21
	./helix_encoder_test_t20
	./ivs_framework_test_t21
	./ivs_framework_test_t20
	./ivs_move_bench_t20 equiv

clean:
	$(RM) descriptor_test annexb_test level_test helix_encoder_test \
		helix_encoder_test_t21 helix_encoder_test_t20 \
		ivs_framework_test_t21 ivs_framework_test_t20 ivs_move_bench_t20
