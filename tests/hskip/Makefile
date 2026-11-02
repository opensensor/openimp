CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests
HSKIP_TEST := $(BUILD)/hskip-test
BLACK_TEST := $(BUILD)/hskip-black-test
H264 := $(PROJECT_DIR)/src/t30/h264enc

.PHONY: check clean

check: $(HSKIP_TEST) $(BLACK_TEST)
	$(HSKIP_TEST) hskip_vectors.txt
	$(BLACK_TEST) black_vectors.txt

$(HSKIP_TEST): hskip_test.c $(PROJECT_DIR)/src/hskip/hskip.c $(PROJECT_DIR)/src/hskip/hskip.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src -o $@ hskip_test.c $(PROJECT_DIR)/src/hskip/hskip.c

$(BLACK_TEST): black_test.c $(PROJECT_DIR)/src/hskip/hskip_black.c $(PROJECT_DIR)/src/hskip/hskip.h $(H264)/cabac.c $(H264)/common.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src -I$(H264) -o $@ black_test.c $(PROJECT_DIR)/src/hskip/hskip_black.c $(H264)/cabac.c $(H264)/common.c

clean:
	$(RM) $(HSKIP_TEST) $(BLACK_TEST)
