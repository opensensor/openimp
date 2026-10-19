# Host test of the test pattern API (src/openimp_testpattern.c): make -C tests/testpattern check
CC ?= cc
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests

.PHONY: check clean

check: $(BUILD)/tp_test $(BUILD)/tp_hold
	$(BUILD)/tp_test
	$(CC) -O2 -Wall -Wextra -Werror -o $(BUILD)/tpctl $(PROJECT_DIR)/tools/testpattern/tpctl.c
	sh ctl_test.sh $(BUILD)

$(BUILD)/tp_test: tp_test.c $(PROJECT_DIR)/src/openimp_testpattern.c $(PROJECT_DIR)/src/openimp_testpattern_gen.h \
		$(PROJECT_DIR)/src/openimp_testpattern_logo.h $(PROJECT_DIR)/include/openimp_testpattern.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu99 -O2 -g -Wall -Wextra -Werror -I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src -o $@ tp_test.c \
		$(PROJECT_DIR)/src/openimp_testpattern.c -lpthread

clean:
	$(RM) $(BUILD)/tp_test $(BUILD)/tpctl

$(BUILD)/tp_hold: tp_hold.c $(PROJECT_DIR)/src/openimp_testpattern.c $(PROJECT_DIR)/src/openimp_testpattern_gen.h
	@mkdir -p $(BUILD)
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src -o $@ tp_hold.c \
		$(PROJECT_DIR)/src/openimp_testpattern.c -lpthread
