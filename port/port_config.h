/*
 * port_config.h — user-facing port settings persisted to ./port_config.ini.
 *
 * One global PortConfig struct, set up at startup with defaults that match
 * the previously hard-coded values, then optionally overwritten by INI on
 * disk. The pre-game options/controls menu (port_menu.cpp) mutates this
 * struct and calls port_config_save() to write changes back. The rest of
 * the port reads from g_port_config — no env-var overrides except where
 * existing infrastructure still honors them (PORT_GL / PORT_GL_SCALE).
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* PSX pad button ids — used as indices into kb_map[] / pad_map[]. Order is
 * stable, never reorder; new buttons go at the end before PORT_BTN_COUNT. */
typedef enum {
    PORT_BTN_UP = 0,
    PORT_BTN_DOWN,
    PORT_BTN_LEFT,
    PORT_BTN_RIGHT,
    PORT_BTN_CROSS,
    PORT_BTN_CIRCLE,
    PORT_BTN_TRIANGLE,
    PORT_BTN_SQUARE,
    PORT_BTN_L1,
    PORT_BTN_R1,
    PORT_BTN_L2,
    PORT_BTN_R2,
    PORT_BTN_START,
    PORT_BTN_SELECT,
    PORT_BTN_COUNT
} PortButton;

/* SDL scancode for "unbound" — clearly invalid, never produced by SDL. */
#define PORT_KEY_UNBOUND     (-1)
/* SDL_GameControllerButton for "unbound" — SDL uses -1 for INVALID. */
#define PORT_PAD_UNBOUND     (-1)

typedef struct {
    /* --- video --- */
    int window_w;        /* logical window size at startup */
    int window_h;
    int fullscreen;      /* 0/1, borderless-desktop fullscreen */
    int vsync;           /* 0/1 */
    int gl_enabled;      /* 0=SDL2 software, 1=OpenGL */
    int gl_scale;        /* PORT_GL_SCALE: 1..8 (FBO upscale factor) */
    int widescreen;      /* 0=4:3 (320 internal), 1=16:9 Hor+ (400 internal) */

    /* --- audio --- */
    int volume_master;   /* 0..100 */

    /* --- input bindings ---
     * kb_map[btn]  = SDL_Scancode (or PORT_KEY_UNBOUND)
     * pad_map[btn] = SDL_GameControllerButton (or PORT_PAD_UNBOUND).
     * L2/R2 on gamepad come from trigger axes, not buttons — pad_map[L2/R2]
     * is conventionally PORT_PAD_UNBOUND and the code falls back to triggers. */
    int kb_map[PORT_BTN_COUNT];
    int pad_map[PORT_BTN_COUNT];
} PortConfig;

extern PortConfig g_port_config;

/* Human-readable label for the UI ("Cross", "L1", …). Stable strings; safe
 * to use as text or as a unique id. */
const char *port_btn_name(int btn);

/* Initialize to compile-time defaults (matches the historical hard-coded
 * SDL_SCANCODE_* / SDL_CONTROLLER_BUTTON_* table from mts.c). */
void port_config_set_defaults(PortConfig *c);

/* Load INI from `path`. Returns 1 if a file existed and was parsed, 0 if
 * the file is missing (caller should then keep defaults). Errors are
 * logged to stderr. Missing keys keep their current value, so a partial
 * file is fine. */
int port_config_load(const char *path);

/* Write current g_port_config to `path` as INI. Returns 1 on success. */
int port_config_save(const char *path);

/* The default INI path, relative to the current working directory.
 * (Per user choice: ./port_config.ini next to the executable.) */
extern const char *PORT_CONFIG_DEFAULT_PATH;

#ifdef __cplusplus
}
#endif
#endif /* PORT_CONFIG_H */
