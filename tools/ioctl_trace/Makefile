# Build libioctl_trace.so (LD_PRELOAD shim) for tracing /dev/avpu ioctls
# Usage:
#   make            # native build
#   make clean
#   CROSS_COMPILE=mipsel-linux-gnu- make  # cross build for device

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -fPIC -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -shared -ldl

all: libioctl_trace.so

libioctl_trace.so: trace_ioctls.o
	$(CC) -o $@ $^ $(LDFLAGS)

trace_ioctls.o: trace_ioctls.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f libioctl_trace.so trace_ioctls.o

.PHONY: all clean

