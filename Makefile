# Network Compression Detection Tool
#
# Targets:
#   make            - build ncd-client and ncd-server
#   make full       - client, server, and standalone probe (needs libpcap)
#   make probe      - standalone probe only
#   make test       - unit tests
#   make clean      - remove build artifacts

CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O2 -g
# glibc hides POSIX APIs under -std=c11 unless a feature macro is set.
CPPFLAGS += -D_DEFAULT_SOURCE -Icommon -Icommon/vendor
LDFLAGS  ?=
LDLIBS   ?=

BUILD_DIR := build
BIN_DIR   := bin

COMMON_SRCS := \
	common/util.c \
	common/config.c \
	common/timing.c \
	common/entropy.c \
	common/udp_train.c \
	common/cli.c \
	common/vendor/cJSON.c

COMMON_OBJS := $(COMMON_SRCS:%.c=$(BUILD_DIR)/%.o)

.PHONY: all client server probe test clean dirs

# Default: cooperative tools (no libpcap required)
all: client server

client: dirs $(BIN_DIR)/ncd-client
server: dirs $(BIN_DIR)/ncd-server
probe:  dirs $(BIN_DIR)/ncd-probe

full: client server probe

dirs:
	@mkdir -p $(BIN_DIR) $(BUILD_DIR)/common/vendor $(BUILD_DIR)/cooperative \
		$(BUILD_DIR)/standalone $(BUILD_DIR)/tests

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/ncd-client: cooperative/client.c $(COMMON_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BIN_DIR)/ncd-server: cooperative/server.c $(COMMON_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BIN_DIR)/ncd-probe: standalone/probe.c $(COMMON_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS) -lpcap

$(BIN_DIR)/test_timing: tests/test_timing.c $(BUILD_DIR)/common/timing.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BIN_DIR)/test_config: tests/test_config.c \
		$(BUILD_DIR)/common/config.o \
		$(BUILD_DIR)/common/util.o \
		$(BUILD_DIR)/common/vendor/cJSON.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BIN_DIR)/test_entropy: tests/test_entropy.c $(BUILD_DIR)/common/entropy.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

test: dirs $(BIN_DIR)/test_timing $(BIN_DIR)/test_config $(BIN_DIR)/test_entropy
	@echo "Running unit tests..."
	$(BIN_DIR)/test_timing
	$(BIN_DIR)/test_config
	$(BIN_DIR)/test_entropy
	@echo "All tests passed."

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) received_config.json
