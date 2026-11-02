CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests
DEMASK_TEST := $(BUILD)/demask-test
DEMASK_SIM := $(BUILD)/demask-sim
SRC := $(PROJECT_DIR)/src/demask/demask.c $(PROJECT_DIR)/src/demask/demask.h

.PHONY: check clean

check: $(DEMASK_TEST) $(DEMASK_SIM)
	$(DEMASK_TEST) demask_vectors.txt

$(DEMASK_TEST): demask_test.c $(SRC)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src -o $@ demask_test.c $(PROJECT_DIR)/src/demask/demask.c

$(DEMASK_SIM): demask_sim.c $(SRC)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(PROJECT_DIR)/src -o $@ demask_sim.c $(PROJECT_DIR)/src/demask/demask.c

clean:
	$(RM) $(DEMASK_TEST) $(DEMASK_SIM)
