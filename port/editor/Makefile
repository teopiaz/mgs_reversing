# MGS Stage Editor — links against pre-built port/obj/*.o
#
# Usage:
#   cd port/editor
#   make            # builds the editor (builds the port first if needed)
#   ./editor PORT_ISO=/path/to/MGS.bin   # or pass --iso <path>
#
# The editor is a separate binary from the main port. It reuses the existing
# port build artifacts (libdg, libfs, libgv, libhzd, imgui, gl_renderer) but
# excludes main.o, main_game.o and imgui_debug.o — those are replaced by
# editor's own main + imgui setup.

UNAME_S := $(shell uname -s)
CC      := clang
CXX     := clang++
PORT    := ..
SRCDIR  := ../../source
OBJDIR  := obj

INCLUDES := -I $(PORT)/psx -I $(PORT)/include -I $(PORT) -I $(SRCDIR)/include -I $(SRCDIR) -I $(PORT)/imgui -I $(PORT)/imgui/backends

CFLAGS  := -Wno-everything -Wno-error -O0 -g -fcommon -DINTEGRAL -DPORT_BUILD \
           -include $(PORT)/psx/port_overrides.h $(INCLUDES) \
           -Wno-int-conversion -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
           -Wno-incompatible-pointer-types
CXXFLAGS := -w -O1 -g -std=c++17 $(INCLUDES)
SDL_CFLAGS := $(shell sdl2-config --cflags)

ifeq ($(UNAME_S),Darwin)
    OPENGL_LIB := -framework OpenGL
    CODESIGN   := codesign -s - --entitlements $(PORT)/entitlements.plist -f $@ 2>/dev/null || true
else
    OPENGL_LIB := -lGL
    CODESIGN   := true
endif

LDFLAGS := $(shell sdl2-config --libs) -lm -lc++ $(OPENGL_LIB)

TARGET := editor

# --- Editor sources ---
EDITOR_C_SRC   := main.c editor_stubs.c ed_loader.c ed_camera.c ed_render.c ed_hzd.c ed_actors.c ed_demo.c
EDITOR_CXX_SRC := ed_ui.cpp
EDITOR_C_OBJ   := $(patsubst %.c,$(OBJDIR)/%.o,$(EDITOR_C_SRC))
EDITOR_CXX_OBJ := $(patsubst %.cpp,$(OBJDIR)/%.o,$(EDITOR_CXX_SRC))
EDITOR_OBJ     := $(EDITOR_C_OBJ) $(EDITOR_CXX_OBJ)

# --- Port objects to reuse ---
# Build the port first so port/obj/*.o exists, then link everything except
# main.o, main_game.o, imgui_debug.o (replaced by editor) and test_server.o
# (game-loop helper not needed by editor).
PORT_OBJ_DIR := $(PORT)/obj
PORT_OBJ_EXCLUDE := main.o imgui_debug.o test_server.o
PORT_OBJ_ALL := $(shell find $(PORT_OBJ_DIR) -name '*.o' -type f 2>/dev/null | sort)
PORT_OBJ := $(filter-out $(addprefix $(PORT_OBJ_DIR)/,$(PORT_OBJ_EXCLUDE)), $(PORT_OBJ_ALL))

all: $(TARGET)

# Ensure the port has been built so port/obj/*.o exists.
$(PORT)/mgs:
	$(MAKE) -C $(PORT)

$(TARGET): $(PORT)/mgs $(EDITOR_OBJ)
	$(CXX) -o $@ $(EDITOR_OBJ) $(PORT_OBJ) $(LDFLAGS)
	$(CODESIGN)

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
