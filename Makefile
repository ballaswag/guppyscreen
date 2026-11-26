#
# Makefile
#
ifdef CROSS_COMPILE
CC 	= $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
CPP = $(CC) -E
AS 	= $(CROSS_COMPILE)as
LD	= $(CROSS_COMPILE)ld
AR	= $(CROSS_COMPILE)ar
NM	= $(CROSS_COMPILE)nm
STRIP 	= $(CROSS_COMPILE)strip
OBJCOPY 	= $(CROSS_COMPILE)objcopy
endif

STRIP ?= strip

LVGL_DIR_NAME 	?= lvgl
LVGL_DIR 		?= .

WARNINGS		:= -Wall -Wextra -Wno-unused-function -Wno-error=strict-prototypes -Wpointer-arith \
					-fno-strict-aliasing -Wno-error=cpp -Wuninitialized -Wmaybe-uninitialized -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess \
					-Wno-format-nonliteral -Wno-cast-qual -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security -Wno-error=pedantic \
					-Wno-sign-compare -Wdouble-promotion -Wclobbered -Wempty-body -Wtype-limits -Wshift-negative-value \
					-Wno-unused-value -Wno-unused-parameter -Wno-missing-field-initializers -Wuninitialized -Wmaybe-uninitialized -Wall -Wextra -Wno-unused-parameter \
					-Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wpointer-arith -Wno-cast-qual \
					-Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security -Wno-sign-compare
CFLAGS 			?= -O3 -g0 -MD -MP -I$(LVGL_DIR)/ $(WARNINGS)
DEPS_KEY		?= $(if $(CROSS_COMPILE),$(patsubst %-,%,$(CROSS_COMPILE)),native)
DEPS_DIR		?= build/deps/$(DEPS_KEY)
DEPS_LIB_DIR	= $(DEPS_DIR)/lib
LIBHV_A			= $(DEPS_LIB_DIR)/libhv.a
WPA_CLIENT_A	= $(DEPS_LIB_DIR)/libwpa_client.a
LIBHV_INPUTS	:= $(shell find libhv \( -path libhv/include -o -path libhv/lib \) -prune -o -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name 'Makefile*' -o -name 'config.mk' \) -print)
WPA_CLIENT_INPUTS := $(shell find wpa_supplicant -type f \( -name '*.c' -o -name '*.h' -o -name 'Makefile*' -o -name '.config' \))
LDFLAGS 		?= -static -lm -L$(DEPS_LIB_DIR) -l:libhv.a -latomic -lpthread -L$(DEPS_LIB_DIR) -l:libwpa_client.a -lstdc++fs
BIN 			= grumpyscreen
BUILD_DIR 		?= ./build
BUILD_OBJ_DIR 	= $(BUILD_DIR)/obj
BUILD_BIN_DIR 	= $(BUILD_DIR)/bin

prefix 			?= /usr
bindir 			?= $(prefix)/bin

#Collect the files to compile
MAINSRC = 		$(wildcard $(LVGL_DIR)/src/*.cpp)

# Vendor drivers for the MMU panel, one file each. A build carries only the
# ones named here; Klipper's objects select the backend at runtime. Leave it
# empty for a build with the panel but no vendor support:
#   make MMU_BACKENDS="afc hh"  AFC and Happy Hare (the default)
#   make MMU_BACKENDS="afc"     just AFC
#   make MMU_BACKENDS=""        no vendor driver at all
MMU_BACKENDS 	?= afc hh
MMU_BACKEND_SRC = $(foreach b,$(MMU_BACKENDS),$(LVGL_DIR)/src/$(b)_backend.cpp)
MAINSRC 		:= $(filter-out $(filter %_backend.cpp,$(MAINSRC)),$(MAINSRC)) $(MMU_BACKEND_SRC)
MMU_BACKEND_DEFS = $(foreach b,$(MMU_BACKENDS),-D MMU_BACKEND_$(shell echo $(b) | tr 'a-z' 'A-Z'))
DEFINES 		+= $(MMU_BACKEND_DEFS)

# The Moonraker update_manager client only exists in a COSMOS build.
ifndef COSMOS
MAINSRC 		:= $(filter-out $(LVGL_DIR)/src/update_manager_client.cpp,$(MAINSRC))
endif

include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk
CSRCS			:= $(filter-out $(LVGL_DIR)/lv_drivers/wayland/%.c,$(CSRCS))

CSRCS 			+= $(wildcard $(LVGL_DIR)/assets/*.c)
ifdef COSMOS
CSRCS			:= $(filter-out $(LVGL_DIR)/assets/dejavusans_mono_hh_14.c,$(CSRCS))
endif
ifdef GUPPY_CALIBRATE
CSRCS			+= $(wildcard $(LVGL_DIR)/lv_touch_calibration/*.c)
DEFINES += -D GUPPY_CALIBRATE
DEFINES += -D EVDEV_CALIBRATE
endif

ASSET_DIR		= material
ifdef GUPPY_SMALL_SCREEN
ASSET_DIR		= material_46
DEFINES			+= -D GUPPY_SMALL_SCREEN
endif

CSRCS 			+= $(wildcard $(LVGL_DIR)/assets/$(ASSET_DIR)/*.c)

ifdef GUPPYSCREEN_VERSION
DEFINES			+= -D GUPPYSCREEN_VERSION=\"$(GUPPYSCREEN_VERSION)\"
else
DEFINES			+= -D GUPPYSCREEN_VERSION=\"unknown\"
endif

ifdef GUPPYSCREEN_BRANCH
DEFINES			+= -D GUPPYSCREEN_BRANCH=\"$(GUPPYSCREEN_BRANCH)\"
else
DEFINES			+= -D GUPPYSCREEN_BRANCH=\"unknown\"
endif

ifdef COSMOS
  DEFINES	    += -D COSMOS='"$(COSMOS)"'

  UPDATE_TEXT    ?= Update\nCOSMOS
  UPDATE_TITLE   ?= $(subst \n, ,$(UPDATE_TEXT))
  UPDATE_PROMPT  ?= Are you sure you want to update COSMOS?\n\nThis will download and update to the latest version of COSMOS!
  UPDATE_FAILURE	?= Failed to initiate update COSMOS!
  UPDATE_SUCCESS	?= Your printer will restart shortly!

  DEFINES	    += -D UPDATE_BUTTON_TEXT='"$(UPDATE_TEXT)"'
  DEFINES	    += -D UPDATE_BUTTON_TITLE='"$(UPDATE_TITLE)"'
  DEFINES	    += -D UPDATE_BUTTON_PROMPT='"$(UPDATE_PROMPT)"'
  DEFINES	    += -D UPDATE_BUTTON_FAILURE='"$(UPDATE_FAILURE)"'
  DEFINES	    += -D UPDATE_BUTTON_SUCCESS='"$(UPDATE_SUCCESS)"'

  SWITCH_TO_STOCK_TEXT    ?= Switch to OC\nPatched
  SWITCH_TO_STOCK_TITLE   ?= $(subst \n, ,$(SWITCH_TO_STOCK_TEXT))
  SWITCH_TO_STOCK_PROMPT  ?= Are you sure you want to switch to OpenCentauri patched firmware?\n\nThis will take some time, **DO NOT TURN OFF YOUR PRINTER**, just wait for it to reboot.
  SWITCH_TO_STOCK_FAILURE	?= Failed to initiate switch to OC Patched!
  SWITCH_TO_STOCK_SUCCESS	?= Your printer will restart shortly!

  FACTORY_RESET_TEXT    ?= Factory\nReset
  FACTORY_RESET_TITLE   ?= $(subst \n, ,$(FACTORY_RESET_TEXT))
  FACTORY_RESET_PROMPT  ?= Are you sure you want factory reset?\n\nThis will reset all printer setting but it will stay using COSMOS, it will not switch back to stock.
  FACTORY_RESET_FAILURE	?= Failed to initiate factory reset!
  FACTORY_RESET_SUCCESS	?= Your printer will restart shortly!
else
  SWITCH_TO_STOCK_TEXT    ?= Switch to\nStock
  SWITCH_TO_STOCK_TITLE   ?= $(subst \n, ,$(SWITCH_TO_STOCK_TEXT))
  SWITCH_TO_STOCK_PROMPT  ?= Are you sure you want to switch to stock?\n\nThis will temporarily switch the printer to stock creality firmware!
  SWITCH_TO_STOCK_FAILURE	?= Failed to initiate switch to stock!
  SWITCH_TO_STOCK_SUCCESS	?= Please power cycle your printer!\nPlease wait for the stock screen to appear!

  FACTORY_RESET_TEXT    ?= Factory\nReset
  FACTORY_RESET_TITLE   ?= $(subst \n, ,$(FACTORY_RESET_TEXT))
  FACTORY_RESET_PROMPT  ?= Are you sure you want to factory reset?\n\nThis will reset the printer to stock creality firmware!
  FACTORY_RESET_FAILURE	?= Failed to initiate factory reset!
  FACTORY_RESET_SUCCESS	?= Your printer will restart shortly!\nPlease wait for the stock screen to appear!
endif

DEFINES	    += -D FACTORY_RESET_BUTTON_TEXT='"$(FACTORY_RESET_TEXT)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_TITLE='"$(FACTORY_RESET_TITLE)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_PROMPT='"$(FACTORY_RESET_PROMPT)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_FAILURE='"$(FACTORY_RESET_FAILURE)"'
DEFINES	    += -D FACTORY_RESET_BUTTON_SUCCESS='"$(FACTORY_RESET_SUCCESS)"'

DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_TEXT='"$(SWITCH_TO_STOCK_TEXT)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_TITLE='"$(SWITCH_TO_STOCK_TITLE)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_PROMPT='"$(SWITCH_TO_STOCK_PROMPT)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_FAILURE='"$(SWITCH_TO_STOCK_FAILURE)"'
DEFINES	    += -D SWITCH_TO_STOCK_BUTTON_SUCCESS='"$(SWITCH_TO_STOCK_SUCCESS)"'

OBJEXT 			?= .o

AOBJS 			= $(ASRCS:.S=$(OBJEXT))
COBJS 			= $(CSRCS:.c=$(OBJEXT))

MAINOBJ 		= $(MAINSRC:.cpp=$(OBJEXT))
DEPS                    = $(addprefix $(BUILD_OBJ_DIR)/, $(patsubst %.o, %.d, $(MAINOBJ)))

OBJS 			= $(AOBJS) $(COBJS) $(MAINOBJ)
TARGET 			= $(addprefix $(BUILD_OBJ_DIR)/, $(patsubst ./%, %, $(OBJS)))

INC 				:= -I./ -I./lvgl/ -I./lv_touch_calibration -I./fmt/include -I$(DEPS_DIR)/include -I./wpa_supplicant/src/common
LDLIBS	 			:= -lm

DEFINES				+= -D _GNU_SOURCE -DSPDLOG_COMPILED_LIB

ifdef GUPPY_SDL
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
ifeq ($(strip $(SDL_LIBS)),)
SDL_LIBS := -lSDL2
endif
LDFLAGS				:= $(filter-out -static,$(LDFLAGS))
INC					+= $(SDL_CFLAGS)
LDFLAGS				+= $(SDL_LIBS)
DEFINES				+= -D GUPPY_SDL -D USE_SDL=1
# SDL_RES=<w>x<h> sizes the desktop window explicitly, for checking a layout at
# a resolution no board ships yet; without it the window is the panel the build
# is for.
ifdef SDL_RES
DEFINES				+= -D SDL_HOR_RES=$(word 1,$(subst x, ,$(SDL_RES))) -D SDL_VER_RES=$(word 2,$(subst x, ,$(SDL_RES)))
else ifdef GUPPY_SMALL_SCREEN
DEFINES				+= -D SDL_HOR_RES=480 -D SDL_VER_RES=272
else
DEFINES				+= -D SDL_HOR_RES=800 -D SDL_VER_RES=480
endif
else
# the framebuffer drives the display and blanks it for the screen timeout
# wherever there is no SDL window (the Android app has its own build)
DEFINES				+= -D GUPPY_FBDEV
endif

COMPILE_CC				= $(CC) $(CFLAGS) $(INC) $(DEFINES)
COMPILE_CXX				= $(CC) $(CFLAGS) $(INC) $(DEFINES)

## MAINOBJ -> OBJFILES

all: default

libhv.a: $(LIBHV_A)

wpaclient: $(WPA_CLIENT_A)

$(LIBHV_A): $(LIBHV_INPUTS)
	$(MAKE) -C libhv clean
	$(MAKE) -C libhv -j$$(nproc) libhv
	@mkdir -p $(DEPS_LIB_DIR)
	@cp libhv/lib/libhv.a $(LIBHV_A)
	@cp -r libhv/include $(DEPS_DIR)/

$(WPA_CLIENT_A): $(WPA_CLIENT_INPUTS)
	$(MAKE) -C wpa_supplicant/wpa_supplicant clean
	$(MAKE) -C wpa_supplicant/wpa_supplicant -j$$(nproc) libwpa_client.a
	@mkdir -p $(DEPS_LIB_DIR)
	@cp wpa_supplicant/wpa_supplicant/libwpa_client.a $(WPA_CLIENT_A)

$(BUILD_OBJ_DIR)/%.o: %.cpp | $(LIBHV_A)
	@mkdir -p $(dir $@)
	@$(COMPILE_CXX) -std=c++17 $(CFLAGS) -c $< -o $@
	@echo "CXX $<"

$(BUILD_OBJ_DIR)/%.o: %.c | $(LIBHV_A)
	@mkdir -p $(dir $@)
	@$(COMPILE_CC)  $(CFLAGS) -c $< -o $@
	@echo "CC $<"

default: libhv.a wpaclient $(TARGET)
	@mkdir -p $(dir $(BUILD_BIN_DIR)/)
	$(CXX) -o $(BUILD_BIN_DIR)/$(BIN) $(TARGET) $(LDFLAGS) $(LDLIBS)
	@echo "CXX $<"
	@$(STRIP) $(BUILD_BIN_DIR)/grumpyscreen

libhvclean:
	$(MAKE) -C libhv clean
	rm -f $(LIBHV_A)
	rm -rf $(DEPS_DIR)/include

wpaclean:
	$(MAKE) -C wpa_supplicant/wpa_supplicant clean
	rm -f $(WPA_CLIENT_A)

clean:
	rm -rf $(BUILD_DIR)

CONSOLE_FONT_TEST_SRCS = assets/dejavusans_mono_14.c \
 lvgl/src/font/lv_font.c lvgl/src/font/lv_font_fmt_txt.c \
 lvgl/src/misc/lv_mem.c lvgl/src/misc/lv_utils.c lvgl/src/misc/lv_gc.c
ifndef COSMOS
CONSOLE_FONT_TEST_SRCS += assets/dejavusans_mono_hh_14.c
endif
CONSOLE_FONT_TEST_OBJS = $(addprefix $(BUILD_OBJ_DIR)/,$(CONSOLE_FONT_TEST_SRCS:.c=.o))

.PHONY: test-console-font
test-console-font: $(CONSOLE_FONT_TEST_OBJS)
	$(CXX) -std=c++17 $(INC) $(DEFINES) tests/test_console_font.cpp $(CONSOLE_FONT_TEST_OBJS) -o $(BUILD_DIR)/test_console_font
	$(BUILD_DIR)/test_console_font

LVGL_TEST_OBJS = $(filter-out $(BUILD_OBJ_DIR)/lv_drivers/%.o,$(filter $(BUILD_OBJ_DIR)/lvgl/%.o $(BUILD_OBJ_DIR)/lv_%.o,$(TARGET)))
.PHONY: test-notification-banner
test-notification-banner: $(LVGL_TEST_OBJS) $(BUILD_OBJ_DIR)/src/notification_banner.o
	@$(CXX) -std=c++17 -I./src $(INC) $(DEFINES) tests/test_notification_banner.cpp $(BUILD_OBJ_DIR)/src/notification_banner.o $(LVGL_TEST_OBJS) -lm -o $(BUILD_DIR)/test_notification_banner
	$(BUILD_DIR)/test_notification_banner

MMU_TEST_OBJS = $(filter-out $(BUILD_OBJ_DIR)/src/main.o,$(TARGET))
.PHONY: test-mmu-lifecycle
test-mmu-lifecycle: libhv.a wpaclient $(MMU_TEST_OBJS)
	@$(CXX) -std=c++17 -I./src $(INC) $(DEFINES) tests/test_mmu_lifecycle.cpp $(filter-out $(BUILD_OBJ_DIR)/src/websocket_client.o,$(MMU_TEST_OBJS)) $(LDFLAGS) $(LDLIBS) -o $(BUILD_DIR)/test_mmu_lifecycle
	$(BUILD_DIR)/test_mmu_lifecycle
	$(BUILD_DIR)/test_mmu_lifecycle --startup
	$(BUILD_DIR)/test_mmu_lifecycle --spoolman
	$(BUILD_DIR)/test_mmu_lifecycle --small
	$(BUILD_DIR)/test_mmu_lifecycle --spoolman-small

.PHONY: test-prompt-panel
test-prompt-panel: libhv.a wpaclient $(MMU_TEST_OBJS)
	@$(CXX) -std=c++17 -I./src $(INC) $(DEFINES) tests/test_prompt_panel.cpp $(MMU_TEST_OBJS) $(LDFLAGS) $(LDLIBS) -o $(BUILD_DIR)/test_prompt_panel
	$(BUILD_DIR)/test_prompt_panel
	$(BUILD_DIR)/test_prompt_panel --phone
	$(BUILD_DIR)/test_prompt_panel --tablet

.PHONY: test-print-panel
test-print-panel: libhv.a wpaclient $(MMU_TEST_OBJS)
	@$(CXX) -std=c++17 -I./src $(INC) $(DEFINES) tests/test_print_panel.cpp $(filter-out $(BUILD_OBJ_DIR)/src/websocket_client.o,$(MMU_TEST_OBJS)) $(LDFLAGS) $(LDLIBS) -o $(BUILD_DIR)/test_print_panel
	$(BUILD_DIR)/test_print_panel
	$(BUILD_DIR)/test_print_panel --small

test: libhv.a test-console-font test-notification-banner test-mmu-lifecycle test-prompt-panel test-print-panel
	@mkdir -p $(BUILD_DIR)
	g++ -std=gnu++17 -O2 -I./src -I$(DEPS_DIR)/include/ tests/test_config.cpp -o $(BUILD_DIR)/test_config
	$(BUILD_DIR)/test_config
	g++ -std=gnu++17 -O2 -I./src tests/test_console_colour.cpp src/console_colour.cpp -o $(BUILD_DIR)/test_console_colour
	$(BUILD_DIR)/test_console_colour
# the backend fixture stubs State and the websocket client, so it links
# only the backend under test (libhv for the client's base class)
	g++ -std=gnu++17 -O2 -I./src -I./fmt/include -Ilibhv/include/ $(MMU_BACKEND_DEFS) $(MMU_BACKEND_SRC) \
		tests/test_backends.cpp src/mmu_tools.cpp src/notify_consumer.cpp -o $(BUILD_DIR)/test_backends \
		-L$(DEPS_LIB_DIR) -l:libhv.a -lpthread
	$(BUILD_DIR)/test_backends

-include			$(DEPS)
