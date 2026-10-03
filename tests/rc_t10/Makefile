CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror -ffp-contract=off
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests
RC_T10_TEST := $(BUILD)/rc-t10-test
SRCS := $(PROJECT_DIR)/src/rc_t10/rc_t10.c

.PHONY: check clean

check: $(RC_T10_TEST)
	$(RC_T10_TEST) rc_t10_vectors.txt

$(RC_T10_TEST): rc_t10_test.c $(SRCS) $(PROJECT_DIR)/src/rc_t10/rc_t10.h \
		$(PROJECT_DIR)/src/rc_t20/rc_t20_internal.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src -o $@ rc_t10_test.c $(SRCS) -lm

clean:
	$(RM) $(RC_T10_TEST)
