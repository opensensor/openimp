CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests
FISHEYE_TEST := $(BUILD)/fisheye-test
SRC := $(PROJECT_DIR)/src/fisheye/fisheye.c $(PROJECT_DIR)/src/fisheye/fisheye.h

.PHONY: check clean

check: $(FISHEYE_TEST)
	$(FISHEYE_TEST) fisheye_vectors.txt

$(FISHEYE_TEST): fisheye_test.c $(SRC)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src -o $@ fisheye_test.c $(PROJECT_DIR)/src/fisheye/fisheye.c

clean:
	$(RM) $(FISHEYE_TEST)
