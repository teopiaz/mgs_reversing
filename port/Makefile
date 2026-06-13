

UNAME_S := $(shell uname -s)
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

ARCHFLAGS =
ifeq ($(UNAME_S),Darwin)
	ifeq ($(shell uname -m),arm64)
		# SDL2 in /usr/local is x86_64; build x86_64 under Rosetta to match.
		ifeq ($(wildcard /opt/homebrew/lib/libSDL2.dylib),)
			ARCHFLAGS = -arch x86_64
		endif
	endif
endif

CFLAGS = $(ARCHFLAGS) -std=gnu17 -Wno-everything -Wno-error -O0 -g -fcommon -funsigned-char -DINTEGRAL -DPORT_BUILD=1 -include psx/port_overrides.h $(INCLUDES) \
		 -Wno-int-conversion -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
		 -Wno-incompatible-pointer-types $(SANITIZE)
CXXFLAGS = $(ARCHFLAGS) -std=c++17 -w -O1 -g -I imgui -I imgui/backends $(SANITIZE)
SDL_CFLAGS = $(shell sdl2-config --cflags)

ifeq ($(UNAME_S),Darwin)
	OPENGL_LIB = -framework OpenGL
	CODESIGN = codesign -s - --entitlements entitlements.plist -f $@ 2>/dev/null || true
else
	OPENGL_LIB = -lGL
	CODESIGN = true
endif

LDFLAGS = $(ARCHFLAGS) $(shell sdl2-config --libs) -lm -lc++ $(OPENGL_LIB) $(SANITIZE)

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

# Per-subdir port copies for game/* files. Same mechanism as LIBGV_PORT_COPIES:
# list a stem (no extension), the source is filtered out, and the port copy at
# port/<subdir>/<stem>.c is compiled instead. Add new entries for any source/
# file that diverges enough on the port to deserve a full copy (~4+ ifdefs,
# rebase-hot zone, or struct-layout / GTE-light tweaks).
TAKABE_PORT_COPIES = gas_efct
EQUIP_PORT_COPIES  = jpegcam
KOJO_PORT_COPIES   = demothrd demoexec
MENU_PORT_COPIES   = radio datasave
THING_PORT_COPIES  = sight
SOUND_PORT_COPIES  = sd_main sd_file
GAME_PORT_COPIES   = control gamed jimctrl

GAME_SRC = $(filter-out $(EXCLUDE_FILES), $(GAME_ALL_SRC))
GAME_SRC := $(filter-out $(addprefix $(SRCDIR)/takabe/,$(addsuffix .c,$(TAKABE_PORT_COPIES))), $(GAME_SRC))
GAME_SRC := $(filter-out $(addprefix $(SRCDIR)/equip/, $(addsuffix .c,$(EQUIP_PORT_COPIES))),  $(GAME_SRC))
GAME_SRC := $(filter-out $(addprefix $(SRCDIR)/kojo/,  $(addsuffix .c,$(KOJO_PORT_COPIES))),   $(GAME_SRC))
GAME_SRC := $(filter-out $(addprefix $(SRCDIR)/menu/,  $(addsuffix .c,$(MENU_PORT_COPIES))),   $(GAME_SRC))
GAME_SRC := $(filter-out $(addprefix $(SRCDIR)/thing/, $(addsuffix .c,$(THING_PORT_COPIES))),  $(GAME_SRC))
GAME_SRC := $(filter-out $(addprefix $(SRCDIR)/sound/, $(addsuffix .c,$(SOUND_PORT_COPIES))),  $(GAME_SRC))
GAME_SRC := $(filter-out $(addprefix $(SRCDIR)/game/,  $(addsuffix .c,$(GAME_PORT_COPIES))),   $(GAME_SRC))
GAME_OBJ = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/src_%.o,$(GAME_SRC))
GAME_OBJ += $(addprefix $(OBJDIR)/src_takabe/,$(addsuffix .o,$(TAKABE_PORT_COPIES)))
GAME_OBJ += $(addprefix $(OBJDIR)/src_equip/, $(addsuffix .o,$(EQUIP_PORT_COPIES)))
GAME_OBJ += $(addprefix $(OBJDIR)/src_kojo/,  $(addsuffix .o,$(KOJO_PORT_COPIES)))
GAME_OBJ += $(addprefix $(OBJDIR)/src_menu/,  $(addsuffix .o,$(MENU_PORT_COPIES)))
GAME_OBJ += $(addprefix $(OBJDIR)/src_thing/, $(addsuffix .o,$(THING_PORT_COPIES)))
GAME_OBJ += $(addprefix $(OBJDIR)/src_sound/, $(addsuffix .o,$(SOUND_PORT_COPIES)))
GAME_OBJ += $(addprefix $(OBJDIR)/src_game/,  $(addsuffix .o,$(GAME_PORT_COPIES)))

# --- Engine libraries (from source, unmodified) ---
# gvd.c needs the port memory pool (port_normal_memory etc.) instead of the
# hardcoded PSX MEM_ADDR; compile the port copy under port/libgv/ instead.
LIBGV_PORT_COPIES = gvd actor
LIBGV_SRC = $(filter-out $(addprefix $(SRCDIR)/libgv/,$(addsuffix .c,$(LIBGV_PORT_COPIES))), $(wildcard $(SRCDIR)/libgv/*.c))
LIBGV_OBJ = $(patsubst $(SRCDIR)/libgv/%.c,$(OBJDIR)/libgv_%.o,$(LIBGV_SRC))
LIBGV_OBJ += $(addprefix $(OBJDIR)/libgv_,$(addsuffix .o,$(LIBGV_PORT_COPIES)))

# Patched libgcl (64-bit pointer fix in parse.c/expr.c)
LIBGCL_SRC = $(wildcard libgcl_fix/*.c)
LIBGCL_OBJ = $(patsubst libgcl_fix/%.c,$(OBJDIR)/libgcl_%.o,$(LIBGCL_SRC))

# online.c and near.c have port-specific 64-bit adaptations (scratchpad address
# remap + pointer side-channels); compile the fully-adapted port copies under
# port/libhzd/ instead of the pristine upstream sources.
LIBHZD_PORT_COPIES = online near vector trap level hzdd bind
LIBHZD_SRC = $(filter-out $(addprefix $(SRCDIR)/libhzd/,$(addsuffix .c,$(LIBHZD_PORT_COPIES))), $(wildcard $(SRCDIR)/libhzd/*.c))
LIBHZD_OBJ = $(patsubst $(SRCDIR)/libhzd/%.c,$(OBJDIR)/libhzd_%.o,$(LIBHZD_SRC))
LIBHZD_OBJ += $(addprefix $(OBJDIR)/libhzd_,$(addsuffix .o,$(LIBHZD_PORT_COPIES)))

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
	$(SRCDIR)/overlays/d18a/animal/snake18/snake18.c \
	$(SRCDIR)/overlays/s08b/animal/ninja/bunsin2.c \
	$(SRCDIR)/overlays/s11d/chara/rope/rope.c \
	$(SRCDIR)/overlays/s11d/chara/hind/hind.c \
	$(SRCDIR)/overlays/s11d/chara/hind/hind_bul.c \
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
	$(OBJDIR)/imgui_impl_opengl3.o \
	$(OBJDIR)/port_memory.o \
	$(OBJDIR)/extern_stubs.o \
	$(OBJDIR)/game_stubs.o \
	$(OBJDIR)/asm_stubs.o \
	$(OBJDIR)/gte_math.o \
	$(OBJDIR)/gpu_stubs.o \
	$(OBJDIR)/struct_assertions.o \
	$(OBJDIR)/mts.o \
	$(OBJDIR)/libdg_stub.o \
	$(OBJDIR)/vram.o \
	$(OBJDIR)/gl_renderer.o \
	$(OBJDIR)/pcx_loader.o \
	$(OBJDIR)/kmd_loader.o \
	$(OBJDIR)/libdg_bound.o \
	$(OBJDIR)/libdg_chanl.o \
	$(OBJDIR)/libdg_display.o \
	$(OBJDIR)/libdg_divide.o \
	$(OBJDIR)/libdg_light.o \
	$(OBJDIR)/libdg_matrix.o \
	$(OBJDIR)/libdg_obj.o \
	$(OBJDIR)/libdg_pos.o \
	$(OBJDIR)/libdg_opack.o \
	$(OBJDIR)/libdg_palette.o \
	$(OBJDIR)/libdg_prim.o \
	$(OBJDIR)/libdg_pshade.o \
	$(OBJDIR)/libdg_screen.o \
	$(OBJDIR)/libdg_shade.o \
	$(OBJDIR)/libdg_text.o \
	$(OBJDIR)/libfs.o \
	$(OBJDIR)/iso_reader.o \
	$(OBJDIR)/spu_emu.o \
	$(OBJDIR)/memcard_host.o \
	$(OBJDIR)/photo_export.o \
	$(OBJDIR)/hzd_loader.o \
	$(OBJDIR)/game_script_fix.o \
	$(OBJDIR)/link_stubs.o \
	$(OBJDIR)/strcode_lookup.o \
	$(OBJDIR)/gcl_overlay.o \
	$(OBJDIR)/chara_overrides.o \
	$(OBJDIR)/load_data_chara.o \
	$(OBJDIR)/port_config.o \
	$(OBJDIR)/port_menu.o

BSS_OBJ = $(OBJDIR)/bss.o

ALL_OBJ = $(PORT_OBJ) $(STAGE_OBJ) $(OVERLAY_OBJ) $(LIBGV_OBJ) $(LIBGCL_OBJ) $(LIBHZD_OBJ) $(BSS_OBJ) $(GAME_OBJ)

$(TARGET): $(ALL_OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)
	$(CODESIGN)

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
$(OBJDIR)/imgui_impl_opengl3.o: imgui/backends/imgui_impl_opengl3.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<
$(OBJDIR)/imgui_debug.o: imgui_debug.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(INCLUDES) -c -o $@ $<

# --- Port source rules ---
$(OBJDIR)/main.o: main.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/main_game.o: main_game.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

# Pre-game menu + config persistence. port_menu uses ImGui so it needs the
# imgui include path that other CXX rules already pull in. port_config is
# plain C with SDL only.
$(OBJDIR)/port_config.o: port_config.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/port_menu.o: port_menu.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(INCLUDES) -c -o $@ $<

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

$(OBJDIR)/struct_assertions.o: psx/struct_assertions.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/mts.o: mts/mts.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/libdg_stub.o: libdg/libdg_stub.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/vram.o: libdg/vram.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/gl_renderer.o: libdg/gl_renderer.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/pcx_loader.o: libdg/pcx_loader.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/kmd_loader.o: libdg/kmd_loader.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Original libdg source files — need -include of patched libdg.h to fix enum conflict
LIBDG_CFLAGS = $(CFLAGS) -include $(CURDIR)/libdg/libdg.h

# Documentation for the port-copy mechanism: list libdg/<stem>.c files that
# have a port-adapted copy under port/libdg/. The pattern rule below picks the
# port copy automatically because Make prefers a rule whose prerequisite
# actually exists; this list is just for human reference / future reviewers.
LIBDG_PORT_COPIES = opack bound trans obj prim

# Port-adapted libdg copies (take precedence over the source rule below).
$(OBJDIR)/libdg_%.o: libdg/%.c | $(OBJDIR)
	$(CC) $(LIBDG_CFLAGS) -iquote $(SRCDIR)/libdg -c -o $@ $<

$(OBJDIR)/libdg_%.o: $(SRCDIR)/libdg/%.c | $(OBJDIR)
	$(CC) $(LIBDG_CFLAGS) -c -o $@ $<

$(OBJDIR)/libfs.o: libfs/libfs.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/iso_reader.o: libfs/iso_reader.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<


$(OBJDIR)/spu_emu.o: sound/spu_emu.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJDIR)/memcard_host.o: memcard/memcard_host.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/photo_export.o: memcard/photo_export.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Wno-unused-function -c -o $@ $<

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

$(OBJDIR)/strcode_lookup.o: strcode_lookup.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/link_stubs.o: link_stubs.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/gcl_overlay.o: gcl_overlay.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/chara_overrides.o: chara_overrides.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/load_data_chara.o: load_data_chara.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Original source rules ---
# Port-adapted libgv copies (take precedence over the source pattern rule below).
$(OBJDIR)/libgv_%.o: libgv/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -iquote $(SRCDIR)/libgv -c -o $@ $<

$(OBJDIR)/libgv_%.o: $(SRCDIR)/libgv/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/libgcl_%.o: libgcl_fix/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -iquote libgcl_fix -c -o $@ $<

# Port-adapted collision copies (take precedence over the pattern rule below).
$(OBJDIR)/libhzd_%.o: libhzd/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -iquote $(SRCDIR)/libhzd -c -o $@ $<

$(OBJDIR)/libhzd_%.o: $(SRCDIR)/libhzd/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/bss.o: $(SRCDIR)/data/bss.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Port-adapted takabe copies (take precedence over the generic src_%.o below).
$(OBJDIR)/src_takabe/%.o: takabe/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -iquote $(SRCDIR)/takabe -c -o $@ $<

# Port-adapted equip copies (same pattern).
$(OBJDIR)/src_equip/%.o: equip/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -iquote $(SRCDIR)/equip -c -o $@ $<

# Port-adapted kojo copies (same pattern).
$(OBJDIR)/src_kojo/%.o: kojo/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -iquote $(SRCDIR)/kojo -c -o $@ $<

# Port-adapted menu copies (same pattern).
$(OBJDIR)/src_menu/%.o: menu/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -iquote $(SRCDIR)/menu -c -o $@ $<

# Port-adapted thing copies (same pattern).
$(OBJDIR)/src_thing/%.o: thing/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -iquote $(SRCDIR)/thing -c -o $@ $<

# Port-adapted sound copies (same pattern).
$(OBJDIR)/src_sound/%.o: sound/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -iquote $(SRCDIR)/sound -c -o $@ $<

# Port-adapted game copies (same pattern).
$(OBJDIR)/src_game/%.o: game/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -iquote $(SRCDIR)/game -c -o $@ $<

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
