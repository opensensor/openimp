# IVS (T23 ABI) and JPEG user-table host tests: make -C tests/t23 -f stub_fixes.mk check
CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -Werror
PROJECT_DIR := ../..
BUILD := $(PROJECT_DIR)/build/tests
IVS_SRC := $(PROJECT_DIR)/src/t31
IVS_DEPS := $(IVS_SRC)/openimp_t31_ivs_move.c $(IVS_SRC)/openimp_t31_ivs_move.h \
	$(IVS_SRC)/openimp_t31_ivs_abi.h
DIGEST_T31 := $(BUILD)/t23-ivs-digest-t31abi
DIGEST_T23 := $(BUILD)/t23-ivs-digest-t23abi
FRAMEWORK := $(BUILD)/t23-ivs-framework-test

.PHONY: check clean

# The T23 move / base move results must equal the T31 ones bit for bit
# (same vendor scalar code, only the parameter layout differs), and the T23
# IVS framework must deliver them through the T23 capture record and ABI.
check: $(DIGEST_T31) $(DIGEST_T23) $(FRAMEWORK)
	$(DIGEST_T31) > $(BUILD)/t23-ivs-digest-t31abi.txt
	$(DIGEST_T23) > $(BUILD)/t23-ivs-digest-t23abi.txt
	cat $(BUILD)/t23-ivs-digest-t23abi.txt
	cmp $(BUILD)/t23-ivs-digest-t31abi.txt $(BUILD)/t23-ivs-digest-t23abi.txt
	$(FRAMEWORK)

$(DIGEST_T31): ivs_move_digest.c $(IVS_DEPS)
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -I$(IVS_SRC) ivs_move_digest.c \
		$(IVS_SRC)/openimp_t31_ivs_move.c -o "$@"

$(DIGEST_T23): ivs_move_digest.c $(IVS_DEPS)
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -DPLATFORM_T23 -I$(IVS_SRC) ivs_move_digest.c \
		$(IVS_SRC)/openimp_t31_ivs_move.c -o "$@"

$(FRAMEWORK): ivs_framework_test.c $(IVS_SRC)/openimp_t31_ivs.c \
	$(IVS_SRC)/openimp_t31_ivs.h $(IVS_DEPS)
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -DPLATFORM_T23 -I$(PROJECT_DIR)/include \
		-I$(PROJECT_DIR)/src -I$(IVS_SRC) ivs_framework_test.c \
		$(IVS_SRC)/openimp_t31_ivs.c $(IVS_SRC)/openimp_t31_ivs_move.c \
		-pthread -o "$@"

clean:
	$(RM) $(DIGEST_T31) $(DIGEST_T23) $(FRAMEWORK) \
		$(BUILD)/t23-ivs-digest-t31abi.txt $(BUILD)/t23-ivs-digest-t23abi.txt
