CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests
ZERO_COPY_TEST_BIN := $(BUILD)/t23-helix-zero-copy-test
FAKE_HELIXD_BIN := $(BUILD)/t23-fake-helixd
# The IMP headers pin the 32-bit MIPS ABI with _Static_assert; this host
# build (x86_64, both ends of the IPC built alike) only checks the bridge's
# input path, so those layout checks are compiled out here.
INCLUDES := -DPLATFORM_T23 '-D_Static_assert(c,m)=' -Wno-unused-result \
	-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src

.PHONY: check clean

check: $(ZERO_COPY_TEST_BIN) $(FAKE_HELIXD_BIN)
	$(ZERO_COPY_TEST_BIN) zero-copy "$(abspath $(FAKE_HELIXD_BIN))"
	$(ZERO_COPY_TEST_BIN) copy "$(abspath $(FAKE_HELIXD_BIN))"

$(ZERO_COPY_TEST_BIN): helix_zero_copy_test.c \
	$(PROJECT_DIR)/src/t23/openimp_t23_helix_bridge.c \
	$(PROJECT_DIR)/src/t23/openimp_t23_helix_bridge.h \
	$(PROJECT_DIR)/src/t23/openimp_t23_helix_ipc.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(INCLUDES) helix_zero_copy_test.c \
		$(PROJECT_DIR)/src/t23/openimp_t23_helix_bridge.c -o "$@" -lpthread

$(FAKE_HELIXD_BIN): fake_helixd.c $(PROJECT_DIR)/src/t23/openimp_t23_helix_ipc.h
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(INCLUDES) fake_helixd.c -o "$@"

clean:
	$(RM) $(ZERO_COPY_TEST_BIN) $(FAKE_HELIXD_BIN)
