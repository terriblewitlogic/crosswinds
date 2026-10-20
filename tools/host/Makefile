ROOT := $(abspath ../..)
X4_BUILD := build_x4
X4_BIN := $(X4_BUILD)/crosswinds_host
X4_RETURN_FS ?= $(X4_BUILD)/return_fs
X4_RETURN_DIR ?= $(X4_BUILD)/return_ui
# Every test recipe empties its SH_FS_DIR first, so a run never resumes the
# company an earlier run saved. (A recipe whose later step deliberately
# resumes the first step's save empties it only once, before the first.)

LGFX_VER := 1.2.24
VENDOR_LGFX := vendor/LovyanGFX
LGFX ?= $(firstword $(foreach p,$(VENDOR_LGFX) $(HOME)/Arduino/libraries/LovyanGFX,$(if $(wildcard $(p)/library.properties),$(abspath $(p)))))

SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)
ifeq ($(origin CXX),default)
CXX := clang++
endif
COMMON := -O2 -g -Wall -Wextra -Werror -DLGFX_SDL -DSH_HOST_TESTS \
          -Ishim -I$(ROOT) -isystem $(LGFX)/src $(SDL_CFLAGS) -MMD -MP \
          -Wno-vla-cxx-extension
X4_FEATURE_FLAGS ?= -DSH_X4_VOYAGE_STANDBY=1 -DSH_X4_STANDBY_DIAGNOSTICS=1
X4_COMMON := $(COMMON) $(X4_FEATURE_FLAGS)
X4_CXXFLAGS := -std=c++17 $(X4_COMMON)
X4_CFLAGS := $(X4_COMMON)

LGFX_SRC := $(wildcard $(LGFX)/src/lgfx/v1/*.cpp) \
            $(wildcard $(LGFX)/src/lgfx/v1/misc/*.cpp) \
            $(LGFX)/src/lgfx/v1/panel/Panel_Device.cpp \
            $(LGFX)/src/lgfx/v1/panel/Panel_FrameBufferBase.cpp \
            $(wildcard $(LGFX)/src/lgfx/v1/platforms/sdl/*.cpp)
LGFX_C := $(wildcard $(LGFX)/src/lgfx/utility/*.c) \
          $(wildcard $(LGFX)/src/lgfx/Fonts/efont/*.c) \
          $(wildcard $(LGFX)/src/lgfx/Fonts/IPA/*.c) \
          $(wildcard $(LGFX)/src/lgfx/v1/lv_font/*.c)
X4_GAME := content sim render_x4 board_x4 input save audio
X4_GAME_OBJ := $(X4_GAME:%=$(X4_BUILD)/game/%.o)
X4_LGFX_OBJ := $(patsubst $(LGFX)/src/lgfx/%.cpp,$(X4_BUILD)/lgfx/%.o,$(LGFX_SRC))
X4_LGFX_COBJ := $(patsubst $(LGFX)/src/lgfx/%.c,$(X4_BUILD)/lgfx/%.o,$(LGFX_C))
X4_OBJ := $(X4_BUILD)/main.o $(X4_GAME_OBJ) $(X4_LGFX_OBJ) $(X4_LGFX_COBJ)

all: $(X4_BIN)

deps: $(VENDOR_LGFX)/library.properties
$(VENDOR_LGFX)/library.properties:
	git clone --depth 1 --branch $(LGFX_VER) https://github.com/lovyan03/LovyanGFX.git $(VENDOR_LGFX)

$(X4_OBJ): | check-lgfx
check-lgfx:
	@test -f "$(LGFX)/library.properties" || { echo "LovyanGFX not found at $(LGFX). Run make deps or set LGFX=/path."; exit 1; }

$(X4_BIN): $(X4_OBJ)
	$(CXX) $^ -o $@ $(SDL_LIBS)

$(X4_BUILD)/main.o: main.cpp $(ROOT)/crosswinds.ino
	@mkdir -p $(@D)
	$(CXX) $(X4_CXXFLAGS) -c $< -o $@

$(X4_BUILD)/game/%.o: $(ROOT)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(X4_CXXFLAGS) -c $< -o $@

$(X4_BUILD)/lgfx/%.o: $(LGFX)/src/lgfx/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(X4_CXXFLAGS) -w -c $< -o $@

$(X4_BUILD)/lgfx/%.o: $(LGFX)/src/lgfx/%.c
	@mkdir -p $(@D)
	$(CC) $(X4_CFLAGS) -w -c $< -o $@

play-x4: $(X4_BIN)
	./$(X4_BIN)

shot-x4: $(X4_BIN)
	SH_DEMO=1 SH_SKIP_TITLE_MS=1 SH_SHOT_MS=1200 SH_SHOT_EXIT=1 \
	SH_SHOT_PATH=$(X4_BUILD)/shot.ppm SH_FS_DIR=$(X4_BUILD)/shot_fs ./$(X4_BIN)

gallery-x4: $(X4_BIN)
	rm -rf $(X4_BUILD)/gallery_fs
	SH_GALLERY=$(X4_BUILD)/gallery SH_FS_DIR=$(X4_BUILD)/gallery_fs ./$(X4_BIN)

# The main X4 check: the merchant journey (first voyage, trade, parcels,
# arrival, upgrades), screen navigation, the dedicated Watch journey and the
# goal moments.
x4-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/test_fs $(X4_BUILD)/test_navigation_fs $(X4_BUILD)/test_watch_journey_fs $(X4_BUILD)/test_goal_moment_fs
	SH_X4_TEST=1 SH_FS_DIR=$(X4_BUILD)/test_fs ./$(X4_BIN)
	SH_X4_NAVIGATION_TEST=1 SH_FS_DIR=$(X4_BUILD)/test_navigation_fs ./$(X4_BIN)
	SH_X4_WATCH_JOURNEY_TEST=1 SH_FS_DIR=$(X4_BUILD)/test_watch_journey_fs ./$(X4_BIN)
	SH_X4_GOAL_MOMENT_TEST=1 SH_FS_DIR=$(X4_BUILD)/test_goal_moment_fs ./$(X4_BIN)
	SH_X4_ONBOARDING_TEST=1 SH_FS_DIR=$(X4_BUILD)/test_onboarding_fs ./$(X4_BIN)
	rm -rf $(X4_BUILD)/test_story_fs
	SH_X4_STORY_TEST=1 SH_FS_DIR=$(X4_BUILD)/test_story_fs ./$(X4_BIN)
	rm -rf $(X4_BUILD)/test_atlas_fs
	SH_X4_ATLAS_TEST=1 SH_FS_DIR=$(X4_BUILD)/test_atlas_fs ./$(X4_BIN)

x4-goal-moment-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/goal_moment_fs
	SH_FS_DIR=$(X4_BUILD)/goal_moment_fs SH_X4_GOAL_MOMENT_DIR=$(abspath $(X4_BUILD)/goal_moment) SH_X4_GOAL_MOMENT_TEST=1 ./$(X4_BIN)

.PHONY: x4-goal-moment-test

# The Grey Passenger story parcels; captures land in build_x4/story.
x4-story-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/story_fs
	SH_FS_DIR=$(X4_BUILD)/story_fs SH_X4_STORY_DIR=$(abspath $(X4_BUILD)/story) SH_X4_STORY_TEST=1 ./$(X4_BIN)

.PHONY: x4-story-test

# The Sea Atlas; captures land in build_x4/atlas.
x4-atlas-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/atlas_fs
	SH_FS_DIR=$(X4_BUILD)/atlas_fs SH_X4_ATLAS_DIR=$(abspath $(X4_BUILD)/atlas) SH_X4_ATLAS_TEST=1 ./$(X4_BIN)

.PHONY: x4-atlas-test

x4-onboarding-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/onboarding_fs
	SH_FS_DIR=$(X4_BUILD)/onboarding_fs SH_X4_ONBOARDING_DIR=$(abspath $(X4_BUILD)/onboarding) SH_X4_ONBOARDING_TEST=1 ./$(X4_BIN)

.PHONY: x4-onboarding-test

x4-nav-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/nav_fs
	SH_X4_TEST=nav SH_FS_DIR=$(X4_BUILD)/nav_fs ./$(X4_BIN)

x4-manifest-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/manifest_fs
	SH_X4_TEST=manifest SH_X4_MANIFEST_DIR=$(X4_BUILD)/manifest SH_FS_DIR=$(X4_BUILD)/manifest_fs ./$(X4_BIN)

.PHONY: x4-manifest-test

x4-buffered-input-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/buffered_input_fs
	SH_X4_BUFFERED_INPUT_TEST=1 SH_FS_DIR=$(X4_BUILD)/buffered_input_fs ./$(X4_BIN)

.PHONY: x4-buffered-input-test

x4-route-geometry-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/route_geometry_fs $(X4_BUILD)/follow_fs
	SH_X4_ROUTE_GEOMETRY_TEST=1 SH_FS_DIR=$(X4_BUILD)/route_geometry_fs ./$(X4_BIN)
	SH_X4_FOLLOW_TEST=1 SH_FS_DIR=$(X4_BUILD)/follow_fs ./$(X4_BIN)

x4-delivery-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/delivery_fs
	SH_TIME_SCALE=1 SH_X4_TEST=delivery SH_FS_DIR=$(X4_BUILD)/delivery_fs ./$(X4_BIN)

x4-watch-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/watch_fs $(X4_BUILD)/follow_fs
	SH_TIME_SCALE=1 SH_X4_TEST=watch SH_X4_WATCH_DIR=$(X4_BUILD)/watch SH_FS_DIR=$(X4_BUILD)/watch_fs ./$(X4_BIN)
	SH_TIME_SCALE=1 SH_X4_FOLLOW_TEST=1 SH_FS_DIR=$(X4_BUILD)/follow_fs ./$(X4_BIN)

standby-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/standby_fs
	SH_FS_DIR=$(X4_BUILD)/standby_fs SH_X4_STANDBY_TEST=1 $(X4_BIN)

x4-dispatch-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/dispatch_fs
	SH_FS_DIR=$(X4_BUILD)/dispatch_fs SH_X4_DISPATCH_TEST=1 $(X4_BIN)

.PHONY: x4-dispatch-test

x4-merchant-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/merchant_fs $(X4_BUILD)/merchant_standby_fs $(X4_BUILD)/merchant_return_fs
	SH_FS_DIR=$(X4_BUILD)/merchant_fs SH_X4_MERCHANT_DIR=$(abspath $(X4_BUILD)/merchant) SH_X4_MERCHANT_TEST=1 $(X4_BIN)
	SH_FS_DIR=$(X4_BUILD)/merchant_standby_fs SH_X4_MERCHANT_STANDBY_TEST=1 $(X4_BIN)
	SH_FS_DIR=$(X4_BUILD)/merchant_return_fs SH_X4_MERCHANT_RETURN_DIR=$(abspath $(X4_BUILD)/merchant) SH_X4_MERCHANT_RETURN_TEST=1 $(X4_BIN)

.PHONY: x4-merchant-test

x4-watch-journey-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/watch_journey_fs
	SH_FS_DIR=$(X4_BUILD)/watch_journey_fs SH_X4_WATCH_JOURNEY_DIR=$(abspath $(X4_BUILD)/watch_journey) SH_X4_WATCH_JOURNEY_TEST=1 $(X4_BIN)

.PHONY: x4-watch-journey-test

# The tab/child navigation contract (Back from roots, map port browsing, the
# Company tools round trip).
x4-navigation-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/navigation_fs
	SH_FS_DIR=$(X4_BUILD)/navigation_fs SH_X4_NAVIGATION_DIR=$(abspath $(X4_BUILD)/navigation) SH_X4_NAVIGATION_TEST=1 $(X4_BIN)

.PHONY: x4-navigation-test

x4-startup-input-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/startup_input_fs
	SH_FS_DIR=$(X4_BUILD)/startup_input_fs SH_X4_STARTUP_INPUT_TEST=1 $(X4_BIN)

.PHONY: x4-startup-input-test

x4-growth-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/growth_fs
	SH_FS_DIR=$(X4_BUILD)/growth_fs SH_X4_GROWTH_TEST=1 $(X4_BIN)

x4-return-test: $(X4_BIN)
	rm -rf $(X4_RETURN_FS)
	SH_FS_DIR=$(X4_RETURN_FS) SH_X4_RETURN_DIR=$(X4_RETURN_DIR) SH_X4_RETURN_TEST=1 $(X4_BIN)
	rm -rf $(X4_RETURN_FS)_title && cp -R $(X4_RETURN_FS) $(X4_RETURN_FS)_title
	SH_FS_DIR=$(X4_RETURN_FS)_title SH_X4_RETURN_DIR=$(X4_RETURN_DIR) SH_X4_RETURN_TITLE_TEST=1 $(X4_BIN)
	SH_FS_DIR=$(X4_RETURN_FS) SH_X4_RETURN_DIR=$(X4_RETURN_DIR) SH_X4_RETURN_BOOT_TEST=1 $(X4_BIN)

.PHONY: x4-return-test

power-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/power_fs
	SH_POWER_TEST=off SH_BATTERY=2 SH_FS_DIR=$(X4_BUILD)/power_fs ./$(X4_BIN)
	SH_POWER_TEST=warn SH_BATTERY=9 SH_FS_DIR=$(X4_BUILD)/power_fs ./$(X4_BIN)

# A save written by the version-6 build must still resume (fixtures/save_v6
# was captured with the X4 self-test before the format moved to 7).
load-test: $(X4_BIN)
	rm -rf $(X4_BUILD)/load_fs && cp -R fixtures/save_v6 $(X4_BUILD)/load_fs
	SH_LOAD_TEST=1 SH_FS_DIR=$(X4_BUILD)/load_fs ./$(X4_BIN)

test: $(X4_BIN)
	rm -rf $(X4_BUILD)/save_test_fs
	SH_SAVE_TEST=1 SH_FS_DIR=$(X4_BUILD)/save_test_fs ./$(X4_BIN)

clean:
	rm -rf $(X4_BUILD)

-include $(X4_OBJ:.o=.d)
.PHONY: all deps check-lgfx play-x4 shot-x4 gallery-x4 x4-test x4-nav-test x4-growth-test x4-route-geometry-test x4-delivery-test x4-watch-test load-test power-test standby-test test clean
