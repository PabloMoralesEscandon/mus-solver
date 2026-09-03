CC ?= cc
AR ?= ar

LDLIBS += -lzstd

CPPFLAGS += -Iinclude -IC-CFR/include -ISimulador-Mus/include
ifneq ($(origin MUS_MAX_MUSES), undefined)
CPPFLAGS += -DMUS_GAME_MAX_MUSES=$(MUS_MAX_MUSES)
endif
C_STANDARD := -std=c17
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror
RELEASE_LTO_FLAGS := -flto
RELEASE_FLAGS := -O2 $(RELEASE_LTO_FLAGS)
RELEASE_LINK_FLAGS := $(RELEASE_LTO_FLAGS)
DEBUG_FLAGS := -O0 -g3
DEPENDENCY_FLAGS := -MMD -MP
THREAD_FLAGS ?= -pthread

BUILD_DIR := build
CFR_BUILD_DIR ?= build
LTO_BUILD_DIR ?= build/lto
RELEASE_DIR := $(BUILD_DIR)/release
DEBUG_DIR := $(BUILD_DIR)/debug

MUS_SOURCES := src/mus_game.c src/mus_evaluation.c src/mus_strategy.c
SIMULATOR_SOURCES := Simulador-Mus/src/cartas.c \
	Simulador-Mus/src/baraja_espanola.c Simulador-Mus/src/mus.c
LIB_SOURCES := $(MUS_SOURCES) $(SIMULATOR_SOURCES)
APP_SOURCE := app/mus_solver_cli.c
TEST_SOURCE := tests/test_mus_solver.c

RELEASE_OBJECTS := $(patsubst %.c,$(RELEASE_DIR)/%.o,$(LIB_SOURCES))
DEBUG_OBJECTS := $(patsubst %.c,$(DEBUG_DIR)/%.o,$(LIB_SOURCES))
RELEASE_APP_OBJECT := $(patsubst %.c,$(RELEASE_DIR)/%.o,$(APP_SOURCE))
DEBUG_APP_OBJECT := $(patsubst %.c,$(DEBUG_DIR)/%.o,$(APP_SOURCE))
DEBUG_TEST_OBJECT := $(patsubst %.c,$(DEBUG_DIR)/%.o,$(TEST_SOURCE))

RELEASE_CFR_LIBRARY := C-CFR/$(CFR_BUILD_DIR)/release/libcfr.a
DEBUG_CFR_LIBRARY := C-CFR/$(CFR_BUILD_DIR)/debug/libcfr.a
RELEASE_LIBRARY := $(RELEASE_DIR)/libmus_solver.a
DEBUG_LIBRARY := $(DEBUG_DIR)/libmus_solver.a
RELEASE_BINARY := $(RELEASE_DIR)/mus-solver
DEBUG_BINARY := $(DEBUG_DIR)/mus-solver
TEST_BINARY := $(DEBUG_DIR)/mus_solver_tests

DEPENDENCY_FILES := $(RELEASE_OBJECTS:.o=.d) $(DEBUG_OBJECTS:.o=.d) \
	$(RELEASE_APP_OBJECT:.o=.d) $(DEBUG_APP_OBJECT:.o=.d) \
	$(DEBUG_TEST_OBJECT:.o=.d)

.PHONY: all lto debug test test-tsan test-sanitize dependencies cfr-release \
	cfr-debug clean

all: $(RELEASE_BINARY)

lto:
	$(MAKE) BUILD_DIR=$(LTO_BUILD_DIR) CFR_BUILD_DIR=$(LTO_BUILD_DIR) \
		all

debug: $(DEBUG_BINARY) $(TEST_BINARY)

test: $(TEST_BINARY) $(DEBUG_BINARY)
	./$(TEST_BINARY)
	./tests/test_cli.sh ./$(DEBUG_BINARY)

test-tsan:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/test-tsan \
		CFR_BUILD_DIR=$(CFR_BUILD_DIR)/test-tsan \
		CFLAGS='$(CFLAGS) -fsanitize=thread -fno-omit-frame-pointer' \
		LDFLAGS='$(LDFLAGS) -fsanitize=thread' test

test-sanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize \
		CFR_BUILD_DIR=$(CFR_BUILD_DIR)/sanitize \
		CFLAGS='$(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all' \
		LDFLAGS='$(LDFLAGS) -fsanitize=address,undefined' test

dependencies:
	$(MAKE) -C C-CFR test
	$(MAKE) -C Simulador-Mus test-release

cfr-release:
	$(MAKE) -C C-CFR BUILD_DIR=$(CFR_BUILD_DIR) \
		CFLAGS='$(CFLAGS) $(RELEASE_LTO_FLAGS)' \
		LDFLAGS='$(LDFLAGS) $(RELEASE_LINK_FLAGS)' \
		$(CFR_BUILD_DIR)/release/libcfr.a

cfr-debug:
	$(MAKE) -C C-CFR BUILD_DIR=$(CFR_BUILD_DIR) CFLAGS='$(CFLAGS)' \
		LDFLAGS='$(LDFLAGS)' $(CFR_BUILD_DIR)/debug/libcfr.a

$(RELEASE_LIBRARY): $(RELEASE_OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(DEBUG_LIBRARY): $(DEBUG_OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(RELEASE_BINARY): $(RELEASE_APP_OBJECT) $(RELEASE_LIBRARY) \
		cfr-release
	$(CC) $(LDFLAGS) $(RELEASE_LINK_FLAGS) $(THREAD_FLAGS) \
		$(RELEASE_APP_OBJECT) $(RELEASE_LIBRARY) $(RELEASE_CFR_LIBRARY) \
		$(LDLIBS) -o $@

$(DEBUG_BINARY): $(DEBUG_APP_OBJECT) $(DEBUG_LIBRARY) cfr-debug
	$(CC) $(LDFLAGS) $(THREAD_FLAGS) $(DEBUG_APP_OBJECT) $(DEBUG_LIBRARY) \
		$(DEBUG_CFR_LIBRARY) $(LDLIBS) -o $@

$(TEST_BINARY): $(DEBUG_TEST_OBJECT) $(DEBUG_LIBRARY) cfr-debug
	$(CC) $(LDFLAGS) $(DEBUG_TEST_OBJECT) $(DEBUG_LIBRARY) \
		$(DEBUG_CFR_LIBRARY) $(LDLIBS) -o $@

$(RELEASE_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) $(C_STANDARD) $(WARNINGS) \
		$(RELEASE_FLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

$(DEBUG_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) $(C_STANDARD) $(WARNINGS) \
		$(DEBUG_FLAGS) $(DEPENDENCY_FLAGS) -c $< -o $@

clean:
	$(RM) -r -- $(BUILD_DIR)

-include $(DEPENDENCY_FILES)
