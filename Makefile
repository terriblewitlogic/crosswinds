LGFX ?= $(firstword $(foreach p,tools/host/vendor/LovyanGFX $(HOME)/Arduino/libraries/LovyanGFX,$(if $(wildcard $(p)/library.properties),$(abspath $(p)))))
# Xteink X4: ESP32-C3 with 16MB flash in DIO mode (GPIO12/13 are the SD chip
# select and the battery latch, so QIO is not possible), native USB serial.
X4_FQBN ?= esp32:esp32:esp32c3:CDCOnBoot=cdc,FlashMode=dio,FlashSize=16M,PartitionScheme=huge_app,UploadSpeed=921600
X4_BUILD_DIR ?= $(abspath tools/device_build/x4)
X4_BUILD_FLAGS ?= -DSH_X4_VOYAGE_STANDBY=1 -DSH_X4_STANDBY_DIAGNOSTICS=1
ARDUINO_LIBRARIES ?= $(abspath $(dir $(LGFX)))
PORT ?=

play-x4:
	$(MAKE) -C tools/host play-x4 LGFX="$(LGFX)"

shot-x4:
	$(MAKE) -C tools/host shot-x4 LGFX="$(LGFX)"

gallery-x4:
	$(MAKE) -C tools/host gallery-x4 LGFX="$(LGFX)"

gallery-x4-sheet: gallery-x4
	python3 tools/assets/build_x4_gallery_sheet.py

x4-test:
	$(MAKE) -C tools/host x4-test LGFX="$(LGFX)"

x4-delivery-test:
	$(MAKE) -C tools/host x4-delivery-test LGFX="$(LGFX)"

x4-route-geometry-test:
	$(MAKE) -C tools/host x4-route-geometry-test LGFX="$(LGFX)"

x4-watch-test:
	$(MAKE) -C tools/host x4-watch-test LGFX="$(LGFX)"

x4-nav-test:
	$(MAKE) -C tools/host x4-nav-test LGFX="$(LGFX)"

x4-dispatch-test:
	$(MAKE) -C tools/host x4-dispatch-test LGFX="$(LGFX)"

.PHONY: x4-dispatch-test

x4-merchant-test:
	$(MAKE) -C tools/headless merchant-test
	$(MAKE) -C tools/host x4-merchant-test LGFX="$(LGFX)"

.PHONY: x4-merchant-test

x4-startup-input-test:
	$(MAKE) -C tools/headless button-events-test
	$(MAKE) -C tools/host x4-startup-input-test LGFX="$(LGFX)"

.PHONY: x4-startup-input-test

x4-watch-journey-test:
	$(MAKE) -C tools/host x4-watch-journey-test LGFX="$(LGFX)"

x4-navigation-test:
	$(MAKE) -C tools/host x4-navigation-test LGFX="$(LGFX)"

x4-goal-moment-test:
	$(MAKE) -C tools/host x4-goal-moment-test LGFX="$(LGFX)"

x4-onboarding-test:
	$(MAKE) -C tools/host x4-onboarding-test LGFX="$(LGFX)"

.PHONY: x4-watch-journey-test x4-goal-moment-test x4-onboarding-test

x4-growth-test:
	$(MAKE) -C tools/host x4-growth-test LGFX="$(LGFX)"

x4-return-test:
	$(MAKE) -C tools/host x4-return-test LGFX="$(LGFX)"

.PHONY: x4-return-test

x4-manifest-test:
	$(MAKE) -C tools/host x4-manifest-test LGFX="$(LGFX)"

.PHONY: x4-manifest-test

x4-buffered-input-test:
	$(MAKE) -C tools/headless button-events-test
	$(MAKE) -C tools/host x4-buffered-input-test LGFX="$(LGFX)"

.PHONY: x4-buffered-input-test

load-test:
	$(MAKE) -C tools/host load-test LGFX="$(LGFX)"

power-test:
	$(MAKE) -C tools/host power-test LGFX="$(LGFX)"

headless-test:
	$(MAKE) -C tools/headless test economy-regressions voyage-test manifest-test persistence-test return-test merchant-monte-carlo

voyage-test:
	$(MAKE) -C tools/headless voyage-test manifest-test persistence-test

standby-test:
	$(MAKE) -C tools/headless standby-test
	$(MAKE) -C tools/host standby-test LGFX="$(LGFX)"

save-test:
	$(MAKE) -C tools/host test LGFX="$(LGFX)"

ubsan:
	$(MAKE) -C tools/headless ubsan

art:
	python3 tools/assets/build_ship_sprites.py
	python3 tools/assets/build_generated_art_x4.py

x4-routes: art
	python3 tools/assets/build_x4_routes.py
	python3 tools/assets/check_x4_routes.py

verify: headless-test save-test x4-test x4-nav-test x4-dispatch-test x4-growth-test x4-buffered-input-test x4-manifest-test x4-route-geometry-test x4-merchant-test x4-return-test x4-startup-input-test x4-watch-journey-test load-test power-test standby-test ubsan

device-x4:
	arduino-cli compile --fqbn "$(X4_FQBN)" --build-path "$(X4_BUILD_DIR)" \
	  --build-property "compiler.cpp.extra_flags=$(X4_BUILD_FLAGS)" --libraries "$(ARDUINO_LIBRARIES)" .

# The upload's output goes to a log, never through a pipe (a closed pipe kills
# esptool after the bootloader and the app never lands). The app is then read
# back from the device and compared with the build, so a flash that did not
# take fails here instead of on the reader.
ESPTOOL ?= $(firstword $(wildcard $(HOME)/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool) esptool.py)

backup-x4:
	@test -n "$(PORT)" || { echo "Set PORT=/dev/cu.usbmodem..."; exit 1; }
	python3 tools/device/backup_x4.py --port "$(PORT)" --esptool "$(ESPTOOL)"

upload-x4:
# Compilation must be in this same shell too: make -i can ignore a failed
# prerequisite and otherwise flash a stale, save-incompatible firmware image.
	@test -n "$(PORT)" && \
	  arduino-cli compile --fqbn "$(X4_FQBN)" --build-path "$(X4_BUILD_DIR)" \
	    --build-property "compiler.cpp.extra_flags=$(X4_BUILD_FLAGS)" --libraries "$(ARDUINO_LIBRARIES)" . && \
	  python3 tools/device/backup_x4.py --port "$(PORT)" --esptool "$(ESPTOOL)" && \
	  arduino-cli upload -p "$(PORT)" --fqbn "$(X4_FQBN)" --input-dir "$(X4_BUILD_DIR)" . > "$(X4_BUILD_DIR)/upload.log" 2>&1 && \
	  sleep 2 && \
	  "$(ESPTOOL)" --chip esp32c3 --port "$(PORT)" --baud 921600 read-flash 0x10000 \
	    $$(wc -c < "$(X4_BUILD_DIR)/crosswinds.ino.bin" | tr -d ' ') "$(X4_BUILD_DIR)/readback.bin" > "$(X4_BUILD_DIR)/readback.log" 2>&1 && \
	  cmp -s "$(X4_BUILD_DIR)/readback.bin" "$(X4_BUILD_DIR)/crosswinds.ino.bin" && \
	  echo "upload verified: the app on the device matches the build" \
	  || { echo "X4 installation stopped. Set PORT=/dev/cu.usbmodem...; check compile/backup output and $(X4_BUILD_DIR)/upload.log or readback.log."; exit 1; }

clean:
	$(MAKE) -C tools/host clean
	$(MAKE) -C tools/headless clean

.PHONY: play-x4 shot-x4 gallery-x4 gallery-x4-sheet x4-test x4-nav-test x4-navigation-test x4-growth-test x4-delivery-test x4-route-geometry-test x4-watch-test load-test power-test headless-test voyage-test standby-test save-test ubsan art x4-routes verify device-x4 upload-x4 clean
.PHONY: backup-x4
