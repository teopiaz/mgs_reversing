/*
 * port_menu.cpp — pre-game ImGui screens (splash, main, options, controls).
 *
 * Lifecycle: main.c calls port_menu_init() once, then in a tight loop calls
 * port_poll_events() (which forwards events to port_menu_handle_event),
 * port_menu_frame(), and SDL_GL_SwapWindow / SDL_RenderPresent. The loop
 * exits when port_menu_state() returns PORT_MENU_GAME (start the engine)
 * or PORT_MENU_QUIT (shut down).
 *
 * Rendering: this module owns its own ImGui frame from NewFrame to Render,
 * since the game's `imgui_render` path expects the engine to already be
 * running. The two paths never overlap — menu runs first, then yields.
 */
#include "port_menu.h"
#include "port_config.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_impl_opengl3.h"

#include <SDL.h>
#include <string.h>
#include <stdio.h>

/* GL vs SDL renderer flag, set by imgui_init in imgui_debug.cpp. The menu
 * needs to know which backend NewFrame/RenderDrawData path to use. */
extern "C" int imgui_using_gl(void);
/* Runtime widescreen toggle in gl_renderer.c. */
extern "C" void gl_renderer_set_widescreen(int on);

static PortMenuState s_state          = PORT_MENU_SPLASH;
static Uint32        s_splash_start_ms = 0;
static const Uint32  SPLASH_MS         = 1500;

/* Key/button capture state for the Controls page. capture_target = -1
 * means no capture in progress; otherwise it's a PortButton index.
 * capture_is_pad differentiates keyboard vs gamepad capture. */
static int  s_capture_target = -1;
static bool s_capture_is_pad = false;

/* Working copy of the config. Editing happens against this; "Save & Apply"
 * commits it to g_port_config + disk; "Cancel" discards. Initialized lazily
 * on entering each editable page. */
static PortConfig s_edit;

static void copy_config(PortConfig *dst, const PortConfig *src)
{
    *dst = *src;
}

/* ------------------------------------------------------------------------ */
/* Page renderers — each draws one ImGui window centered on screen.          */
/* ------------------------------------------------------------------------ */

static void center_next_window(float w, float h)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - w) * 0.5f,
                                   (io.DisplaySize.y - h) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
}

static void page_splash(void)
{
    /* No interaction. Big centered title + version line; auto-advances after
     * SPLASH_MS or on any key. The "any key" is handled by the event hook. */
    Uint32 now = SDL_GetTicks();
    if (s_splash_start_ms == 0) s_splash_start_ms = now;
    if (now - s_splash_start_ms >= SPLASH_MS) {
        s_state = PORT_MENU_MAIN;
        return;
    }
    center_next_window(640, 280);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##splash", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar);

    ImGui::PushFont(nullptr);  /* default font; size up via scale below */
    ImGui::SetWindowFontScale(2.4f);
    ImVec2 sz = ImGui::CalcTextSize("METAL GEAR SOLID");
    ImGui::SetCursorPosX((640 - sz.x) * 0.5f);
    ImGui::SetCursorPosY(60.0f);
    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "METAL GEAR SOLID");

    ImGui::SetWindowFontScale(1.1f);
    sz = ImGui::CalcTextSize("macOS native port");
    ImGui::SetCursorPosX((640 - sz.x) * 0.5f);
    ImGui::SetCursorPosY(140.0f);
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "macOS native port");

    ImGui::SetWindowFontScale(0.9f);
    const char *hint = "press any key to continue";
    sz = ImGui::CalcTextSize(hint);
    ImGui::SetCursorPosX((640 - sz.x) * 0.5f);
    ImGui::SetCursorPosY(230.0f);
    /* Fade-in alpha on the hint to draw the eye to it. */
    float a = ((float)(now - s_splash_start_ms) / (float)SPLASH_MS);
    if (a > 1.0f) a = 1.0f;
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, a), "%s", hint);

    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar();
}

static void page_main(void)
{
    center_next_window(360, 320);
    ImGui::Begin("Metal Gear Solid", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

    ImGui::Dummy(ImVec2(0, 6));
    ImVec2 btn(-1.0f, 36.0f);   /* full-width, fixed height */

    if (ImGui::Button("Start Game", btn)) {
        s_state = PORT_MENU_GAME;
    }
    if (ImGui::Button("Options", btn)) {
        copy_config(&s_edit, &g_port_config);
        s_state = PORT_MENU_OPTIONS;
    }
    if (ImGui::Button("Controls", btn)) {
        copy_config(&s_edit, &g_port_config);
        s_capture_target = -1;
        s_state = PORT_MENU_CONTROLS;
    }
    ImGui::Dummy(ImVec2(0, 12));
    if (ImGui::Button("Exit", btn)) {
        s_state = PORT_MENU_QUIT;
    }

    /* Tiny footer line — where the config lives, version-ish info. */
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::Separator();
    ImGui::TextDisabled("config: %s", PORT_CONFIG_DEFAULT_PATH);

    ImGui::End();
}

static void apply_and_save(void)
{
    /* Commit edit-buffer to live config + disk. Caller is responsible for
     * applying changes that need re-creating the window (resolution /
     * fullscreen / GL backend) — for those we just write the file and the
     * change takes effect on next launch (documented in the UI). */
    copy_config(&g_port_config, &s_edit);
    port_config_save(PORT_CONFIG_DEFAULT_PATH);

    /* In-game-applicable settings can be flipped live. Widescreen has a
     * runtime hook in gl_renderer; volume is read by the audio mixer; the
     * others (resolution etc.) need a restart. */
    gl_renderer_set_widescreen(g_port_config.widescreen);
}

static void page_options(void)
{
    center_next_window(560, 440);
    ImGui::Begin("Options", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse);

    ImGui::TextDisabled("Some changes (resolution, fullscreen, backend) take effect on next launch.");
    ImGui::Separator();

    /* --- Video --- */
    ImGui::Text("Video");

    /* Preset resolution dropdown. The first block is integer multiples of
     * the PSX native 320x224 (preserves pixel aspect exactly); the second
     * block is common modern resolutions that won't be pixel-perfect but
     * are what users actually want for "full HD" / "2K" / "4K" displays.
     * The final "Custom" entry is selected when neither the integer-scale
     * nor modern presets match s_edit.window_w/h — the user can then
     * adjust the W/H sliders directly. */
    struct Preset { const char *label; int w; int h; };
    static const Preset PRESETS[] = {
        { "320 x 224 (PSX native, 1x)",   320,  224  },
        { "640 x 448 (2x)",               640,  448  },
        { "960 x 672 (3x)",               960,  672  },
        { "1280 x 896 (4x)",             1280,  896  },
        { "1600 x 1120 (5x)",            1600, 1120  },
        { "1920 x 1344 (6x)",            1920, 1344  },
        { "2560 x 1792 (8x)",            2560, 1792  },
        { "1280 x 720 (HD 16:9)",        1280,  720  },
        { "1920 x 1080 (Full HD 16:9)",  1920, 1080  },
        { "2560 x 1440 (2K 16:9)",       2560, 1440  },
        { "3840 x 2160 (4K 16:9)",       3840, 2160  },
    };
    const int N_PRESETS = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));

    /* Find which preset matches the current edit values; -1 means custom. */
    int sel = -1;
    for (int i = 0; i < N_PRESETS; i++) {
        if (PRESETS[i].w == s_edit.window_w && PRESETS[i].h == s_edit.window_h) {
            sel = i;
            break;
        }
    }
    /* Build the combo label: either the matched preset's label or a
     * "Custom (WxH)" string. Static buffer is fine — single-threaded UI
     * thread, re-rendered every frame. */
    static char combo_label[64];
    if (sel >= 0)
        snprintf(combo_label, sizeof combo_label, "%s", PRESETS[sel].label);
    else
        snprintf(combo_label, sizeof combo_label, "Custom (%d x %d)",
                 s_edit.window_w, s_edit.window_h);

    if (ImGui::BeginCombo("resolution", combo_label)) {
        for (int i = 0; i < N_PRESETS; i++) {
            bool is_sel = (i == sel);
            if (ImGui::Selectable(PRESETS[i].label, is_sel)) {
                s_edit.window_w = PRESETS[i].w;
                s_edit.window_h = PRESETS[i].h;
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        /* Separator before the manual-edit hint so users know they CAN
         * still set arbitrary values via the sliders below. */
        ImGui::Separator();
        ImGui::TextDisabled("(use sliders below for custom values)");
        ImGui::EndCombo();
    }
    /* Keep the sliders as a fallback for non-standard resolutions, but
     * make them visually subordinate. */
    ImGui::SliderInt("window width",  &s_edit.window_w, 320, 3840);
    ImGui::SliderInt("window height", &s_edit.window_h, 224, 2160);
    ImGui::Checkbox("Fullscreen (borderless desktop)", (bool *)&s_edit.fullscreen);
    ImGui::Checkbox("VSync", (bool *)&s_edit.vsync);
    ImGui::Checkbox("OpenGL backend (recommended)", (bool *)&s_edit.gl_enabled);
    ImGui::SliderInt("FBO scale (1-8)", &s_edit.gl_scale, 1, 8);
    ImGui::Checkbox("Widescreen (16:9 Hor+, internal 400 wide)", (bool *)&s_edit.widescreen);

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Separator();

    /* --- Audio --- */
    ImGui::Text("Audio");
    ImGui::SliderInt("master volume", &s_edit.volume_master, 0, 100, "%d%%");

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Separator();

    /* --- Game --- */
    ImGui::Text("Game");
    /* Maps directly to the OPTION_ENGLISH bit (linkvar.h:170) — 0=JP, 1=EN.
     * The in-game options screen also exposes this, but giving it a pre-game
     * toggle saves the user from booting into the wrong language and having
     * to redo the title-screen menu in a script they can't read. */
    ImGui::TextUnformatted("Language");
    ImGui::SameLine();
    ImGui::RadioButton("English",  &s_edit.language, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Japanese", &s_edit.language, 0);

    ImGui::Dummy(ImVec2(0, 12));
    ImGui::Separator();

    if (ImGui::Button("Save & Apply", ImVec2(140, 32))) {
        apply_and_save();
        s_state = PORT_MENU_MAIN;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 32))) {
        s_state = PORT_MENU_MAIN;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore defaults", ImVec2(160, 32))) {
        port_config_set_defaults(&s_edit);
    }

    ImGui::End();
}

static const char *kb_label(int scancode)
{
    if (scancode < 0) return "(unbound)";
    const char *n = SDL_GetScancodeName((SDL_Scancode)scancode);
    return (n && *n) ? n : "(unknown)";
}

static const char *pad_label(int btn)
{
    if (btn < 0) return "(unbound)";
    const char *n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)btn);
    return n ? n : "(unknown)";
}

static void page_controls(void)
{
    center_next_window(720, 600);
    ImGui::Begin("Controls", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse);

    if (s_capture_target >= 0) {
        ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1),
                           "Press a %s for %s …  (Esc to cancel)",
                           s_capture_is_pad ? "gamepad button" : "key",
                           port_btn_name(s_capture_target));
    } else {
        ImGui::TextDisabled("Click a binding to remap it. L2/R2 on gamepad use trigger axes.");
    }
    ImGui::Separator();

    if (ImGui::BeginTable("bindings", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("PSX button", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Keyboard",   ImGuiTableColumnFlags_WidthFixed, 240.0f);
        ImGui::TableSetupColumn("Gamepad",    ImGuiTableColumnFlags_WidthFixed, 240.0f);
        ImGui::TableHeadersRow();

        for (int b = 0; b < PORT_BTN_COUNT; b++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(port_btn_name(b));

            ImGui::TableSetColumnIndex(1);
            char id[32];
            snprintf(id, sizeof id, "%s##kb_%d", kb_label(s_edit.kb_map[b]), b);
            if (ImGui::Button(id, ImVec2(-1, 0))) {
                s_capture_target = b;
                s_capture_is_pad = false;
            }

            ImGui::TableSetColumnIndex(2);
            snprintf(id, sizeof id, "%s##pad_%d", pad_label(s_edit.pad_map[b]), b);
            if (ImGui::Button(id, ImVec2(-1, 0))) {
                s_capture_target = b;
                s_capture_is_pad = true;
            }
        }
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    if (ImGui::Button("Save & Apply", ImVec2(140, 32))) {
        apply_and_save();
        s_capture_target = -1;
        s_state = PORT_MENU_MAIN;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 32))) {
        s_capture_target = -1;
        s_state = PORT_MENU_MAIN;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore defaults", ImVec2(160, 32))) {
        PortConfig def;
        port_config_set_defaults(&def);
        memcpy(s_edit.kb_map,  def.kb_map,  sizeof s_edit.kb_map);
        memcpy(s_edit.pad_map, def.pad_map, sizeof s_edit.pad_map);
    }

    ImGui::End();
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

extern "C" void port_menu_init(void)
{
    s_state = PORT_MENU_SPLASH;
    s_splash_start_ms = 0;
    s_capture_target = -1;
    copy_config(&s_edit, &g_port_config);
}

extern "C" int port_menu_state(void)
{
    return (int)s_state;
}

extern "C" void port_menu_handle_event(const SDL_Event *event)
{
    if (!event) return;

    /* Splash: any key/click/button advances. */
    if (s_state == PORT_MENU_SPLASH) {
        if (event->type == SDL_KEYDOWN ||
            event->type == SDL_MOUSEBUTTONDOWN ||
            event->type == SDL_CONTROLLERBUTTONDOWN) {
            s_state = PORT_MENU_MAIN;
        }
        return;
    }

    /* Controls page: in capture mode, the next key / button press becomes
     * the new binding. ESC cancels. */
    if (s_capture_target >= 0) {
        if (event->type == SDL_KEYDOWN) {
            SDL_Scancode sc = event->key.keysym.scancode;
            if (sc == SDL_SCANCODE_ESCAPE) {
                s_capture_target = -1;
                return;
            }
            if (!s_capture_is_pad) {
                s_edit.kb_map[s_capture_target] = (int)sc;
                s_capture_target = -1;
            }
            /* If we're waiting for a pad button but a key was pressed,
             * keep waiting (user might still hit the pad). */
        } else if (event->type == SDL_CONTROLLERBUTTONDOWN && s_capture_is_pad) {
            s_edit.pad_map[s_capture_target] = event->cbutton.button;
            s_capture_target = -1;
        }
    }
}

extern "C" void port_menu_frame(SDL_Renderer *renderer)
{
    /* Begin a fresh ImGui frame — the menu fully owns rendering during the
     * pre-game phase. Backend selection mirrors imgui_render's logic. */
    int use_gl = imgui_using_gl();
    if (use_gl) ImGui_ImplOpenGL3_NewFrame();
    else        ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    /* Dim background so the menu reads against a clean dark canvas even
     * before the game has rendered anything. */
    ImGuiIO &io = ImGui::GetIO();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0), io.DisplaySize, IM_COL32(12, 14, 22, 255));

    switch (s_state) {
        case PORT_MENU_SPLASH:   page_splash();   break;
        case PORT_MENU_MAIN:     page_main();     break;
        case PORT_MENU_OPTIONS:  page_options();  break;
        case PORT_MENU_CONTROLS: page_controls(); break;
        default: break;
    }

    ImGui::Render();
    if (use_gl)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    else
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}
