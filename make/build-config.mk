# Compare the effective commands before reusing objects from an earlier build.
# Immediate, private values prevent target-specific bot flags from changing the
# shared receipt. Values reach printf through the environment, not shell code.
define MUS_COMPILE_CONFIG
CC=$(CC)
CPPFLAGS=$(CPPFLAGS)
CFLAGS=$(CFLAGS)
THREAD_FLAGS=$(THREAD_FLAGS)
C_STANDARD=$(C_STANDARD)
WARNINGS=$(WARNINGS) $(C_WARNINGS)
DEPENDENCY_FLAGS=$(DEPENDENCY_FLAGS)
endef

define MUS_LINK_CONFIG
CC=$(CC)
LDFLAGS=$(LDFLAGS)
THREAD_FLAGS=$(THREAD_FLAGS)
LDLIBS=$(LDLIBS)
CFR_BUILD_DIR=$(CFR_BUILD_DIR)
endef

define MUS_ARCHIVE_CONFIG
AR=$(AR)
LIB_SOURCES=$(LIB_SOURCES)
endef

$(RELEASE_DIR)/.mus-compile-config: private export MUS_BUILD_CONFIG := $(MUS_COMPILE_CONFIG)
$(RELEASE_DIR)/.mus-compile-config: private export MUS_MODE_CONFIG := $(RELEASE_FLAGS)
$(DEBUG_DIR)/.mus-compile-config: private export MUS_BUILD_CONFIG := $(MUS_COMPILE_CONFIG)
$(DEBUG_DIR)/.mus-compile-config: private export MUS_MODE_CONFIG := $(DEBUG_FLAGS)
$(RELEASE_DIR)/.mus-link-config: private export MUS_BUILD_CONFIG := $(MUS_LINK_CONFIG)
$(RELEASE_DIR)/.mus-link-config: private export MUS_MODE_CONFIG := $(RELEASE_LINK_FLAGS)
$(DEBUG_DIR)/.mus-link-config: private export MUS_BUILD_CONFIG := $(MUS_LINK_CONFIG)
$(DEBUG_DIR)/.mus-link-config: private export MUS_MODE_CONFIG :=
$(RELEASE_DIR)/.mus-archive-config $(DEBUG_DIR)/.mus-archive-config: private export MUS_BUILD_CONFIG := $(MUS_ARCHIVE_CONFIG)
$(RELEASE_DIR)/.mus-archive-config $(DEBUG_DIR)/.mus-archive-config: private export MUS_MODE_CONFIG :=

MUS_CONFIG_FILES := $(foreach mode,$(RELEASE_DIR) $(DEBUG_DIR),\
	$(addprefix $(mode)/.mus-,compile-config archive-config link-config))

.PHONY: mus-config-force
mus-config-force:

$(MUS_CONFIG_FILES): mus-config-force
	+@mkdir -p $(dir $@)
	+@printf '%s\n%s\n' "$$MUS_BUILD_CONFIG" "$$MUS_MODE_CONFIG" > $@.tmp
	+@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv -f $@.tmp $@; fi
