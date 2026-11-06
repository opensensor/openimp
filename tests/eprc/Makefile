CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror -ffp-contract=off
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests
EPRC_TEST := $(BUILD)/eprc-test
EPRC30_TEST := $(BUILD)/eprc-t30-test

.PHONY: check clean

check: $(EPRC_TEST) $(EPRC30_TEST)
	$(EPRC_TEST) eprc_vectors.txt
	$(EPRC_TEST) eprc_t21_vectors.txt t21
	$(EPRC_TEST) eprc_ext_vectors.txt
	$(EPRC_TEST) eprc_t21_ext_vectors.txt t21
	$(EPRC_TEST) eprc_random_vectors.txt
	$(EPRC_TEST) eprc_t21_random_vectors.txt t21
	$(EPRC_TEST) eprc_mbrc_vectors.txt
	$(EPRC_TEST) eprc_t21_mbrc_vectors.txt t21
	$(EPRC30_TEST) eprc_t30_vectors.txt
	$(EPRC30_TEST) eprc_t30_real_vectors.txt
	$(EPRC30_TEST) eprc_t30_random_vectors.txt
	$(EPRC30_TEST) eprc_t30_static_vectors.txt
	$(EPRC30_TEST) eprc_t30_h265_vectors.txt
	$(EPRC30_TEST) eprc_t30_h265_random_vectors.txt

$(EPRC_TEST): eprc_test.c $(PROJECT_DIR)/src/eprc/eprc.c $(PROJECT_DIR)/src/eprc/eprc_t21.c $(PROJECT_DIR)/src/eprc/eprc_mbrc.c \
		$(PROJECT_DIR)/src/eprc/eprc.h $(PROJECT_DIR)/src/eprc/eprc_internal.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src -o $@ eprc_test.c \
		$(PROJECT_DIR)/src/eprc/eprc.c $(PROJECT_DIR)/src/eprc/eprc_t21.c \
		$(PROJECT_DIR)/src/eprc/eprc_mbrc.c -lm

$(EPRC30_TEST): eprc_t30_test.c $(PROJECT_DIR)/src/eprc/eprc_t30.c \
		$(PROJECT_DIR)/src/eprc/eprc.h $(PROJECT_DIR)/src/eprc/eprc_internal.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fwrapv -I$(PROJECT_DIR)/src -o $@ eprc_t30_test.c \
		$(PROJECT_DIR)/src/eprc/eprc_t30.c -lm

clean:
	$(RM) $(EPRC_TEST) $(EPRC30_TEST)
