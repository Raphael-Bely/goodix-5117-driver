CC     := gcc
CFLAGS := -std=gnu11 -Wall -Wextra -Wpedantic -O2 -g -Iinclude \
          $(shell pkg-config --cflags libusb-1.0 openssl)
# Record which headers each target used, so touching one rebuilds what it
# should. The name is pinned to the target: src/match.c and tools/match.c
# produce build/match.o and build/match, and letting the compiler derive the
# name would have both write build/match.d, silently losing one.
DEPFLAGS = -MMD -MP -MF $@.d

LDLIBS := $(shell pkg-config --libs libusb-1.0 openssl) -lm

BUILD := build
LIB   := $(BUILD)/libgoodix.a
SRCS  := $(wildcard src/*.c)
OBJS  := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
# fplist links against libfprint rather than our own code, so it has its own
# target and stays out of the default build.
TOOL_SRCS := $(filter-out tools/fplist.c,$(wildcard tools/*.c))
TOOLS     := $(patsubst tools/%.c,$(BUILD)/%,$(TOOL_SRCS))

all: $(TOOLS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(LIB): $(OBJS)
	ar rcs $@ $^

$(BUILD)/%: tools/%.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -o $@ $< $(LIB) $(LDLIBS)

# Separate target: this one links against libfprint, which only exists once
# the local build has been installed. Source libfprint-env.sh first.
fplist: | $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/fplist tools/fplist.c \
	      $(shell pkg-config --cflags --libs libfprint-2 glib-2.0)

# The driver must live inside the libfprint tree, so our sources are copied
# there rather than linked from outside. This project stays the one place they
# are edited.
DRIVER_DIR := libfprint/libfprint/drivers/goodix5117
DRIVER_SRCS := device.c protocol.c config.c init.c session.c tls.c \
               capture.c image.c match.c

sync-driver:
	mkdir -p $(DRIVER_DIR)/goodix
	cp include/goodix/*.h $(DRIVER_DIR)/goodix/
	cp $(addprefix src/,$(DRIVER_SRCS)) $(DRIVER_DIR)/
	cp driver/goodix5117.c $(DRIVER_DIR)/
	@echo "synced $(words $(DRIVER_SRCS)) sources + the adapter into $(DRIVER_DIR)"

TEST_SRCS := $(wildcard tests/test_*.c)
TESTS     := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRCS))

$(BUILD)/test_%: tests/test_%.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Itests -o $@ $< $(LIB) $(LDLIBS)

# None of these touch the sensor, so they run anywhere.
test: $(TESTS)
	@status=0; for t in $(TESTS); do $$t || status=1; done; \
	 [ $$status -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"; \
	 exit $$status

# Defined here so every variable it refers to already exists: `:=` expands
# immediately, and TESTS is declared above.
DEPS := $(addsuffix .d,$(OBJS) $(TOOLS) $(TESTS))
-include $(DEPS)

clean:
	rm -rf $(BUILD)

.PHONY: all clean fplist sync-driver test
