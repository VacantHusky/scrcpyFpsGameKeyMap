# This makefile provides recipes to build a "portable" version of scrcpy for
# Windows.
#
# Here, "portable" means that the client and server binaries are expected to be
# anywhere, but in the same directory, instead of well-defined separate
# locations (e.g. /usr/bin/scrcpy and /usr/share/scrcpy/scrcpy-server).
#
# In particular, this implies to change the location from where the client push
# the server to the device.

.PHONY: default clean \
	test \
	build-server \
	prepare-deps \
	build-win64 \
	dist-win64 \
	zip-win64 \
	release

GRADLE ?= ./gradlew

TEST_BUILD_DIR := build-test
SERVER_BUILD_DIR := build-server
WIN64_BUILD_DIR := build-win64

VERSION := $(shell git describe --tags --always)

DIST := dist
WIN64_TARGET_DIR := scrcpy-win64-$(VERSION)
WIN64_TARGET := $(WIN64_TARGET_DIR).zip

RELEASE_DIR := release-$(VERSION)

release: clean test build-server zip-win64
	mkdir -p "$(RELEASE_DIR)"
	cp "$(SERVER_BUILD_DIR)/server/scrcpy-server" \
		"$(RELEASE_DIR)/scrcpy-server-$(VERSION)"
	cp "$(DIST)/$(WIN64_TARGET)" "$(RELEASE_DIR)"
	cd "$(RELEASE_DIR)" && \
		sha256sum "scrcpy-server-$(VERSION)" \
			"scrcpy-win64-$(VERSION).zip" > SHA256SUMS.txt
	@echo "Release generated in $(RELEASE_DIR)/"

clean:
	$(GRADLE) clean
	rm -rf "$(DIST)" "$(TEST_BUILD_DIR)" "$(SERVER_BUILD_DIR)" \
		"$(WIN64_BUILD_DIR)"

test:
	[ -d "$(TEST_BUILD_DIR)" ] || ( mkdir "$(TEST_BUILD_DIR)" && \
		meson setup "$(TEST_BUILD_DIR)" -Db_sanitize=address )
	ninja -C "$(TEST_BUILD_DIR)"
	$(GRADLE) -p server check

build-server:
	[ -d "$(SERVER_BUILD_DIR)" ] || ( mkdir "$(SERVER_BUILD_DIR)" && \
		meson setup "$(SERVER_BUILD_DIR)" --buildtype release -Dcompile_app=false )
	ninja -C "$(SERVER_BUILD_DIR)"

prepare-deps:
	@app/prebuilt-deps/prepare-adb.sh
	@app/prebuilt-deps/prepare-sdl.sh
	@app/prebuilt-deps/prepare-ffmpeg.sh
	@app/prebuilt-deps/prepare-libusb.sh


build-win64: prepare-deps
	rm -rf "$(WIN64_BUILD_DIR)"
	mkdir -p "$(WIN64_BUILD_DIR)/local"
	cp -r app/prebuilt-deps/data/ffmpeg-6.1-scrcpy-3/win64/. "$(WIN64_BUILD_DIR)/local/"
	cp -r app/prebuilt-deps/data/SDL2-2.28.5/x86_64-w64-mingw32/. "$(WIN64_BUILD_DIR)/local/"
	cp -r app/prebuilt-deps/data/libusb-1.0.26/libusb-MinGW-x64/. "$(WIN64_BUILD_DIR)/local/"
	meson setup "$(WIN64_BUILD_DIR)" \
		--pkg-config-path="$(WIN64_BUILD_DIR)/local/lib/pkgconfig" \
		-Dc_args="-I$(PWD)/$(WIN64_BUILD_DIR)/local/include" \
		-Dc_link_args="-L$(PWD)/$(WIN64_BUILD_DIR)/local/lib" \
		--cross-file=cross_win64.txt \
		--buildtype=release --strip -Db_lto=true \
		-Dcompile_server=false \
		-Dportable=true
	ninja -C "$(WIN64_BUILD_DIR)"

dist-win64: build-server build-win64
	mkdir -p "$(DIST)/$(WIN64_TARGET_DIR)"
	cp "$(SERVER_BUILD_DIR)"/server/scrcpy-server "$(DIST)/$(WIN64_TARGET_DIR)/"
	cp "$(WIN64_BUILD_DIR)"/app/scrcpy.exe "$(DIST)/$(WIN64_TARGET_DIR)/"
	cp app/data/scrcpy-console.bat "$(DIST)/$(WIN64_TARGET_DIR)/"
	cp app/data/scrcpy-noconsole.vbs "$(DIST)/$(WIN64_TARGET_DIR)/"
	cp app/data/icon.png "$(DIST)/$(WIN64_TARGET_DIR)/"
	cp app/data/open_a_terminal_here.bat "$(DIST)/$(WIN64_TARGET_DIR)/"
	cp app/prebuilt-deps/data/platform-tools-34.0.5/adb.exe "$(DIST)/$(WIN64_TARGET_DIR)/"
	cp app/prebuilt-deps/data/platform-tools-34.0.5/AdbWinApi.dll "$(DIST)/$(WIN64_TARGET_DIR)/"
	cp app/prebuilt-deps/data/platform-tools-34.0.5/AdbWinUsbApi.dll "$(DIST)/$(WIN64_TARGET_DIR)/"
	cp "$(WIN64_BUILD_DIR)"/local/bin/*.dll "$(DIST)/$(WIN64_TARGET_DIR)/"
	cp fps_game_config.txt "$(DIST)/$(WIN64_TARGET_DIR)/"

zip-win64: dist-win64
	cd "$(DIST)"; \
		zip -r "$(WIN64_TARGET)" "$(WIN64_TARGET_DIR)"
