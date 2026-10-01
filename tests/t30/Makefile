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

.PHONY: check clean

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

check: descriptor_test annexb_test level_test helix_encoder_test
	./descriptor_test
	./annexb_test
	./level_test
	./helix_encoder_test

clean:
	$(RM) descriptor_test annexb_test level_test helix_encoder_test
