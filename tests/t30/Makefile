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

.PHONY: check clean ivs-bench sanitize

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
	$(ROOT)/src/t21/t21_h264_descriptor.c $(ROOT)/src/t30/helix_bitstream.c \
	$(ROOT)/src/eprc/eprc.c

helix_encoder_test_t21: $(HELIX_T21_SOURCES) $(ROOT)/src/t30/t30_annexb.h $(ROOT)/src/t30/t30_h264_level.h
	$(CC) $(CFLAGS) -DPLATFORM_T21 -fno-pie -no-pie \
		-I$(ROOT)/include -I$(ROOT)/src $(INCLUDES) \
		-Wl,--wrap=open,--wrap=close,--wrap=ioctl \
		-o $@ $(HELIX_T21_SOURCES) -lm

# T21 reserved-memory layout: OpenIMP's arena, the real encoder and shared
# bitstream buffer through timps' idle/re-enable cycles (PC420 numbers).
T21_RMEM_SOURCES := t21_rmem_layout_test.c \
	$(filter-out helix_encoder_test.c,$(HELIX_T21_SOURCES))

t21_rmem_layout_test: $(T21_RMEM_SOURCES) $(ROOT)/src/rmem_arena.h \
	$(ROOT)/src/t30/helix_bitstream.h $(ROOT)/src/t30/t30_helix_encoder.h
	$(CC) $(CFLAGS) -DPLATFORM_T21 -fno-pie -no-pie \
		-I$(ROOT)/include -I$(ROOT)/src $(INCLUDES) \
		-Wl,--wrap=open,--wrap=close,--wrap=ioctl \
		-o $@ $(T21_RMEM_SOURCES) -pthread -lm

# T23 with the reference ring: ring, EMC and the shared bitstream area.
t23_ring_layout_test: $(T21_RMEM_SOURCES) $(ROOT)/src/rmem_arena.h \
	$(ROOT)/src/t21/t21_ref_ring.h $(ROOT)/src/t30/t30_helix_encoder.h
	$(CC) $(CFLAGS) -DPLATFORM_T23 -fno-pie -no-pie \
		-I$(ROOT)/include -I$(ROOT)/src $(INCLUDES) \
		-Wl,--wrap=open,--wrap=close,--wrap=ioctl \
		-o $@ $(T21_RMEM_SOURCES) -pthread -lm

t21_sinter_map_test: t21_sinter_map_test.c $(ROOT)/src/isp/isp_t21_sinter.h
	$(CC) $(CFLAGS) -I$(ROOT)/src -o $@ t21_sinter_map_test.c

# T20 builds the T21 userspace with PLATFORM_T20 and the T30 descriptor.
helix_encoder_test_t20: $(HELIX_SOURCES) $(ROOT)/src/t30/t30_annexb.h $(ROOT)/src/t30/t30_h264_level.h
	$(CC) $(CFLAGS) -DPLATFORM_T20 -DPLATFORM_T21 -fno-pie -no-pie \
		-I$(ROOT)/include -I$(ROOT)/src $(INCLUDES) \
		-Wl,--wrap=open,--wrap=close,--wrap=ioctl \
		-o $@ $(HELIX_SOURCES)

# Helix hardware JPEG against a fake /dev/soc_vpu that executes the command
# list (no PIE: 32-bit addresses).  Built per command-list generation and
# with and without the software JPEG fallback.
HJPEG_DEPS := helix_jpeg_test.c $(ROOT)/src/t30/helix_jpeg.c \
	$(ROOT)/src/t30/helix_jpeg.h
HJPEG_FLAGS = $(CFLAGS) -fno-pie -no-pie -I$(ROOT)/include -I$(ROOT)/src \
	-Wl,--wrap=open,--wrap=close,--wrap=ioctl

helix_jpeg_test_t21: $(HJPEG_DEPS) $(ROOT)/src/t30/helix_bitstream.c
	$(CC) $(HJPEG_FLAGS) -DPLATFORM_T21 -o $@ helix_jpeg_test.c \
		$(ROOT)/src/t30/helix_jpeg.c $(ROOT)/src/t30/helix_bitstream.c \
		-lm -pthread

helix_jpeg_test_t20: $(HJPEG_DEPS)
	$(CC) $(HJPEG_FLAGS) -DPLATFORM_T20 -DPLATFORM_T21 -DOPENIMP_SW_JPEG=0 \
		-o $@ helix_jpeg_test.c $(ROOT)/src/t30/helix_jpeg.c -lm -pthread

helix_jpeg_test_t23: $(HJPEG_DEPS)
	$(CC) $(HJPEG_FLAGS) -DPLATFORM_T23 -DOPENIMP_SW_JPEG=0 -o $@ \
		helix_jpeg_test.c $(ROOT)/src/t30/helix_jpeg.c -lm -pthread

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

# FrameSource / VBM lifecycle against a fake frame-channel driver
# (fs_lifecycle_test.c), as built for T21, T20 and T23.
FS_SOURCES := fs_lifecycle_test.c \
	$(ROOT)/src/framesource/framesource_tseries.c \
	$(ROOT)/src/kernel_interface.c
FS_DEPS := $(FS_SOURCES) $(ROOT)/src/kernel_interface.h \
	$(ROOT)/src/vbm_dq_step.h $(ROOT)/src/openimp_ready_event.h
FS_FLAGS = -Wno-unused-result '-D_Static_assert(c,m)=' -fno-pie -no-pie \
	-I$(ROOT)/include -I$(ROOT)/src \
	-Wl,--wrap=open,--wrap=close,--wrap=ioctl,--wrap=select
# alignment: the 32-bit module layout stores pointers at 4-byte offsets
FS_SANITIZE_ASAN := -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize=alignment \
	-fno-sanitize-recover=undefined
FS_SANITIZE_TSAN := -O1 -g -fsanitize=thread

fs_lifecycle_test_t21: $(FS_DEPS)
	$(CC) $(CFLAGS) $(FS_FLAGS) -DPLATFORM_T21 -o $@ $(FS_SOURCES) -pthread

fs_lifecycle_test_t20: $(FS_DEPS)
	$(CC) $(CFLAGS) $(FS_FLAGS) -DPLATFORM_T20 -DPLATFORM_T21 \
		-o $@ $(FS_SOURCES) -pthread

fs_lifecycle_test_t23: $(FS_DEPS)
	$(CC) $(CFLAGS) $(FS_FLAGS) -DPLATFORM_T23 -o $@ $(FS_SOURCES) -pthread

# IMP_System_Init/Exit and the timestamp base, as built for T21.
SYSTEM_SOURCES := system_test.c $(ROOT)/src/t40/openimp_p0.c
SYSTEM_FLAGS = -Wno-unused-result '-D_Static_assert(c,m)=' -DPLATFORM_T21 \
	-I$(ROOT)/include -I$(ROOT)/src

system_test: $(SYSTEM_SOURCES)
	$(CC) $(CFLAGS) $(SYSTEM_FLAGS) -o $@ $(SYSTEM_SOURCES) -pthread

# Host tests under the sanitizers: the FrameSource/VBM lifecycle for T21,
# T20 and T23 (ASan+UBSan, TSan), the Helix encoder for T30, T21 and T20
# (UBSan), the IVS framework for T21 and T20 (ASan+UBSan, TSan). Slowed
# down, the IVS thread can still be busy when the next frame comes and
# drops it, as on a loaded camera: a functional mismatch there (exit 1)
# is tolerated, a sanitizer report (other exit codes) is not.
SAN_CC = $(CC) -std=gnu11 -Wall -Wextra -Werror
# The stream buffer address travels in a 32-bit field, so the encoder's
# malloc must stay below 4 GiB: UBSan only, not ASan's high heap.
HELIX_SANITIZE := -O1 -g -fsanitize=undefined -fno-sanitize=alignment \
	-fno-sanitize-recover=undefined
TSAN_RUN = TSAN_OPTIONS="halt_on_error=1 suppressions=$(CURDIR)/fs_lifecycle_tsan.supp"

sanitize: $(FS_DEPS) $(HELIX_SOURCES) $(HELIX_T21_SOURCES) $(IVS_DEPS)
	set -e; for soc in "T21" "T20 -DPLATFORM_T21" "T23"; do \
		name=$$(echo $$soc | cut -d' ' -f1); \
		$(SAN_CC) $(FS_FLAGS) $(FS_SANITIZE_ASAN) -DPLATFORM_$$soc \
			-o fs_lifecycle_asan_$$name $(FS_SOURCES) -pthread; \
		./fs_lifecycle_asan_$$name; \
		$(SAN_CC) $(FS_FLAGS) $(FS_SANITIZE_TSAN) -DPLATFORM_$$soc \
			-o fs_lifecycle_tsan_$$name $(FS_SOURCES) -pthread; \
		$(TSAN_RUN) ./fs_lifecycle_tsan_$$name; \
	done
	$(SAN_CC) $(SYSTEM_FLAGS) $(FS_SANITIZE_TSAN) -o system_tsan \
		$(SYSTEM_SOURCES) -pthread
	TSAN_OPTIONS=halt_on_error=1 ./system_tsan
	set -e; for soc in "T30:$(HELIX_SOURCES)" \
		"T21:$(HELIX_T21_SOURCES)" \
		"T20 -DPLATFORM_T21:$(HELIX_SOURCES)"; do \
		defs=$${soc%%:*}; srcs=$${soc#*:}; name=$$(echo $$defs | cut -d' ' -f1); \
		$(SAN_CC) $(HELIX_SANITIZE) -DPLATFORM_$$defs -fno-pie -no-pie \
			-I$(ROOT)/include -I$(ROOT)/src $(INCLUDES) \
			-Wl,--wrap=open,--wrap=close,--wrap=ioctl \
			-o helix_encoder_asan_$$name $$srcs -lm; \
		./helix_encoder_asan_$$name; \
	done
	set -e; for soc in "T21" "T20 -DPLATFORM_T21"; do \
		name=$$(echo $$soc | cut -d' ' -f1); \
		$(SAN_CC) $(FS_SANITIZE_ASAN) -Wno-unused-function -DPLATFORM_$$soc \
			-fno-pie -no-pie -I$(ROOT)/include -I$(ROOT)/src -I$(IVS_SRC) \
			-o ivs_framework_asan_$$name $(IVS_SOURCES) -pthread; \
		ASAN_OPTIONS=exitcode=86 UBSAN_OPTIONS=exitcode=86 \
			./ivs_framework_asan_$$name || [ $$? -eq 1 ]; \
		$(SAN_CC) $(FS_SANITIZE_TSAN) -Wno-unused-function -DPLATFORM_$$soc \
			-fno-pie -no-pie -I$(ROOT)/include -I$(ROOT)/src -I$(IVS_SRC) \
			-o ivs_framework_tsan_$$name $(IVS_SOURCES) -pthread; \
		$(TSAN_RUN) ./ivs_framework_tsan_$$name || [ $$? -eq 1 ]; \
	done

# Shared reference ring (OPENIMP_REF_SHARE) layout and command list
ref_ring_test: ref_ring_test.c $(ROOT)/src/t21/t21_h264_descriptor.c \
	$(ROOT)/src/t21/t21_ref_ring.h
	$(CC) $(CFLAGS) -I$(ROOT)/src -o $@ ref_ring_test.c \
		$(ROOT)/src/t21/t21_h264_descriptor.c

ref_ring_test_t23: ref_ring_test.c $(ROOT)/src/t21/t21_h264_descriptor.c \
	$(ROOT)/src/t21/t21_ref_ring.h
	$(CC) $(CFLAGS) -DT21_HELIX_T23_DELTAS -I$(ROOT)/src -o $@ \
		ref_ring_test.c $(ROOT)/src/t21/t21_h264_descriptor.c

check: ref_ring_test ref_ring_test_t23 descriptor_test annexb_test level_test helix_encoder_test \
	helix_encoder_test_t21 helix_encoder_test_t20 t21_rmem_layout_test t23_ring_layout_test \
	t21_sinter_map_test helix_jpeg_test_t21 helix_jpeg_test_t20 helix_jpeg_test_t23 \
	ivs_framework_test_t21 ivs_framework_test_t20 ivs_move_bench_t20 \
	fs_lifecycle_test_t21 fs_lifecycle_test_t20 fs_lifecycle_test_t23 \
	system_test rc_readback_test_t21 rc_readback_test_t20 rc_readback_test_t23
	./ref_ring_test
	./ref_ring_test_t23
	./descriptor_test
	./annexb_test
	./level_test
	./helix_encoder_test
	./helix_encoder_test_t21
	./t21_rmem_layout_test
	./t23_ring_layout_test
	./t21_sinter_map_test
	./helix_encoder_test_t20
	./helix_jpeg_test_t21
	./helix_jpeg_test_t20
	./helix_jpeg_test_t23
	./ivs_framework_test_t21
	./ivs_framework_test_t20
	./ivs_move_bench_t20 equiv
	./fs_lifecycle_test_t21
	./fs_lifecycle_test_t20
	./fs_lifecycle_test_t23
	./system_test
	./rc_readback_test_t21
	./rc_readback_test_t20
	./rc_readback_test_t23

# GetChnAttrRcMode read-back of T21, T20 and T23 (src/t40/p2_rc_readback.h).
rc_readback_test_t21: rc_readback_test.c $(ROOT)/src/t40/p2_rc_readback.h
	$(CC) $(CFLAGS) -DPLATFORM_T21 '-D_Static_assert(c,m)=' -I$(ROOT)/include -I$(ROOT)/src \
		-o $@ rc_readback_test.c

rc_readback_test_t20: rc_readback_test.c $(ROOT)/src/t40/p2_rc_readback.h
	$(CC) $(CFLAGS) -DPLATFORM_T20 -DPLATFORM_T21 '-D_Static_assert(c,m)=' -I$(ROOT)/include \
		-I$(ROOT)/src -o $@ rc_readback_test.c

rc_readback_test_t23: rc_readback_test.c $(ROOT)/src/t40/p2_rc_readback.h
	$(CC) $(CFLAGS) -DPLATFORM_T23 '-D_Static_assert(c,m)=' -I$(ROOT)/include \
		-I$(ROOT)/src -o $@ rc_readback_test.c

clean:
	$(RM) descriptor_test annexb_test level_test helix_encoder_test \
		rc_readback_test_t21 rc_readback_test_t20 rc_readback_test_t23 \
		helix_encoder_test_t21 helix_encoder_test_t20 t21_rmem_layout_test t23_ring_layout_test \
		t21_sinter_map_test helix_jpeg_test_t21 helix_jpeg_test_t20 helix_jpeg_test_t23 \
		ivs_framework_test_t21 ivs_framework_test_t20 ivs_move_bench_t20 \
		fs_lifecycle_test_t21 fs_lifecycle_test_t20 fs_lifecycle_test_t23 \
		fs_lifecycle_asan_* fs_lifecycle_tsan_* helix_encoder_asan_* \
		ivs_framework_asan_* ivs_framework_tsan_* system_test system_tsan
