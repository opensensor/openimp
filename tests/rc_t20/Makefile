CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror -ffp-contract=off
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests
RC_T20_TEST := $(BUILD)/rc-t20-test
SRCS := $(PROJECT_DIR)/src/rc_t20/rc_t20.c $(PROJECT_DIR)/src/rc_t20/rc_t20_mb.c $(PROJECT_DIR)/src/hskip/hskip.c

.PHONY: check clean

check: $(RC_T20_TEST)
	$(RC_T20_TEST) rc_t20_vectors.txt
	$(RC_T20_TEST) rc_t20_vectors_hskip.txt
	RC_MB_RC2=0 $(RC_T20_TEST) rc_t20_vectors_changeref0.txt
	RC_MB_RC2=0 $(RC_T20_TEST) rc_t20_vectors_hskip_changeref0.txt

$(RC_T20_TEST): rc_t20_test.c $(SRCS) $(PROJECT_DIR)/src/rc_t20/rc_t20.h \
		$(PROJECT_DIR)/src/rc_t20/rc_t20_internal.h $(PROJECT_DIR)/src/hskip/hskip.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src -o $@ rc_t20_test.c $(SRCS) -lm

clean:
	$(RM) $(RC_T20_TEST)
