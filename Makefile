# Top-level tasks for the gadget.
#
# Two toolchains, two targets: the host suite is plain g++ plus Python
# (tools/run_tests.py), and the firmware is built with arduino-cli using the
# board options from esp32_gadget/INSTALL.md §3. Everything is overridable
# because on WSL the only arduino-cli may be the Windows one, which needs
# Windows paths and receives Windows arguments:
#
#   make build ARDUINO_CLI='"/mnt/c/Program Files/Arduino CLI/arduino-cli.exe"' \
#              SKETCH='C:/Users/me/workspace/esp32/esp32_gadget' \
#              WEB_UI='C:/Users/me/workspace/esp32/esp32_gadget/web_ui' \
#              WEB_ASSETS='C:/Users/me/workspace/esp32/esp32_gadget/web_assets.h' \
#              BUILD_DIR='C:/Users/me/workspace/esp32/.build-make'
#
# The targets that only need Python or g++ (test, pack, verify-assets, seed)
# take plain relative paths and work anywhere.

PYTHON ?= python3
ARDUINO_CLI ?= arduino-cli

# The FQBN is fixed: PSRAM=opi and CDCOnBoot=cdc are not preferences. Wrong PSRAM
# does not boot and wrong CDC leaves the console silent. See INSTALL.md §3.
FQBN ?= esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashMode=qio,UploadSpeed=921600,USBMode=hwcdc,UploadMode=default,CPUFreq=240

SKETCH ?= esp32_gadget
WEB_UI ?= $(SKETCH)/web_ui
WEB_ASSETS ?= $(SKETCH)/web_assets.h
BUILD_DIR ?= .build-make

# Serial port for `make flash`; never guessed, always passed by the owner.
PORT ?=

.PHONY: help test pack verify-assets seed build flash clean

help:
	@echo "make test          host suite: pure C++ tests + web asset round trip"
	@echo "make pack          regenerate $(WEB_ASSETS) from $(WEB_UI)"
	@echo "make verify-assets fail when $(WEB_ASSETS) is stale"
	@echo "make seed          generate admin_seed.h from .env"
	@echo "make build         verify assets, then compile the firmware"
	@echo "make flash         build, then upload to PORT=<com port>"
	@echo "make clean         remove the arduino-cli build directory"
	@echo ""
	@echo "Overridables: PYTHON, ARDUINO_CLI, FQBN, SKETCH, BUILD_DIR, PORT"

# The suite compiles every tools/*_test.cpp and checks that web_assets.h matches
# web_ui/. A test that silently stops compiling looks like a test that passes,
# which is why this is the one command to run before touching the firmware.
test:
	$(PYTHON) tools/run_tests.py

pack:
	$(PYTHON) tools/pack_web.py $(WEB_UI) $(WEB_ASSETS)

verify-assets:
	$(PYTHON) tools/verify_assets.py $(WEB_UI) $(WEB_ASSETS)

seed:
	$(PYTHON) tools/gen_admin_defaults.py

# Fails instead of repacking: a stale panel is a real defect and it has to be
# seen, not silently repaired. `make pack` is the repair.
build: verify-assets
	$(ARDUINO_CLI) compile --fqbn "$(FQBN)" --build-path "$(BUILD_DIR)" "$(SKETCH)"

flash: build
	@test -n "$(PORT)" || { echo "usage: make flash PORT=COM5" >&2; exit 1; }
	$(ARDUINO_CLI) upload -p "$(PORT)" --fqbn "$(FQBN)" --build-path "$(BUILD_DIR)" "$(SKETCH)"

clean:
	rm -rf "$(BUILD_DIR)"
