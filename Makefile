# OpenIMP Makefile
# Build system for libimp and libsysutils stub implementation

# Toolchain
CC ?= gcc
AR ?= ar
RANLIB ?= ranlib

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
LIB_DIR = lib

# Installation
PREFIX ?= /usr/local
INSTALL_INC_DIR = $(PREFIX)/include
INSTALL_LIB_DIR = $(PREFIX)/lib

# Compiler flags
CFLAGS = -Wall -Wextra -O2 -fPIC -I$(INC_DIR)
LDFLAGS = -shared -lpthread -lrt

# Platform detection (can be overridden)
PLATFORM ?= T31

# Add platform define
CFLAGS += -DPLATFORM_$(PLATFORM)

# Source files
IMP_SOURCES = \
	$(SRC_DIR)/imp_system.c \
	$(SRC_DIR)/imp_isp.c \
	$(SRC_DIR)/imp_framesource.c \
	$(SRC_DIR)/imp_encoder.c \
	$(SRC_DIR)/imp_audio.c \
	$(SRC_DIR)/imp_osd.c \
	$(SRC_DIR)/imp_ivs.c

SU_SOURCES = \
	$(SRC_DIR)/su_base.c

# Object files
IMP_OBJECTS = $(IMP_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
SU_OBJECTS = $(SU_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Library names
LIBIMP_SO = $(LIB_DIR)/libimp.so
LIBIMP_A = $(LIB_DIR)/libimp.a
LIBSU_SO = $(LIB_DIR)/libsysutils.so
LIBSU_A = $(LIB_DIR)/libsysutils.a

# Targets
.PHONY: all clean install test

all: $(LIBIMP_SO) $(LIBIMP_A) $(LIBSU_SO) $(LIBSU_A)

# Create directories
$(BUILD_DIR) $(LIB_DIR):
	mkdir -p $@

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Build libimp shared library
$(LIBIMP_SO): $(IMP_OBJECTS) | $(LIB_DIR)
	$(CC) $(LDFLAGS) -o $@ $^

# Build libimp static library
$(LIBIMP_A): $(IMP_OBJECTS) | $(LIB_DIR)
	$(AR) rcs $@ $^
	$(RANLIB) $@

# Build libsysutils shared library
$(LIBSU_SO): $(SU_OBJECTS) | $(LIB_DIR)
	$(CC) $(LDFLAGS) -o $@ $^

# Build libsysutils static library
$(LIBSU_A): $(SU_OBJECTS) | $(LIB_DIR)
	$(AR) rcs $@ $^
	$(RANLIB) $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(LIB_DIR)

# Install libraries and headers
install: all
	install -d $(INSTALL_INC_DIR)/imp
	install -d $(INSTALL_INC_DIR)/sysutils
	install -d $(INSTALL_LIB_DIR)
	install -m 644 $(INC_DIR)/imp/*.h $(INSTALL_INC_DIR)/imp/
	install -m 644 $(INC_DIR)/sysutils/*.h $(INSTALL_INC_DIR)/sysutils/
	install -m 755 $(LIBIMP_SO) $(INSTALL_LIB_DIR)/
	install -m 644 $(LIBIMP_A) $(INSTALL_LIB_DIR)/
	install -m 755 $(LIBSU_SO) $(INSTALL_LIB_DIR)/
	install -m 644 $(LIBSU_A) $(INSTALL_LIB_DIR)/

# Test target
test: all
	@echo "Building test..."
	$(CC) $(CFLAGS) -L$(LIB_DIR) tests/api_test.c -o $(BUILD_DIR)/api_test -limp -lsysutils
	@echo "Running test..."
	LD_LIBRARY_PATH=$(LIB_DIR) $(BUILD_DIR)/api_test

# Help target
help:
	@echo "OpenIMP Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build all libraries (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  install  - Install libraries and headers"
	@echo "  test     - Build and run tests"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  CC       - C compiler (default: gcc)"
	@echo "  PREFIX   - Installation prefix (default: /usr/local)"
	@echo "  PLATFORM - Target platform (default: T31)"
	@echo "             Options: T21, T23, T31, C100, T40, T41"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build for T31"
	@echo "  make PLATFORM=T23       # Build for T23"
	@echo "  make install PREFIX=~/.local  # Install to home directory"

