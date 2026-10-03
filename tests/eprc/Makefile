CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror -ffp-contract=off
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests
EPRC_TEST := $(BUILD)/eprc-test

.PHONY: check clean

check: $(EPRC_TEST)
	$(EPRC_TEST) eprc_vectors.txt
	$(EPRC_TEST) eprc_t21_vectors.txt t21
	$(EPRC_TEST) eprc_ext_vectors.txt
	$(EPRC_TEST) eprc_t21_ext_vectors.txt t21
	$(EPRC_TEST) eprc_random_vectors.txt
	$(EPRC_TEST) eprc_t21_random_vectors.txt t21
	$(EPRC_TEST) eprc_mbrc_vectors.txt
	$(EPRC_TEST) eprc_t21_mbrc_vectors.txt t21

$(EPRC_TEST): eprc_test.c $(PROJECT_DIR)/src/eprc/eprc.c $(PROJECT_DIR)/src/eprc/eprc_t21.c $(PROJECT_DIR)/src/eprc/eprc_mbrc.c \
		$(PROJECT_DIR)/src/eprc/eprc.h $(PROJECT_DIR)/src/eprc/eprc_internal.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src -o $@ eprc_test.c \
		$(PROJECT_DIR)/src/eprc/eprc.c $(PROJECT_DIR)/src/eprc/eprc_t21.c \
		$(PROJECT_DIR)/src/eprc/eprc_mbrc.c -lm

clean:
	$(RM) $(EPRC_TEST)
