
CC = clang
CXX = clang++
SRCDIR = ../source

INCLUDES = -I psx -I include -I . -I $(SRCDIR)/include -I $(SRCDIR)

# AddressSanitizer support: enable with 'make ASAN=1'
ifeq ($(ASAN),1)
SANITIZE = -fsanitize=address -fno-omit-frame-pointer
else
SANITIZE =
endif

CFLAGS = -Wno-everything -Wno-error -O0 -g -DINTEGRAL -include psx/port_overrides.h $(INCLUDES) \
		 -Wno-int-conversion -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
		 -Wno-incompatible-pointer-types $(SANITIZE)
CXXFLAGS = -w -O1 -g -I imgui -I imgui/backends $(SANITIZE)
SDL_CFLAGS = $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs) -lm -lc++ $(SANITIZE)

OBJDIR = obj
TARGET = mgs

# Files that can't compile due to MIPS asm or PSX-specific constructs
EXCLUDE_FILES = \
	$(SRCDIR)/game/movie.c \
	$(SRCDIR)/takabe/vib_edit.c \
	$(SRCDIR)/game/script.c \
	$(SRCDIR)/enemy/merylaction.c \
	$(SRCDIR)/enemy/merylcheck.c

all: $(TARGET)

# --- Discover all compilable source files ---
# Exclude snake_vr (VR disc variant, duplicates snake/) and stage/stagevr (per-overlay)
GAME_ALL_SRC = $(shell find $(SRCDIR)/game $(SRCDIR)/chara $(SRCDIR)/enemy \
	$(SRCDIR)/weapon $(SRCDIR)/bullet $(SRCDIR)/equip $(SRCDIR)/anime \
	$(SRCDIR)/thing $(SRCDIR)/font $(SRCDIR)/menu $(SRCDIR)/animal \
	$(SRCDIR)/kojo $(SRCDIR)/okajima $(SRCDIR)/onoda $(SRCDIR)/takabe \
	$(SRCDIR)/contrib $(SRCDIR)/sound -name "*.c" -not -path "*/snake_vr/*" -not -path "*/torture/*" | sort)

GAME_SRC = $(filter-out $(EXCLUDE_FILES), $(GAME_ALL_SRC))
GAME_OBJ = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/src_%.o,$(GAME_SRC))

# --- Engine libraries (from source, unmodified) ---
LIBGV_SRC = $(wildcard $(SRCDIR)/libgv/*.c)
LIBGV_OBJ = $(patsubst $(SRCDIR)/libgv/%.c,$(OBJDIR)/libgv_%.o,$(LIBGV_SRC))

# Patched libgcl (64-bit pointer fix in parse.c/expr.c)
LIBGCL_SRC = $(wildcard libgcl_fix/*.c)
LIBGCL_OBJ = $(patsubst libgcl_fix/%.c,$(OBJDIR)/libgcl_%.o,$(LIBGCL_SRC))

LIBHZD_SRC = $(wildcard $(SRCDIR)/libhzd/*.c)
LIBHZD_OBJ = $(patsubst $(SRCDIR)/libhzd/%.c,$(OBJDIR)/libhzd_%.o,$(LIBHZD_SRC))

# --- All stage definition files ---
STAGE_NAMES = abst brf camera change d00a d01a d03a d11c d16e d18a d18ar \
	demosel ending endingr opening option preope rank roll \
	s00a s01a s02a s02b s02c s02d s02e \
	s03a s03ar s03b s03c s03d s03dr s03e s03er \
	s04a s04b s04br s04c s05a s06a \
	s07a s07b s07br s07c s07cr \
	s08a s08b s08br s08c s08cr \
	s09a s09ar s10a s10ar \
	s11a s11b s11c s11d s11e s11g s11h s11i \
	s12a s12b s12c s13a s14e \
	s15a s15b s15c s16a s16b s16c s16d \
	s17a s17ar s18a s18ar \
	s19a s19ar s19b s19br \
	s20a s20ar \
	select select1 select2 select3 select4 selectd \
	sound title

STAGE_OBJ = $(patsubst %,$(OBJDIR)/stage_%.o,$(STAGE_NAMES))

# --- All overlay source files ---
# Exclude: files that don't compile, and R-variant overlays (duplicate symbols with base)
OVERLAY_EXCLUDE = $(SRCDIR)/overlays/s12c/takabe/libdg2.c \
	$(SRCDIR)/overlays/s19b/menu/countdwn.c \
	$(shell find $(SRCDIR)/overlays/d18ar $(SRCDIR)/overlays/s08br $(SRCDIR)/overlays/s19br -name "*.c" 2>/dev/null)
OVERLAY_ALL_SRC = $(shell find $(SRCDIR)/overlays -name "*.c" | sort)
OVERLAY_SRC = $(filter-out $(OVERLAY_EXCLUDE), $(OVERLAY_ALL_SRC))
OVERLAY_OBJ = $(patsubst $(SRCDIR)/overlays/%.c,$(OBJDIR)/ovl_%.o,$(OVERLAY_SRC))

# --- Port sources ---
PORT_OBJ = \
	$(OBJDIR)/main.o \
	$(OBJDIR)/main_game.o \
	$(OBJDIR)/imgui_debug.o \
	$(OBJDIR)/imgui.o \
	$(OBJDIR)/imgui_draw.o \
	$(OBJDIR)/imgui_tables.o \
	$(OBJDIR)/imgui_widgets.o \
	$(OBJDIR)/imgui_impl_sdl2.o \
	$(OBJDIR)/imgui_impl_sdlrenderer2.o \
	$(OBJDIR)/port_memory.o \
	$(OBJDIR)/extern_stubs.o \
	$(OBJDIR)/game_stubs.o \
	$(OBJDIR)/asm_stubs.o \
	$(OBJDIR)/gte_math.o \
	$(OBJDIR)/gpu_stubs.o \
	$(OBJDIR)/mts.o \
	$(OBJDIR)/libdg_stub.o \
	$(OBJDIR)/vram.o \
	$(OBJDIR)/pcx_loader.o \
	$(OBJDIR)/kmd_loader.o \
	$(OBJDIR)/libdg_bound.o \
	$(OBJDIR)/libdg_chanl.o \
	$(OBJDIR)/libdg_display.o \
	$(OBJDIR)/libdg_divide.o \
	$(OBJDIR)/libdg_light.o \
	$(OBJDIR)/libdg_matrix.o \
	$(OBJDIR)/libdg_obj.o \
	$(OBJDIR)/libdg_opack.o \
	$(OBJDIR)/libdg_palette.o \
	$(OBJDIR)/libdg_prim.o \
	$(OBJDIR)/libdg_pshade.o \
	$(OBJDIR)/libdg_screen.o \
	$(OBJDIR)/libdg_shade.o \
	$(OBJDIR)/libdg_text.o \
	$(OBJDIR)/libfs.o \
	$(OBJDIR)/spu_emu.o \
	$(OBJDIR)/memcard_stub.o \
	$(OBJDIR)/hzd_loader.o \
	$(OBJDIR)/game_script_fix.o \
	$(OBJDIR)/link_stubs.o \
	$(OBJDIR)/test_server.o

BSS_OBJ = $(OBJDIR)/bss.o

ALL_OBJ = $(PORT_OBJ) $(STAGE_OBJ) $(OVERLAY_OBJ) $(LIBGV_OBJ) $(LIBGCL_OBJ) $(LIBHZD_OBJ) $(BSS_OBJ) $(GAME_OBJ)

$(TARGET): $(ALL_OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)
	codesign -s - --entitlements entitlements.plist -f $@ 2>/dev/null || true

# --- ImGui rules ---
$(OBJDIR)/imgui.o: imgui/imgui.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<
$(OBJDIR)/imgui_draw.o: imgui/imgui_draw.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<
$(OBJDIR)/imgui_tables.o: imgui/imgui_tables.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<
$(OBJDIR)/imgui_widgets.o: imgui/imgui_widgets.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<
$(OBJDIR)/imgui_impl_sdl2.o: imgui/backends/imgui_impl_sdl2.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<
$(OBJDIR)/imgui_impl_sdlrenderer2.o: imgui/backends/imgui_impl_sdlrenderer2.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<
$(OBJDIR)/imgui_debug.o: imgui_debug.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(INCLUDES) -c -o $@ $<

# --- Port source rules ---
$(OBJDIR)/main.o: main.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/main_game.o: main_game.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/port_memory.o: port_memory.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/extern_stubs.o: extern_stubs.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/game_stubs.o: game_stubs.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/asm_stubs.o: asm_stubs.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/gte_math.o: psx/gte_math.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/gpu_stubs.o: psx/gpu_stubs.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/mts.o: mts/mts.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/libdg_stub.o: libdg/libdg_stub.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/vram.o: libdg/vram.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/pcx_loader.o: libdg/pcx_loader.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/kmd_loader.o: libdg/kmd_loader.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Original libdg source files — need -include of patched libdg.h to fix enum conflict
LIBDG_CFLAGS = $(CFLAGS) -include $(CURDIR)/libdg/libdg.h

$(OBJDIR)/libdg_%.o: $(SRCDIR)/libdg/%.c | $(OBJDIR)
	$(CC) $(LIBDG_CFLAGS) -c -o $@ $<

$(OBJDIR)/libfs.o: libfs/libfs.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<


$(OBJDIR)/spu_emu.o: sound/spu_emu.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/memcard_stub.o: memcard/memcard_stub.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/hzd_loader.o: libhzd/hzd_loader.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Stage definition files (each gets unique _StageCharacterEntries symbol) ---
define STAGE_RULE
$(OBJDIR)/stage_$(1).o: $(SRCDIR)/stage/$(1).c | $(OBJDIR)
	$(CC) $(CFLAGS) -D_StageCharacterEntries=_StageCharacterEntries_$(1) -c -o $$@ $$<
endef
$(foreach s,$(STAGE_NAMES),$(eval $(call STAGE_RULE,$(s))))

# --- Overlay source files ---
$(OBJDIR)/ovl_%.o: $(SRCDIR)/overlays/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/game_script_fix.o: game_script_fix.c | $(OBJDIR)
	$(CC) $(CFLAGS) -I libgcl_fix -c -o $@ $<

$(OBJDIR)/link_stubs.o: link_stubs.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/test_server.o: test_server.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Original source rules ---
$(OBJDIR)/libgv_%.o: $(SRCDIR)/libgv/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/libgcl_%.o: libgcl_fix/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -iquote libgcl_fix -c -o $@ $<

$(OBJDIR)/libhzd_%.o: $(SRCDIR)/libhzd/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/bss.o: $(SRCDIR)/data/bss.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Game source files — flatten directory structure in obj names
$(OBJDIR)/src_%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
