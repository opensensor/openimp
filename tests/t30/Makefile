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

check: descriptor_test annexb_test
	./descriptor_test
	./annexb_test

clean:
	$(RM) descriptor_test annexb_test
