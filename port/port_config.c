/*
 * port_config.c — INI parser/writer for ./port_config.ini.
 *
 * Format (one section per category, key=value lines):
 *
 *   [video]
 *   window_w = 1280
 *   window_h = 896
 *   fullscreen = 0
 *   vsync = 1
 *   gl_enabled = 1
 *   gl_scale = 4
 *   widescreen = 0
 *
 *   [audio]
 *   volume_master = 100
 *
 *   [keyboard]
 *   up = 82      ; SDL_SCANCODE_UP
 *   …
 *
 *   [gamepad]
 *   up = 11      ; SDL_CONTROLLER_BUTTON_DPAD_UP
 *   …
 *
 * We store SDL enum values as integers — same int the SDL API expects, no
 * name→enum translation. The Controls menu uses SDL_GetScancodeName /
 * SDL_GameControllerGetStringForButton for the UI label so users don't
 * have to read raw numbers.
 */
#include "port_config.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

/* port_overrides.h (force-included by the Makefile) macros every
 * fprintf(stream, ...) into printf(...) to paper over PSX code that
 * uses `fprintf(int_stream_id, ...)`. That macro destroys our ability
 * to write to a FILE* and silently sends config data to stdout instead
 * of the .ini file. Undo it for this port-native file — every fprintf
 * here is the real libc one with a real FILE *. */
#undef fprintf

PortConfig g_port_config;
const char *PORT_CONFIG_DEFAULT_PATH = "port_config.ini";

const char *port_btn_name(int btn)
{
    switch (btn) {
        case PORT_BTN_UP:       return "Up";
        case PORT_BTN_DOWN:     return "Down";
        case PORT_BTN_LEFT:     return "Left";
        case PORT_BTN_RIGHT:    return "Right";
        case PORT_BTN_CROSS:    return "Cross";
        case PORT_BTN_CIRCLE:   return "Circle";
        case PORT_BTN_TRIANGLE: return "Triangle";
        case PORT_BTN_SQUARE:   return "Square";
        case PORT_BTN_L1:       return "L1";
        case PORT_BTN_R1:       return "R1";
        case PORT_BTN_L2:       return "L2";
        case PORT_BTN_R2:       return "R2";
        case PORT_BTN_START:    return "Start";
        case PORT_BTN_SELECT:   return "Select";
        default:                return "?";
    }
}

void port_config_set_defaults(PortConfig *c)
{
    /* Video: match the historical compile-time defaults from main.c.
     * SCREEN_WIDTH * WINDOW_SCALE = 320 * 4 = 1280, etc. */
    c->window_w     = 1280;
    c->window_h     = 896;
    c->fullscreen   = 0;
    c->vsync        = 1;
    c->gl_enabled   = 1;
    c->gl_scale     = 4;
    c->widescreen   = 0;

    /* Audio. */
    c->volume_master = 100;

    /* Keyboard: matches the previous hard-coded mts.c table verbatim. */
    c->kb_map[PORT_BTN_UP]       = SDL_SCANCODE_UP;
    c->kb_map[PORT_BTN_DOWN]     = SDL_SCANCODE_DOWN;
    c->kb_map[PORT_BTN_LEFT]     = SDL_SCANCODE_LEFT;
    c->kb_map[PORT_BTN_RIGHT]    = SDL_SCANCODE_RIGHT;
    c->kb_map[PORT_BTN_CROSS]    = SDL_SCANCODE_X;
    c->kb_map[PORT_BTN_CIRCLE]   = SDL_SCANCODE_Z;
    c->kb_map[PORT_BTN_TRIANGLE] = SDL_SCANCODE_S;
    c->kb_map[PORT_BTN_SQUARE]   = SDL_SCANCODE_A;
    c->kb_map[PORT_BTN_L1]       = SDL_SCANCODE_Q;
    c->kb_map[PORT_BTN_R1]       = SDL_SCANCODE_E;
    c->kb_map[PORT_BTN_L2]       = SDL_SCANCODE_1;
    c->kb_map[PORT_BTN_R2]       = SDL_SCANCODE_3;
    c->kb_map[PORT_BTN_START]    = SDL_SCANCODE_RETURN;
    c->kb_map[PORT_BTN_SELECT]   = SDL_SCANCODE_BACKSPACE;

    /* Gamepad: matches the previous hard-coded mts.c table. L2/R2 are
     * triggers, not buttons — leave PORT_PAD_UNBOUND so the per-frame
     * read in mts.c uses the trigger-axis path. */
    c->pad_map[PORT_BTN_UP]       = SDL_CONTROLLER_BUTTON_DPAD_UP;
    c->pad_map[PORT_BTN_DOWN]     = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    c->pad_map[PORT_BTN_LEFT]     = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    c->pad_map[PORT_BTN_RIGHT]    = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    c->pad_map[PORT_BTN_CROSS]    = SDL_CONTROLLER_BUTTON_A;
    c->pad_map[PORT_BTN_CIRCLE]   = SDL_CONTROLLER_BUTTON_B;
    c->pad_map[PORT_BTN_TRIANGLE] = SDL_CONTROLLER_BUTTON_Y;
    c->pad_map[PORT_BTN_SQUARE]   = SDL_CONTROLLER_BUTTON_X;
    c->pad_map[PORT_BTN_L1]       = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    c->pad_map[PORT_BTN_R1]       = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    c->pad_map[PORT_BTN_L2]       = PORT_PAD_UNBOUND;     /* trigger axis */
    c->pad_map[PORT_BTN_R2]       = PORT_PAD_UNBOUND;     /* trigger axis */
    c->pad_map[PORT_BTN_START]    = SDL_CONTROLLER_BUTTON_START;
    c->pad_map[PORT_BTN_SELECT]   = SDL_CONTROLLER_BUTTON_BACK;
}

/* --- INI parsing -----------------------------------------------------------
 * Hand-rolled and tiny on purpose. The file is small (a few dozen keys),
 * not user-typed in tricky ways, and adding a real INI lib would be a
 * larger change than the parser itself. */

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n'))
        *--end = 0;
    return s;
}

/* Section/key lookup keyed by ini-style strings. Order matches the saved
 * file for readability. */
static const char *kb_key_for(int btn)
{
    switch (btn) {
        case PORT_BTN_UP:       return "up";
        case PORT_BTN_DOWN:     return "down";
        case PORT_BTN_LEFT:     return "left";
        case PORT_BTN_RIGHT:    return "right";
        case PORT_BTN_CROSS:    return "cross";
        case PORT_BTN_CIRCLE:   return "circle";
        case PORT_BTN_TRIANGLE: return "triangle";
        case PORT_BTN_SQUARE:   return "square";
        case PORT_BTN_L1:       return "l1";
        case PORT_BTN_R1:       return "r1";
        case PORT_BTN_L2:       return "l2";
        case PORT_BTN_R2:       return "r2";
        case PORT_BTN_START:    return "start";
        case PORT_BTN_SELECT:   return "select";
        default:                return "?";
    }
}

static int btn_from_key(const char *k)
{
    for (int b = 0; b < PORT_BTN_COUNT; b++)
        if (!strcmp(k, kb_key_for(b))) return b;
    return -1;
}

int port_config_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    /* Count successfully-parsed key=value pairs so we can distinguish an
     * actually-loaded file from a 0-byte / all-comments file. The latter
     * happens when a previous run was interrupted between fopen and fputs
     * or the user touched the file by hand; reporting "loaded" for it is
     * misleading. */
    int n_keys_parsed = 0;

    char line[256];
    char section[32] = "";
    while (fgets(line, sizeof(line), f)) {
        /* Strip comments: everything after ';' or '#' on the same line. */
        for (char *p = line; *p; p++) {
            if (*p == ';' || *p == '#') { *p = 0; break; }
        }
        char *s = trim(line);
        if (!*s) continue;

        if (s[0] == '[') {
            char *end = strchr(s, ']');
            if (!end) continue;
            *end = 0;
            strncpy(section, s + 1, sizeof(section) - 1);
            section[sizeof(section) - 1] = 0;
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(s);
        char *val = trim(eq + 1);
        if (!*key) continue;

        long iv = strtol(val, NULL, 10);

        int matched = 1;
        if (!strcmp(section, "video")) {
            if      (!strcmp(key, "window_w"))   g_port_config.window_w = (int)iv;
            else if (!strcmp(key, "window_h"))   g_port_config.window_h = (int)iv;
            else if (!strcmp(key, "fullscreen")) g_port_config.fullscreen = (int)iv;
            else if (!strcmp(key, "vsync"))      g_port_config.vsync = (int)iv;
            else if (!strcmp(key, "gl_enabled")) g_port_config.gl_enabled = (int)iv;
            else if (!strcmp(key, "gl_scale"))   g_port_config.gl_scale = (int)iv;
            else if (!strcmp(key, "widescreen")) g_port_config.widescreen = (int)iv;
            else matched = 0;
        } else if (!strcmp(section, "audio")) {
            if (!strcmp(key, "volume_master"))   g_port_config.volume_master = (int)iv;
            else matched = 0;
        } else if (!strcmp(section, "keyboard")) {
            int b = btn_from_key(key);
            if (b >= 0) g_port_config.kb_map[b] = (int)iv;
            else matched = 0;
        } else if (!strcmp(section, "gamepad")) {
            int b = btn_from_key(key);
            if (b >= 0) g_port_config.pad_map[b] = (int)iv;
            else matched = 0;
        } else {
            matched = 0;
        }
        if (matched) n_keys_parsed++;
    }
    fclose(f);

    /* Empty / unparseable file → caller should treat as "no config". */
    if (n_keys_parsed == 0) return 0;

    /* Clamp ranges so a hand-edited file can't break the renderer. */
    if (g_port_config.gl_scale < 1) g_port_config.gl_scale = 1;
    if (g_port_config.gl_scale > 8) g_port_config.gl_scale = 8;
    if (g_port_config.volume_master < 0)   g_port_config.volume_master = 0;
    if (g_port_config.volume_master > 100) g_port_config.volume_master = 100;
    if (g_port_config.window_w < 320) g_port_config.window_w = 320;
    if (g_port_config.window_h < 224) g_port_config.window_h = 224;

    return 1;
}

int port_config_save(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "port_config_save: open '%s' for write failed: %s\n",
                path, strerror(errno));
        return 0;
    }

    fprintf(f, "; Metal Gear Solid port config. Hand-editable; the menu writes\n");
    fprintf(f, "; this file when you click 'Save & Apply'. Integer values\n");
    fprintf(f, "; for kb_map / pad_map are raw SDL enum values.\n\n");

    fprintf(f, "[video]\n");
    fprintf(f, "window_w   = %d\n", g_port_config.window_w);
    fprintf(f, "window_h   = %d\n", g_port_config.window_h);
    fprintf(f, "fullscreen = %d\n", g_port_config.fullscreen);
    fprintf(f, "vsync      = %d\n", g_port_config.vsync);
    fprintf(f, "gl_enabled = %d\n", g_port_config.gl_enabled);
    fprintf(f, "gl_scale   = %d\n", g_port_config.gl_scale);
    fprintf(f, "widescreen = %d\n\n", g_port_config.widescreen);

    fprintf(f, "[audio]\n");
    fprintf(f, "volume_master = %d\n\n", g_port_config.volume_master);

    fprintf(f, "[keyboard] ; values are SDL_Scancode integers\n");
    for (int b = 0; b < PORT_BTN_COUNT; b++) {
        const char *name = SDL_GetScancodeName((SDL_Scancode)g_port_config.kb_map[b]);
        fprintf(f, "%-8s = %d  ; %s\n", kb_key_for(b), g_port_config.kb_map[b],
                (name && *name) ? name : "(unbound)");
    }
    fprintf(f, "\n");

    fprintf(f, "[gamepad] ; values are SDL_GameControllerButton ints; -1 = unbound (triggers used for L2/R2)\n");
    for (int b = 0; b < PORT_BTN_COUNT; b++) {
        const char *name = (g_port_config.pad_map[b] >= 0)
            ? SDL_GameControllerGetStringForButton((SDL_GameControllerButton)g_port_config.pad_map[b])
            : NULL;
        fprintf(f, "%-8s = %d  ; %s\n", kb_key_for(b), g_port_config.pad_map[b],
                name ? name : "(unbound)");
    }

    fclose(f);
    /* Echo the resolved absolute path so the user can find the file even if
     * they ran the binary from an unexpected cwd. */
    char abs[1024];
    if (realpath(path, abs))
        printf("port: saved config to %s\n", abs);
    else
        printf("port: saved config to %s (cwd-relative)\n", path);
    return 1;
}
