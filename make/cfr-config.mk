# Load after C-CFR/Makefile. Keep the submodule sources and build rules intact.
include $(dir $(lastword $(MAKEFILE_LIST)))build-config.mk

$(RELEASE_OBJECTS): $(RELEASE_DIR)/.mus-compile-config
$(DEBUG_OBJECTS): $(DEBUG_DIR)/.mus-compile-config

# The upstream archive recipe uses $^, so put only object paths in that list.
# A changed archiver must still invalidate the objects and thus the archive.
$(RELEASE_OBJECTS): $(RELEASE_DIR)/.mus-archive-config
$(DEBUG_OBJECTS): $(DEBUG_DIR)/.mus-archive-config
