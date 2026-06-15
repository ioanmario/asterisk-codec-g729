# Makefile for codec_g729.so - G.729 transcoder module for Asterisk 21/22
#
# Builds the bundled bcg729 submodule as a static library and links it into
# a single self-contained Asterisk module (no separate libbcg729 install
# needed on the target box).

CC          ?= gcc
CMAKE       ?= cmake

# Asterisk development headers (asterisk.h, asterisk/translate.h, ...).
ASTERISK_INCLUDE ?= /usr/include

# Where Asterisk loads modules from. Auto-detected via pkg-config when
# available, otherwise the common default.
MODULES_DIR ?= $(shell pkg-config --variable=modulesdir asterisk 2>/dev/null)
ifeq ($(strip $(MODULES_DIR)),)
MODULES_DIR := /usr/lib/asterisk/modules
endif

BCG729_DIR   := third_party/bcg729
BCG729_BUILD := $(BCG729_DIR)/build

TARGET := codec_g729.so
SRC    := src/codec_g729.c

CFLAGS  += -O2 -fPIC -Wall -Wextra -D_GNU_SOURCE
CFLAGS  += -DAST_MODULE_SELF_SYM=__internal_codec_g729_self
CFLAGS  += -I$(ASTERISK_INCLUDE) -I$(BCG729_DIR)/include
LDFLAGS += -shared

.PHONY: all bcg729 install uninstall clean distclean

all: $(TARGET)

# Build bcg729 as a static, position-independent library so it can be linked
# into our shared module.
bcg729:
	@if [ ! -f "$(BCG729_DIR)/CMakeLists.txt" ]; then \
		echo "ERROR: $(BCG729_DIR) is empty. Run: git submodule update --init --recursive"; \
		exit 1; \
	fi
	$(CMAKE) -S $(BCG729_DIR) -B $(BCG729_BUILD) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DBUILD_SHARED_LIBS=OFF \
		-DENABLE_STRICT=NO \
		-DENABLE_UNIT_TESTS=NO
	$(CMAKE) --build $(BCG729_BUILD) --config Release

$(TARGET): $(SRC) bcg729
	$(CC) $(CFLAGS) $(SRC) \
		$$(find $(BCG729_BUILD) -name 'libbcg729.a' | head -n1) \
		$(LDFLAGS) -o $(TARGET)
	@echo "==> Built $(TARGET)"

install: $(TARGET)
	install -d "$(DESTDIR)$(MODULES_DIR)"
	install -m 0644 $(TARGET) "$(DESTDIR)$(MODULES_DIR)/$(TARGET)"
	@echo "==> Installed to $(DESTDIR)$(MODULES_DIR)/$(TARGET)"
	@echo "    In the Asterisk CLI:  module load $(TARGET)"

uninstall:
	rm -f "$(DESTDIR)$(MODULES_DIR)/$(TARGET)"

clean:
	rm -f $(TARGET)

distclean: clean
	rm -rf $(BCG729_BUILD)
