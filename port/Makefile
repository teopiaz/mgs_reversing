CC = clang
SRCDIR = ../source

INCLUDES = -I psx -I include -I . -I $(SRCDIR)/include -I $(SRCDIR)
CFLAGS = -w -O0 -g -DINTEGRAL -include psx/port_overrides.h $(INCLUDES) \
         -Wno-int-conversion -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
         -Wno-incompatible-pointer-types
SDL_CFLAGS = $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs) -lm

OBJDIR = obj
TARGET = mgs

# Files that can't compile due to MIPS asm or PSX-specific constructs
EXCLUDE_FILES = \
	$(SRCDIR)/libhzd/collide.c \
	$(SRCDIR)/game/movie.c \
	$(SRCDIR)/enemy/action.c \
	$(SRCDIR)/bullet/jirai.c \
	$(SRCDIR)/menu/life.c \
	$(SRCDIR)/menu/radar.c \
	$(SRCDIR)/menu/radioanim.c \
	$(SRCDIR)/menu/radiofacedraw.c \
	$(SRCDIR)/menu/debug.c \
	$(SRCDIR)/enemy/searchli.c \
	$(SRCDIR)/okajima/blood.c \
	$(SRCDIR)/okajima/splash.c \
	$(SRCDIR)/kojo/demothrd.c \
	$(SRCDIR)/takabe/ending2.c \
	$(SRCDIR)/takabe/envmap3.c \
	$(SRCDIR)/takabe/sub_efct.c \
	$(SRCDIR)/takabe/spark2.c \
	$(SRCDIR)/takabe/thing.c \
	$(SRCDIR)/takabe/vib_edit.c \
	$(SRCDIR)/onoda/change/change.c \
	$(SRCDIR)/onoda/demosel/demosel.c \
	$(SRCDIR)/enemy/merylaction.c \
	$(SRCDIR)/enemy/merylcheck.c \
	$(SRCDIR)/contrib/dev/overlay_table.c \
	$(SRCDIR)/game/script.c

all: $(TARGET)

# --- Discover all compilable source files ---
# Exclude snake_vr (VR disc variant, duplicates snake/) and stage/stagevr (per-overlay)
GAME_ALL_SRC = $(shell find $(SRCDIR)/game $(SRCDIR)/chara $(SRCDIR)/enemy \
	$(SRCDIR)/weapon $(SRCDIR)/bullet $(SRCDIR)/equip $(SRCDIR)/anime \
	$(SRCDIR)/thing $(SRCDIR)/font $(SRCDIR)/menu $(SRCDIR)/animal \
	$(SRCDIR)/kojo $(SRCDIR)/okajima $(SRCDIR)/onoda $(SRCDIR)/takabe \
	$(SRCDIR)/contrib -name "*.c" -not -path "*/snake_vr/*" -not -path "*/torture/*" | sort)

GAME_SRC = $(filter-out $(EXCLUDE_FILES), $(GAME_ALL_SRC))
GAME_OBJ = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/src_%.o,$(GAME_SRC))

# --- Engine libraries (from source, unmodified) ---
LIBGV_SRC = $(wildcard $(SRCDIR)/libgv/*.c)
LIBGV_OBJ = $(patsubst $(SRCDIR)/libgv/%.c,$(OBJDIR)/libgv_%.o,$(LIBGV_SRC))

# Patched libgcl (64-bit pointer fix in parse.c/expr.c)
LIBGCL_SRC = $(wildcard libgcl_fix/*.c)
LIBGCL_OBJ = $(patsubst libgcl_fix/%.c,$(OBJDIR)/libgcl_%.o,$(LIBGCL_SRC))

# Exclude collide.c (hardcoded scratchpad) and hzdd.c (OFFSET_TO_PTR)
LIBHZD_SRC = $(filter-out $(SRCDIR)/libhzd/collide.c $(SRCDIR)/libhzd/hzdd.c, $(wildcard $(SRCDIR)/libhzd/*.c))
LIBHZD_OBJ = $(patsubst $(SRCDIR)/libhzd/%.c,$(OBJDIR)/libhzd_%.o,$(LIBHZD_SRC))

# --- Port sources ---
PORT_OBJ = \
	$(OBJDIR)/main.o \
	$(OBJDIR)/main_game.o \
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
	$(OBJDIR)/sd_stubs.o \
	$(OBJDIR)/memcard_stub.o \
	$(OBJDIR)/libhzd_collide_stub.o \
	$(OBJDIR)/hzd_loader.o \
	$(OBJDIR)/stage_s00a.o \
	$(OBJDIR)/stage_select.o \
	$(OBJDIR)/game_script_fix.o \
	$(OBJDIR)/link_stubs.o

BSS_OBJ = $(OBJDIR)/bss.o

ALL_OBJ = $(PORT_OBJ) $(LIBGV_OBJ) $(LIBGCL_OBJ) $(LIBHZD_OBJ) $(BSS_OBJ) $(GAME_OBJ)

$(TARGET): $(ALL_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)
	codesign -s - --entitlements entitlements.plist -f $@ 2>/dev/null || true

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


$(OBJDIR)/sd_stubs.o: sound/sd_stubs.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/memcard_stub.o: memcard/memcard_stub.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/libhzd_collide_stub.o: libhzd_collide_stub.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/hzd_loader.o: libhzd/hzd_loader.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/stage_s00a.o: $(SRCDIR)/stage/s00a.c | $(OBJDIR)
	$(CC) $(CFLAGS) -D_StageCharacterEntries=_StageCharacterEntries_s00a -c -o $@ $<

$(OBJDIR)/stage_select.o: $(SRCDIR)/stage/select.c | $(OBJDIR)
	$(CC) $(CFLAGS) -D_StageCharacterEntries=_StageCharacterEntries_select -c -o $@ $<

$(OBJDIR)/game_script_fix.o: game_script_fix.c | $(OBJDIR)
	$(CC) $(CFLAGS) -I libgcl_fix -c -o $@ $<

$(OBJDIR)/link_stubs.o: link_stubs.c | $(OBJDIR)
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
