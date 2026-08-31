#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_sdlrenderer2.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include <SDL.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* When imgui_init is given a NULL SDL_Renderer, we assume an SDL_GL context
   is current and use the ImGui OpenGL3 backend instead. */
static bool g_imgui_use_gl = false;

/* Exported so the pre-game menu (port_menu.cpp) can pick the right
 * ImGui backend NewFrame / RenderDrawData path. */
extern "C" int imgui_using_gl(void) { return g_imgui_use_gl ? 1 : 0; }

extern "C" {
#include "imgui_debug.h"

/* Mirror GV_ACT layout from libgv.h */
typedef void (*ActFunc)(void *);
typedef void (*FreeFunc)(void *);
typedef struct _ActorNode {
    struct _ActorNode *prev;
    struct _ActorNode *next;
    ActFunc            act;
    ActFunc            die;
    FreeFunc           free_fn;
    const char        *filename;
    int                runtime;
    int                count;
} ActorNode;

typedef struct {
    ActorNode first;
    ActorNode last;
    short     pause;
    short     kill;
} PortActorList;

extern "C" void *port_gv_actor_list(int level);   /* port/libgv/actor.c */
extern void GV_DestroyActorQuick(void *actor);
extern int GV_Clock;
extern int GV_Time;
extern int GM_GameStatus;
extern int GM_AlertMode;
extern int GM_AlertLevel;

/* CHARA registry — used by the "Spawn actor" debug menu. Mirrors the
 * layout from source/include/charadef.h so we don't have to pull the
 * engine header into this C++ file. */
/* In C this would be a no-arg-prototype that the GCL spawner calls with
 * (name, where, argc, argv) anyway; in C++ '()' means '(void)' so we
 * declare the explicit GCL signature. */
typedef void *(*PortNEWCHARA)(int name, int where, int argc, char **argv);
typedef struct { unsigned short class_id; PortNEWCHARA func; } PortCHARA;
extern PortCHARA  MainCharacterEntries[];          /* terminated by func==NULL */
extern void      *StageCharacterEntries;            /* points at CHARA[] when a stage is loaded */
extern const char *strcode_to_string(uint16_t code);

/* Camera / rendering state */
typedef struct { short m[3][3]; short pad; int t[3]; } PortMATRIX;
typedef struct { short vx, vy, vz, pad; } PortSVECTOR;
typedef struct {
    unsigned long *ot[2];                 /*  0..15  */
    short ot_size, link, dblbuf, dirty;   /* 16..23  */
    PortMATRIX eye_inv;                   /* 24..55  */
    PortMATRIX eye;                       /* 56..87  */
    short clip_distance;                  /* 88      */
    short queue_size;                     /* 90      */
    short prim_index;                     /* 92      */
    short objs_index;                     /* 94      */
    /* Real DG_CHANL on port continues with:
         DG_OBJS **queue;        // 8 bytes  (96..103)
         RECT     clip_rect;     // 8 bytes  (104..111)
         RECT     new_clip_rect; // 8 bytes  (112..119)
         DR_ENV   env1[2];       // 128 bytes (120..247) — 2x (u_long tag + u_long code[15])
         DR_ENV   env2[2];       // 128 bytes (248..375)
         DR_ENV   new_env[2];    // 128 bytes (376..503)
       Total real sizeof(DG_CHANL) == 504 on port. Pad here so DG_Chanls[1]
       and DG_Chanls[2] read from the correct array stride; without this the
       imgui debug pane was reading garbage from inside DG_Chanls[0]. */
    char _pad_to_real_size[504 - 96];
} PortDG_CHANL_Partial;

extern PortDG_CHANL_Partial DG_Chanls[];
extern PortSVECTOR GM_PlayerPosition;
extern int port_last_drawn_faces;
extern int DG_CurrentGroupID;

/* GM_Camera: eye(SVEC) center(SVEC) rotate(SVEC) flags(int) track(int)
   zoom(short) first_person(short) alert_mask(short) interp(short)
   field_28(short) field_2A(short) pan(SVEC) ... */
typedef struct {
    PortSVECTOR eye;
    PortSVECTOR center;
    PortSVECTOR rotate;
    int         flags;
    int         track;
    short       zoom;
    short       first_person;
    short       alert_mask;
    short       interp;
    short       field_28;
    short       field_2A;
    PortSVECTOR pan;
} PortGM_CAMERA;

/* gUnkCameraStruct2_800B7868: eye(SVEC) center(SVEC) rotate(SVEC)
   type(int) track(int) zoom(short) ... */
typedef struct {
    PortSVECTOR eye;
    PortSVECTOR center;
    PortSVECTOR rotate;
    int         type;
    int         track;
    short       zoom;
} PortCamStruct2;

/* gUnkCameraStruct_800B77B8: similar + interp field */
typedef struct {
    PortSVECTOR eye;
    PortSVECTOR center;
    PortSVECTOR rotate;
    PortSVECTOR rotate2;
    int         track;
    int         interp;
} PortCamStructB;

extern PortGM_CAMERA    GM_Camera;
extern PortCamStruct2   gUnkCameraStruct2_800B7868;
extern PortCamStructB   gUnkCameraStruct_800B77B8;
extern PortSVECTOR      svec_800ABA88;

/* Override values for camera debug */
int imgui_cam_override = 0;
int imgui_cam_eye_inv_t[3] = {0};
int imgui_cam_clip_dist = 320;
short imgui_cam_eye_inv_m[3][3] = {{0}};

/* Lighting mode: 0 = Gouraud (default), 1 = per-pixel (PSX NCS formula). */
int imgui_per_pixel_light = 0;

} /* extern "C" */

/* Single unified debug window, toggled by F1 (see main.c). Tabs inside. */
static bool show_debug    = false;
static bool show_imgui_demo = false;

/* Extern helpers pulled in from engine / port. */
extern "C" {
    extern int   DG_FrameRate;
    extern void  port_vram_toggle_debug(void);
    extern int   port_vram_debug_view(void);
    extern long  mts_PadRead(int unused);
    extern unsigned char  port_pad_lx;
    extern unsigned char  port_pad_ly;
    extern char           port_current_stage[16];
    /* Sound engine globals (sd_ext.h). */
    extern unsigned int   str_status;        /* stream state 0..7 */
    extern int            sng_status;        /* song state */
    extern int            bgm_idx;           /* current BGM index */
    extern unsigned int   str_volume;        /* stream volume */
    extern int            str_vox_on;        /* VOX output flag */
    extern int            str_mute_status;
    extern int            sd_sng_code_buf[16];
    extern int            se_tracks;
    /* SPU emulator accessors (port/sound/spu_emu.c). */
    typedef struct {
        int active, key_off;
        short vol_l, vol_r;
        unsigned short pitch;
        unsigned long addr;
        int env_phase, env_level;
    } PortSpuVoiceInfo;
    extern int  port_spu_get_voices(PortSpuVoiceInfo *out, int max);
    extern void port_spu_get_master(short *l, short *r);
    extern int  port_spu_stream_info(int *rd, int *wr_r, int *wr_l,
                                     int *active, unsigned long *br, unsigned long *bl);
    extern int  port_spu_is_muted(void);
    extern void port_spu_set_muted(int on);
    extern int  port_spu_voice_is_muted(int voice);
    extern void port_spu_voice_set_mute(int voice, int on);
    extern void port_spu_voice_mute_all(int on);

    /* Lighting globals (source/libdg/light.c). Mirror layouts locally so this
       C++ TU doesn't pull the C engine headers (and their MIPS/PSYQ types). */
    typedef struct { short vx, vy, vz, pad; } PortSVEC;
    typedef struct { unsigned char r, g, b, cd; } PortCVEC;
    typedef struct {
        PortSVEC       pos;
        unsigned short brightness;
        unsigned short radius;
        PortCVEC       color;
    } PortDG_LIT;
    typedef struct { int count; PortDG_LIT *p; } PortDG_FixedLight;
    typedef struct { int n_lights; PortDG_LIT lights[8]; } PortDG_TmpLightList;

    extern PortMATRIX         DG_LightMatrix;   /* rows = main/sub1/sub2 directions */
    extern PortMATRIX         DG_ColorMatrix;   /* cols = main/sub1/sub2 colors */
    extern PortSVEC           DG_Ambient;       /* global ambient RGB (0..255) */
    /* fix_lights[]/tlights[] are file-static in source/libdg/light.c. */
    extern "C" int   port_dg_fixed_light_count(int group);
    extern "C" void *port_dg_fixed_light_data(int group);
    extern "C" int   port_dg_tmp_light_count(int buf);
    extern "C" void *port_dg_tmp_light_data(int buf);

    /* Lighting debug overrides (libdg_stub.c). */
    extern int   port_light_ambient_override;
    extern int   port_light_ambient_rgb[3];
    extern int   port_light_disable_fixed;
    extern int   port_light_group_disabled[8];
    extern int   port_light_disable_dynamic;
    extern int   port_light_dyn_slot_muted[2][8];
    extern float port_light_ambient_scale;
    extern int   port_force_gouraud_neutral;
    extern int   port_light_dump_request;
}

extern "C" void imgui_init(SDL_Window *window, SDL_Renderer *renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    /* HiDPI scaling. On macOS, SDL_WINDOW_ALLOW_HIGHDPI makes the drawable
       2x the window size on Retina displays and ImGui handles this via
       DisplayFramebufferScale — FontGlobalScale stays 1.0. On Linux, the
       drawable equals the window size even on HiDPI screens, so we query
       the display DPI and scale the font accordingly. */
    float dpi_scale = 1.0f;
#ifndef __APPLE__
    {
        float ddpi = 0.0f;
        int display = SDL_GetWindowDisplayIndex(window);
        if (SDL_GetDisplayDPI(display >= 0 ? display : 0, &ddpi, NULL, NULL) == 0 && ddpi > 0.0f) {
            dpi_scale = ddpi / 96.0f;
            if (dpi_scale < 1.0f) dpi_scale = 1.0f;
            /* Snap to nearest 0.5 to avoid odd fractional scales */
            dpi_scale = floorf(dpi_scale * 2.0f + 0.5f) / 2.0f;
        }
    }
#endif
    io.FontGlobalScale = dpi_scale;
    ImGui::StyleColorsDark();

    g_imgui_use_gl = (renderer == nullptr);
    if (g_imgui_use_gl) {
        ImGui_ImplSDL2_InitForOpenGL(window, SDL_GL_GetCurrentContext());
        ImGui_ImplOpenGL3_Init("#version 330 core");
    } else {
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);
    }
}

extern "C" void imgui_shutdown(void)
{
    if (g_imgui_use_gl)
        ImGui_ImplOpenGL3_Shutdown();
    else
        ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

extern "C" void imgui_process_event(SDL_Event *event)
{
    ImGui_ImplSDL2_ProcessEvent(event);
}

extern "C" void imgui_toggle_actors(void) { show_debug = !show_debug; }
extern "C" void imgui_toggle_camera(void) { show_debug = !show_debug; }
extern "C" void imgui_toggle_debug(void)  { show_debug = !show_debug; }

extern "C" void imgui_render(SDL_Renderer *renderer)
{
    if (!show_debug) return;

    if (g_imgui_use_gl)
        ImGui_ImplOpenGL3_NewFrame();
    else
        ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(620, 720), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("MGS Debug (F1)", &show_debug)) {
        /* Sticky header — always-visible summary + quick actions.
         *   line 1: perf + stage + Snake pos
         *   line 2: active-override badges (visual cue when non-default
         *           state is in effect, so it's obvious why the scene
         *           looks unusual)
         *   line 3: quick actions (Quit / Restart) */
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS %.1f  |  %.2f ms  |  GL %s  |  tick %d",
                    io.Framerate, 1000.0f / (io.Framerate > 1.0f ? io.Framerate : 1.0f),
                    g_imgui_use_gl ? "on" : "off", GV_Time);
        ImGui::Text("Stage: %s   Snake: (%d, %d, %d)",
                    port_current_stage[0] ? port_current_stage : "(none)",
                    GM_PlayerPosition.vx, GM_PlayerPosition.vy, GM_PlayerPosition.vz);

        /* Active override badges. Each non-default toggle / mode lights
         * up so the user can see at a glance why the scene differs from
         * a stock run. Click any badge to clear that override. */
        {
            extern int gl_debug_wireframe, gl_debug_no_textures, gl_debug_skip_3d;
            extern int gl_debug_skip_2d, gl_debug_skip_lines, gl_debug_face_id;
            extern int gl_debug_show_normals, gl_debug_clear_override;
            extern int port_light_ambient_override, port_light_disable_fixed;
            extern int port_light_disable_dynamic, port_force_gouraud_neutral;
            extern int port_freecam_enabled, port_freecam_first_person;
            extern int port_actor_speed;

            struct Badge { const char *name; int *flag; const char *off_value; };
            int actor_off = 1;
            Badge badges[] = {
                {"FREECAM",     &port_freecam_enabled,        nullptr},
                {"FP-VIEW",     &port_freecam_first_person,   nullptr},
                {"OVERRIDE-CAM",&imgui_cam_override,          nullptr},
                {"WIREFRAME",   &gl_debug_wireframe,          nullptr},
                {"NO-TEX",      &gl_debug_no_textures,        nullptr},
                {"SKIP-3D",     &gl_debug_skip_3d,            nullptr},
                {"SKIP-2D",     &gl_debug_skip_2d,            nullptr},
                {"SKIP-LINES",  &gl_debug_skip_lines,         nullptr},
                {"FACE-ID",     &gl_debug_face_id,            nullptr},
                {"NORMALS",     &gl_debug_show_normals,       nullptr},
                {"CLEAR-OVR",   &gl_debug_clear_override,     nullptr},
                {"AMB-OVR",     &port_light_ambient_override, nullptr},
                {"NO-FIXED-LT", &port_light_disable_fixed,    nullptr},
                {"NO-DYN-LT",   &port_light_disable_dynamic,  nullptr},
                {"GOURAUD-FLAT",&port_force_gouraud_neutral,  nullptr},
            };
            int n_active = 0;
            for (auto &b : badges) if (*b.flag) n_active++;
            bool actor_speed_active = (port_actor_speed != 1);
            int total_active = n_active + (actor_speed_active ? 1 : 0);

            if (total_active == 0) {
                ImGui::TextDisabled("(no debug overrides active)");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                                   "%d override%s active:", total_active,
                                   total_active == 1 ? "" : "s");
                ImGui::SameLine();
                /* Speed badge first — non-binary so it gets its own style. */
                if (actor_speed_active) {
                    char buf[24];
                    if (port_actor_speed == 0) snprintf(buf, sizeof buf, "PAUSED");
                    else if (port_actor_speed > 1) snprintf(buf, sizeof buf, "%dx", port_actor_speed);
                    else snprintf(buf, sizeof buf, "1/%dx", 1 - port_actor_speed);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.15f, 1.0f));
                    if (ImGui::SmallButton(buf)) port_actor_speed = 1;
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("Click to reset Actor Speed to 1x.");
                    ImGui::SameLine();
                }
                for (auto &b : badges) {
                    if (!*b.flag) continue;
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.30f, 0.30f, 1.0f));
                    if (ImGui::SmallButton(b.name)) *b.flag = 0;
                    ImGui::PopStyleColor(2);
                    ImGui::SetItemTooltip("Click to clear this override.");
                    ImGui::SameLine();
                }
                ImGui::NewLine();
            }
        }

        /* Quick-toggle row — the handful of debug flags users flip most
         * often, all in one row. State lives in the same globals the
         * full Renderer / Camera tabs edit, so toggles stay in sync. */
        {
            extern int gl_debug_wireframe, gl_debug_no_textures;
            extern int port_freecam_enabled, port_freecam_first_person;
            extern int port_actor_speed;
            bool b;
            b = gl_debug_wireframe != 0;
            if (ImGui::Checkbox("Wire", &b)) gl_debug_wireframe = b;
            ImGui::SetItemTooltip("Wireframe rendering (also under Renderer tab).");
            ImGui::SameLine();
            b = gl_debug_no_textures != 0;
            if (ImGui::Checkbox("No-tex", &b)) gl_debug_no_textures = b;
            ImGui::SetItemTooltip("Skip texture sample; flat vertex color only.");
            ImGui::SameLine();
            b = port_freecam_enabled != 0;
            if (ImGui::Checkbox("Freecam", &b)) port_freecam_enabled = b;
            ImGui::SetItemTooltip("Toggle the over-the-shoulder freecam (also R3 in-game).");
            ImGui::SameLine();
            ImGui::BeginDisabled(!port_freecam_enabled);
            b = port_freecam_first_person != 0;
            if (ImGui::Checkbox("FP", &b)) port_freecam_first_person = b;
            ImGui::SetItemTooltip("Freecam: position the eye AT Snake's head.");
            ImGui::EndDisabled();
            ImGui::SameLine();
            b = (port_actor_speed == 0);
            if (ImGui::Checkbox("Pause", &b)) port_actor_speed = b ? 0 : 1;
            ImGui::SetItemTooltip("Pause the actor system (rendering keeps running).");
        }

        /* Quick action row. */
        {
            extern bool g_running;
            extern const char *port_argv0;
            if (ImGui::SmallButton("Quit")) g_running = false;
            ImGui::SetItemTooltip("Exit the game cleanly.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Restart") && port_argv0)
                execl(port_argv0, port_argv0, (char *)NULL);
            ImGui::SetItemTooltip("Re-exec the current binary (preserves args).");
            ImGui::SameLine();
            extern bool imgui_request_screenshot;
            if (ImGui::SmallButton("Screenshot")) imgui_request_screenshot = true;
            ImGui::SetItemTooltip("Write port/screenshots/mgs_YYYY-MM-DD_HHMMSS.png\nfrom the current FBO (PSX-resolution × PORT_GL_SCALE).");
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextDisabled("F1 toggles this window");
        }

        ImGui::Separator();

        /* Actor speed: throttle / accelerate / pause / single-step the actor
           system (GV_ExecActorSystem). Rendering keeps running at 60fps so
           paused-state frames stay visible. Useful for stepping through
           camera transitions, animation glitches, etc. */
        {
            extern int port_actor_speed;
            extern int port_actor_step;

            const char *label;
            char buf[32];
            if (port_actor_speed == 0)       label = "Paused";
            else if (port_actor_speed == 1)  label = "Normal (1x)";
            else if (port_actor_speed >  0)  { snprintf(buf, sizeof buf, "%dx faster", port_actor_speed); label = buf; }
            else                              { snprintf(buf, sizeof buf, "1/%dx slower", 1 - port_actor_speed); label = buf; }

            ImGui::Text("Actor speed:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s", label);

            ImGui::SliderInt("##actor_speed", &port_actor_speed, -8, 8, "");
            ImGui::SameLine();
            if (ImGui::Button("1x##actor_reset"))    port_actor_speed = 1;
            ImGui::SameLine();
            if (ImGui::Button(port_actor_speed == 0 ? "Resume" : "Pause"))
                port_actor_speed = (port_actor_speed == 0) ? 1 : 0;
            ImGui::SameLine();
            ImGui::BeginDisabled(port_actor_speed != 0);
            if (ImGui::Button("Step"))    port_actor_step  = 1;
            ImGui::SetItemTooltip("Advance the actor system by exactly 1 frame while paused.\nRendering keeps running so paused state stays visible.");
            ImGui::SameLine();
            if (ImGui::Button("Step 10")) port_actor_step  = 10;
            ImGui::SetItemTooltip("Advance the actor system by 10 frames.");
            ImGui::EndDisabled();

            ImGui::Separator();
        }

        if (ImGui::BeginTabBar("##mgs_tabs", ImGuiTabBarFlags_Reorderable)) {
            /* ---------------------------------------------------------- */
            /* RENDERER                                                   */
            /* ---------------------------------------------------------- */
            if (ImGui::BeginTabItem("Renderer")) {
                PortDG_CHANL_Partial *chanl = &DG_Chanls[1];
                extern int port_prim_counts[8];
                extern int gl_debug_wireframe, gl_debug_no_textures;
                extern int gl_debug_skip_3d,   gl_debug_skip_2d, gl_debug_skip_lines;
                extern int gl_debug_blit_nearest;
                extern int gl_renderer_get_scale(void);
                extern void gl_renderer_set_scale(int);
                extern int gl_renderer_get_widescreen(void);
                extern void gl_renderer_set_widescreen(int);

                ImGui::Text("Faces drawn this frame: %d", port_last_drawn_faces);
                ImGui::Text("Objects queued: %d   GroupID: 0x%X",
                            chanl->objs_index, DG_CurrentGroupID);
                ImGui::Text("DG_FrameRate: %d", DG_FrameRate);
                ImGui::Text("Primitives (this frame):");
                ImGui::Indent();
                static const char *pnames[8] = {
                    "TILE", "POLY_F", "POLY_G", "POLY_FT",
                    "POLY_GT", "SPRT", "LINE", "other"
                };
                for (int i = 0; i < 8; i++)
                    ImGui::Text("  %-8s %5d", pnames[i], port_prim_counts[i]);
                ImGui::Unindent();

                if (ImGui::CollapsingHeader("Lighting",
                                            ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Applies to DG_FLAG_SHADE objects only (level geo).");
                    ImGui::RadioButton("Gouraud (per-vertex)", &imgui_per_pixel_light, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("Per-pixel (PSX NCS)",   &imgui_per_pixel_light, 1);
                }

                if (ImGui::CollapsingHeader("Visualization",
                                            ImGuiTreeNodeFlags_DefaultOpen))
                {
                    extern int gl_debug_no_cull, gl_debug_face_id, gl_debug_show_normals;
                    extern int gl_debug_clear_override;
                    extern int gl_debug_cull_cw;
                    extern float gl_debug_clear_rgb[3];
                    bool b;
                    b = gl_debug_wireframe != 0;
                    if (ImGui::Checkbox("Wireframe", &b)) gl_debug_wireframe = b;
                    ImGui::SetItemTooltip("glPolygonMode(GL_LINE) — every tri rendered as edges only.");
                    ImGui::SameLine();
                    b = gl_debug_no_textures != 0;
                    if (ImGui::Checkbox("No textures", &b)) gl_debug_no_textures = b;
                    ImGui::SetItemTooltip("Skip the texture sample in the fragment shader; outputs flat vertex color.");
                    ImGui::SameLine();
                    b = gl_debug_no_cull != 0;
                    if (ImGui::Checkbox("No cull", &b)) gl_debug_no_cull = b;
                    ImGui::SetItemTooltip("Skip CPU backface cull — useful for seeing inside hollow models.");

                    b = gl_debug_cull_cw != 0;
                    if (ImGui::Checkbox("Flip winding", &b)) gl_debug_cull_cw = b;
                    ImGui::SetItemTooltip("Treat clockwise tris as front-facing. Diagnoses inside-out models.");
                    ImGui::SameLine();
                    b = port_force_gouraud_neutral != 0;
                    if (ImGui::Checkbox("Flat 128", &b)) port_force_gouraud_neutral = b ? 1 : 0;
                    ImGui::SetItemTooltip("Skip shade-pipeline output; force neutral (128,128,128) Gouraud. Isolates lighting vs texture issues.");
                    ImGui::SameLine();
                    bool vram_dbg = port_vram_debug_view() != 0;
                    if (ImGui::Checkbox("VRAM view", &vram_dbg)) port_vram_toggle_debug();
                    ImGui::SetItemTooltip("Show the full 1024x512 PSX VRAM image instead of the rendered scene.");

                    b = gl_debug_face_id != 0;
                    if (ImGui::Checkbox("Face IDs", &b)) gl_debug_face_id = b;
                    ImGui::SetItemTooltip("Random color per triangle (hashed gl_PrimitiveID). Counts polys visually.");
                    ImGui::SameLine();
                    b = gl_debug_show_normals != 0;
                    if (ImGui::Checkbox("Normals", &b)) gl_debug_show_normals = b;
                    ImGui::SetItemTooltip("Output the interpolated normal as RGB (N*0.5+0.5).");

                    ImGui::Spacing();
                    b = gl_debug_clear_override != 0;
                    if (ImGui::Checkbox("Override clear color", &b))
                        gl_debug_clear_override = b;
                    if (gl_debug_clear_override) {
                        ImGui::SameLine();
                        ImGui::ColorEdit3("##clrcol", gl_debug_clear_rgb,
                            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                    }
                }

                if (ImGui::CollapsingHeader("Render passes"))
                {
                    ImGui::TextDisabled("Toggle individual passes to isolate bugs.");
                    bool b;
                    b = gl_debug_skip_3d == 0;
                    if (ImGui::Checkbox("Draw 3D", &b)) gl_debug_skip_3d = !b;
                    b = gl_debug_skip_2d == 0;
                    if (ImGui::Checkbox("Draw 2D triangles", &b)) gl_debug_skip_2d = !b;
                    b = gl_debug_skip_lines == 0;
                    if (ImGui::Checkbox("Draw 2D lines", &b)) gl_debug_skip_lines = !b;
                }

                if (ImGui::CollapsingHeader("Shaders"))
                {
                    ImGui::TextDisabled("GLSL lives in port/libdg/shaders/*.{vert,frag}.");
                    ImGui::TextDisabled("Edit, save, click reload -- no rebuild needed.");
                    extern int gl_renderer_reload_shaders(void);
                    if (ImGui::Button("Reload shaders (F5)")) {
                        gl_renderer_reload_shaders();
                    }
                }

                if (ImGui::CollapsingHeader("Effects"))
                {
                    extern int   port_blur_enabled;
                    extern float port_blur_strength;
                    bool b = port_blur_enabled != 0;
                    if (ImGui::Checkbox("Motion blur (NewBlur / NewBlurPure)", &b))
                        port_blur_enabled = b ? 1 : 0;
                    ImGui::SameLine();
                    ImGui::TextDisabled("(2D fb-readback)");
                    ImGui::BeginDisabled(!b);
                    ImGui::SliderFloat("Blur strength", &port_blur_strength,
                                       0.0f, 2.5f, "%.2f");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset")) port_blur_strength = 1.4f;
                    ImGui::TextDisabled("PSX-exact = 2.0, default = 1.4, 0 = off");
                    ImGui::EndDisabled();
                }

                if (ImGui::CollapsingHeader("Quality / Output"))
                {
                    int scale = gl_renderer_get_scale();
                    if (ImGui::SliderInt("Internal scale (PORT_GL_SCALE)", &scale, 1, 8)) {
                        gl_renderer_set_scale(scale);
                    }
                    int fbo_w = 320 * scale, fbo_h = 224 * scale;
                    ImGui::TextDisabled("FBO size: %d x %d", fbo_w, fbo_h);

                    bool b = gl_debug_blit_nearest != 0;
                    if (ImGui::Checkbox("Upscale with GL_NEAREST (sharp / blocky)", &b))
                        gl_debug_blit_nearest = b;

                    /* Fullscreen toggle (matches the F11 / Alt+Enter hotkey). */
                    SDL_Window *win = SDL_GL_GetCurrentWindow();
                    if (!win) win = SDL_GetWindowFromID(1);
                    if (win) {
                        Uint32 wf = SDL_GetWindowFlags(win);
                        bool fs = (wf & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                        if (ImGui::Checkbox("Fullscreen (F11)", &fs))
                            SDL_SetWindowFullscreen(win,
                                fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    }

                    bool ws = gl_renderer_get_widescreen() != 0;
                    if (ImGui::Checkbox("Widescreen (16:9 Hor+)", &ws))
                        gl_renderer_set_widescreen(ws ? 1 : 0);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(3D widens; HUD stays 4:3)");
                }

                if (ImGui::CollapsingHeader("GL driver info"))
                {
                    if (g_imgui_use_gl) {
                        static const char *gl_vendor   = NULL;
                        static const char *gl_renderer = NULL;
                        static const char *gl_version  = NULL;
                        if (!gl_vendor) {
                            typedef const unsigned char *(*PFNGLGETSTRING)(unsigned int);
                            PFNGLGETSTRING f = (PFNGLGETSTRING)
                                SDL_GL_GetProcAddress("glGetString");
                            if (f) {
                                gl_vendor   = (const char *)f(0x1F00);
                                gl_renderer = (const char *)f(0x1F01);
                                gl_version  = (const char *)f(0x1F02);
                            }
                        }
                        if (gl_vendor) {
                            ImGui::Text("Vendor:   %s", gl_vendor);
                            ImGui::Text("Renderer: %s", gl_renderer);
                            ImGui::Text("Version:  %s", gl_version);
                        }
                    } else {
                        ImGui::TextDisabled("(SDL_Renderer path; no GL context)");
                    }
                }

                ImGui::EndTabItem();
            }

            /* ---------------------------------------------------------- */
            /* CAMERA                                                     */
            /* ---------------------------------------------------------- */
            if (ImGui::BeginTabItem("Camera")) {
                PortDG_CHANL_Partial *chanl = &DG_Chanls[1];
                PortGM_CAMERA *gc = &GM_Camera;
                PortCamStruct2 *s2 = &gUnkCameraStruct2_800B7868;
                PortCamStructB *sb = &gUnkCameraStruct_800B77B8;

                int dx = chanl->eye.t[0] - GM_PlayerPosition.vx;
                int dz = chanl->eye.t[2] - GM_PlayerPosition.vz;
                ImGui::Text("Snake: (%d, %d, %d)  Eye.t: (%d, %d, %d)",
                    GM_PlayerPosition.vx, GM_PlayerPosition.vy, GM_PlayerPosition.vz,
                    chanl->eye.t[0], chanl->eye.t[1], chanl->eye.t[2]);
                ImGui::Text("cam-snake dx=%d dz=%d   clip_dist=%d",
                            dx, dz, chanl->clip_distance);

                if (ImGui::TreeNode("GM_Camera (target)")) {
                    ImGui::Text("eye:    (%d, %d, %d)", gc->eye.vx, gc->eye.vy, gc->eye.vz);
                    ImGui::Text("center: (%d, %d, %d)", gc->center.vx, gc->center.vy, gc->center.vz);
                    ImGui::Text("rotate: (%d, %d, %d)", gc->rotate.vx, gc->rotate.vy, gc->rotate.vz);
                    ImGui::Text("zoom=%d fp=%d flags=0x%X track=%d interp=%d",
                        gc->zoom, gc->first_person, gc->flags, gc->track, gc->interp);
                    ImGui::Text("alert_mask=%d pan=(%d,%d,%d)",
                        gc->alert_mask, gc->pan.vx, gc->pan.vy, gc->pan.vz);
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode("Struct2_7868 (smoothed -> DG_LookAt)")) {
                    ImGui::Text("eye:    (%d, %d, %d)", s2->eye.vx, s2->eye.vy, s2->eye.vz);
                    ImGui::Text("center: (%d, %d, %d)", s2->center.vx, s2->center.vy, s2->center.vz);
                    ImGui::Text("rotate: (%d, %d, %d)", s2->rotate.vx, s2->rotate.vy, s2->rotate.vz);
                    ImGui::Text("type=%d track=%d zoom=%d", s2->type, s2->track, s2->zoom);
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode("StructB_77B8 (zone camera source)")) {
                    ImGui::Text("eye:     (%d, %d, %d)", sb->eye.vx, sb->eye.vy, sb->eye.vz);
                    ImGui::Text("center:  (%d, %d, %d)", sb->center.vx, sb->center.vy, sb->center.vz);
                    ImGui::Text("rotate:  (%d, %d, %d)", sb->rotate.vx, sb->rotate.vy, sb->rotate.vz);
                    ImGui::Text("track=%d interp=%d", sb->track, sb->interp);
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode("DG_Chanls[1] (final view matrix)")) {
                    ImGui::Text("eye_inv.t:    (%d, %d, %d)",
                        chanl->eye_inv.t[0], chanl->eye_inv.t[1], chanl->eye_inv.t[2]);
                    ImGui::Text("eye_inv.m[0]: %6d %6d %6d",
                        chanl->eye_inv.m[0][0], chanl->eye_inv.m[0][1], chanl->eye_inv.m[0][2]);
                    ImGui::Text("eye_inv.m[1]: %6d %6d %6d",
                        chanl->eye_inv.m[1][0], chanl->eye_inv.m[1][1], chanl->eye_inv.m[1][2]);
                    ImGui::Text("eye_inv.m[2]: %6d %6d %6d",
                        chanl->eye_inv.m[2][0], chanl->eye_inv.m[2][1], chanl->eye_inv.m[2][2]);
                    ImGui::TreePop();
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Manual eye_inv override");
                ImGui::Checkbox("Override Camera", (bool *)&imgui_cam_override);
                ImGui::SetItemTooltip(
                    "Write the sliders below directly into DG_Chanls[1].eye_inv\n"
                    "each frame. Bypasses the engine's camera_act output. Use to\n"
                    "diff specific camera matrices against a reference.");
                if (imgui_cam_override) {
                    /* Re-init the slider state every time the checkbox flips
                     * back on, so the override starts from the current camera
                     * matrix instead of a stale value from the last session. */
                    static bool inited = false;
                    static bool prev_on = false;
                    if (!prev_on) inited = false;
                    prev_on = true;
                    if (!inited) {
                        for (int r = 0; r < 3; r++)
                            for (int c = 0; c < 3; c++)
                                imgui_cam_eye_inv_m[r][c] = chanl->eye_inv.m[r][c];
                        imgui_cam_eye_inv_t[0] = chanl->eye_inv.t[0];
                        imgui_cam_eye_inv_t[1] = chanl->eye_inv.t[1];
                        imgui_cam_eye_inv_t[2] = chanl->eye_inv.t[2];
                        imgui_cam_clip_dist = chanl->clip_distance;
                        inited = true;
                    }
                    ImGui::SliderInt("t[0] X", &imgui_cam_eye_inv_t[0], -30000, 30000);
                    ImGui::SliderInt("t[1] Y", &imgui_cam_eye_inv_t[1], -30000, 30000);
                    ImGui::SliderInt("t[2] Z", &imgui_cam_eye_inv_t[2], -30000, 30000);
                    ImGui::SliderInt("dist",   &imgui_cam_clip_dist, 50, 2000);
                    for (int r = 0; r < 3; r++) {
                        for (int c = 0; c < 3; c++) {
                            char label[16];
                            snprintf(label, sizeof(label), "m[%d][%d]", r, c);
                            int val = imgui_cam_eye_inv_m[r][c];
                            ImGui::PushItemWidth(80);
                            if (ImGui::DragInt(label, &val, 10, -4096, 4096))
                                imgui_cam_eye_inv_m[r][c] = (short)val;
                            ImGui::PopItemWidth();
                            if (c < 2) ImGui::SameLine();
                        }
                    }
                    if (ImGui::Button("Reset")) { inited = false; imgui_cam_override = 0; }
                } else {
                    static bool prev_on = false;
                    prev_on = false;
                }

                /* Over-the-shoulder freecam — modern third-person camera.
                 * R-stick drives yaw/pitch, smoothed; shoulder offset
                 * frames Snake to the side; left-stick "forward" pushes
                 * Snake away from the camera. R3 toggles. */
                ImGui::Spacing();
                ImGui::SeparatorText("Over-the-shoulder camera");
                extern int port_freecam_enabled;
                extern int port_freecam_yaw, port_freecam_pitch;
                extern int port_freecam_dist, port_freecam_height_off;
                extern int port_freecam_shoulder_off;
                extern int port_freecam_yaw_speed, port_freecam_pitch_speed;
                extern int port_freecam_smoothing, port_freecam_invert_y;
                extern int port_freecam_use_stick, port_freecam_rotate_input;
                extern int port_freecam_R3_toggle;
                extern int port_freecam_pad_offset, port_freecam_pad_sign;
                extern int port_freecam_first_person;

                ImGui::Checkbox("Enable##otscam", (bool *)&port_freecam_enabled);
                ImGui::SameLine();
                ImGui::TextDisabled("(or press R3 in-game)");
                ImGui::SameLine();
                ImGui::Checkbox("First person##otscam", (bool *)&port_freecam_first_person);

                ImGui::BeginDisabled(!port_freecam_enabled);
                if (ImGui::TreeNodeEx("Framing", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::SliderInt("Distance",       &port_freecam_dist,         100,  6000);
                    ImGui::SliderInt("Head Y offset",  &port_freecam_height_off,  -300,  300);
                    ImGui::TextDisabled("PSX +Y is down; negative = camera target moves up.");
                    ImGui::SliderInt("Shoulder offset", &port_freecam_shoulder_off, -300, 300);
                    ImGui::TextDisabled("Lateral offset of the look-at from Snake's pivot.");
                    ImGui::TreePop();
                }
                if (ImGui::TreeNodeEx("Input", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Right-stick controls camera", (bool *)&port_freecam_use_stick);
                    ImGui::Checkbox("Invert Y", (bool *)&port_freecam_invert_y);
                    ImGui::Checkbox("Left-stick rotated to camera",
                                    (bool *)&port_freecam_rotate_input);
                    if (port_freecam_rotate_input) {
                        ImGui::Indent();
                        ImGui::SliderInt("Pad-yaw offset", &port_freecam_pad_offset, 0, 4095);
                        ImGui::Text("Pad-yaw sign:");
                        ImGui::SameLine();
                        if (ImGui::RadioButton("+1##sign", port_freecam_pad_sign ==  1)) port_freecam_pad_sign =  1;
                        ImGui::SameLine();
                        if (ImGui::RadioButton("-1##sign", port_freecam_pad_sign == -1)) port_freecam_pad_sign = -1;
                        ImGui::TextDisabled("Try the 8 combos until pressing UP on the stick walks Snake away from the camera. Then we bake the values in.");
                        if (ImGui::Button("0##po"))    port_freecam_pad_offset = 0;
                        ImGui::SameLine();
                        if (ImGui::Button("1024##po")) port_freecam_pad_offset = 1024;
                        ImGui::SameLine();
                        if (ImGui::Button("2048##po")) port_freecam_pad_offset = 2048;
                        ImGui::SameLine();
                        if (ImGui::Button("3072##po")) port_freecam_pad_offset = 3072;
                        ImGui::Unindent();
                    }
                    ImGui::Checkbox("R3 toggles freecam", (bool *)&port_freecam_R3_toggle);
                    ImGui::SliderInt("Yaw speed",   &port_freecam_yaw_speed,   1, 400);
                    ImGui::SliderInt("Pitch speed", &port_freecam_pitch_speed, 1, 400);
                    ImGui::SliderInt("Smoothing",   &port_freecam_smoothing,   0, 20);
                    ImGui::TextDisabled("0=snappy/raw, 6=cinematic, 20=very floaty.");
                    ImGui::TreePop();
                }
                if (ImGui::TreeNodeEx("Manual angle", 0)) {
                    ImGui::SliderInt("Yaw",   &port_freecam_yaw,   0,  4095, "%d / 4096");
                    ImGui::SliderInt("Pitch", &port_freecam_pitch, 16, 2032, "%d / 4096");
                    if (ImGui::Button("Behind Snake")) { port_freecam_yaw = 0;    port_freecam_pitch = 1024; }
                    ImGui::SameLine();
                    if (ImGui::Button("Top-down"))     { port_freecam_pitch = 64; }
                    ImGui::SameLine();
                    if (ImGui::Button("Eye-level"))    { port_freecam_pitch = 1024; }
                    ImGui::TreePop();
                }
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }

            /* ---------------------------------------------------------- */
            /* ACTORS                                                     */
            /* ---------------------------------------------------------- */
            if (ImGui::BeginTabItem("Actors")) {
                /* -------- Spawn-any-actor --------------------------------
                 * Enumerate every entry in both registries (main binary +
                 * current stage overlay) and let the user spawn one. The
                 * actor's constructor signature varies — the canonical
                 * GCL entry-point is (int name, int where, int argc,
                 * char **argv) per source/game/script.c:475-493, so we
                 * call it that way with argc=0/argv=NULL. `where` is
                 * a HZD-zone bitmask; 0 = "no zone", which is what most
                 * actors check before reading map data.
                 *
                 * Caveats (tooltip below):
                 *  - Position is read from collision/HZD data via `where`,
                 *    not from the camera; many actors will spawn at a
                 *    map-bound location, not wherever you're looking.
                 *  - Some actors expect specific argv args and may crash
                 *    when invoked with argc=0. Treat this as a dev aid. */
                {
                    /* Build a unified list of {class_id, func, table_tag}
                     * from MainCharacterEntries + StageCharacterEntries. */
                    struct Row {
                        unsigned short id;
                        PortNEWCHARA   func;
                        const char    *src;   /* "main" or "stage" */
                        const char    *name;  /* from strcode_to_string, may be NULL */
                    };
                    static Row rows[1024];
                    static int n_rows = 0;
                    static int last_stage_entries_addr = -1;
                    /* Re-enumerate when the stage's chara table pointer
                     * changes (i.e., stage transition) or first time. */
                    int cur_stage_addr = (int)(intptr_t)StageCharacterEntries;
                    if (cur_stage_addr != last_stage_entries_addr) {
                        last_stage_entries_addr = cur_stage_addr;
                        n_rows = 0;
                        for (PortCHARA *p = MainCharacterEntries;
                             p->func != NULL && n_rows < 1024; ++p) {
                            rows[n_rows].id   = p->class_id;
                            rows[n_rows].func = p->func;
                            rows[n_rows].src  = "main";
                            rows[n_rows].name = strcode_to_string(p->class_id);
                            n_rows++;
                        }
                        PortCHARA *st = (PortCHARA *)StageCharacterEntries;
                        if (st) {
                            for (; st->func != NULL && n_rows < 1024; ++st) {
                                rows[n_rows].id   = st->class_id;
                                rows[n_rows].func = st->func;
                                rows[n_rows].src  = "stage";
                                rows[n_rows].name = strcode_to_string(st->class_id);
                                n_rows++;
                            }
                        }
                    }

                    ImGui::SeparatorText("Spawn actor");
                    static char filter[64] = {0};
                    static int  sel = -1;
                    static int  where = 0;
                    ImGui::SetNextItemWidth(180);
                    ImGui::InputTextWithHint("##spawnfilter", "filter (name or 0xHHHH)",
                                             filter, sizeof(filter));
                    ImGui::SameLine();
                    char preview[64];
                    if (sel >= 0 && sel < n_rows) {
                        snprintf(preview, sizeof preview, "0x%04X %s",
                                 rows[sel].id, rows[sel].name ? rows[sel].name : "(unknown)");
                    } else {
                        snprintf(preview, sizeof preview, "(pick an actor)");
                    }
                    ImGui::SetNextItemWidth(260);
                    if (ImGui::BeginCombo("##spawnpick", preview)) {
                        for (int i = 0; i < n_rows; i++) {
                            const char *nm = rows[i].name ? rows[i].name : "";
                            char hex[8]; snprintf(hex, sizeof hex, "%04x", rows[i].id);
                            /* Case-insensitive substring filter on name + hex hash. */
                            if (filter[0]) {
                                bool match_name = false, match_hex = false;
                                if (nm[0]) {
                                    for (const char *s = nm; *s; ++s) {
                                        const char *a = s, *b = filter;
                                        while (*a && *b &&
                                               (*a | 0x20) == (*b | 0x20)) { a++; b++; }
                                        if (!*b) { match_name = true; break; }
                                    }
                                }
                                if (!match_name) {
                                    for (const char *s = hex; *s; ++s) {
                                        const char *a = s, *b = filter;
                                        while (*a && *b &&
                                               (*a | 0x20) == (*b | 0x20)) { a++; b++; }
                                        if (!*b) { match_hex = true; break; }
                                    }
                                }
                                if (!match_name && !match_hex) continue;
                            }
                            char label[80];
                            snprintf(label, sizeof label, "[%s] 0x%04X  %s",
                                     rows[i].src, rows[i].id, nm[0] ? nm : "(unknown)");
                            if (ImGui::Selectable(label, sel == i)) sel = i;
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetNextItemWidth(120);
                    ImGui::InputInt("where (HZD mask)", &where);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(sel < 0 || sel >= n_rows);
                    if (ImGui::Button("Spawn")) {
                        if (sel >= 0 && sel < n_rows && rows[sel].func) {
                            /* GCL-style entry: (name, where, argc, argv). */
                            rows[sel].func((int)rows[sel].id, where, 0, (char **)NULL);
                        }
                    }
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("%d entries (%d match filter)", n_rows,
                        [&]{ int c=0; for (int i=0;i<n_rows;i++) {
                            const char *nm = rows[i].name ? rows[i].name : "";
                            if (!filter[0]) { c++; continue; }
                            const char *a, *b;
                            for (const char *s=nm; *s; ++s) {
                                a=s; b=filter;
                                while (*a && *b && (*a|0x20)==(*b|0x20)) { a++; b++; }
                                if (!*b) { c++; goto _next; }
                            }
                            { char hex[8]; snprintf(hex,sizeof hex,"%04x",rows[i].id);
                              for (const char *s=hex; *s; ++s) {
                                a=s; b=filter;
                                while (*a && *b && (*a|0x20)==(*b|0x20)) { a++; b++; }
                                if (!*b) { c++; break; }
                              }
                            }
                            _next:; }
                          return c; }());
                    ImGui::TextDisabled("Position is read from HZD via `where`, not passed directly. "
                                        "Some actors expect specific argv args and may crash with argc=0.");
                    ImGui::Spacing();
                    ImGui::SeparatorText("Live actors");
                }

                const char *level_names[] = {
                    "DAEMON", "MANAGER", "LEVEL2", "LEVEL3",
                    "LEVEL4", "LEVEL5", "DAEMON2"
                };

                /* Live-actor filter — substring match against actor->filename.
                 * Auto-expands level trees that contain matches; collapses
                 * the rest. */
                static char live_filter[64] = {0};
                static bool show_only_active = false;
                ImGui::SetNextItemWidth(180);
                ImGui::InputTextWithHint("##livefilter", "filter (filename substring)",
                                         live_filter, sizeof(live_filter));
                ImGui::SameLine();
                ImGui::Checkbox("Active only", &show_only_active);
                ImGui::SetItemTooltip("Hide actors whose act callback is NULL (DEAD entries).");

                auto match_filter = [](const char *s) -> bool {
                    if (!live_filter[0]) return true;
                    if (!s) return false;
                    for (const char *p = s; *p; ++p) {
                        const char *a = p, *b = live_filter;
                        while (*a && *b && (*a | 0x20) == (*b | 0x20)) { a++; b++; }
                        if (!*b) return true;
                    }
                    return false;
                };

                int total = 0, total_matches = 0;
                for (int lv = 0; lv < 7; lv++) {
                    PortActorList *list = (PortActorList *)port_gv_actor_list(lv);
                    if (!list) continue;
                    ActorNode *head = &list->first;
                    ActorNode *cur = head->next;
                    int count = 0, matches = 0;
                    while (cur && cur != &list->last && count < 256) {
                        count++;
                        bool active = (cur->act != NULL);
                        if ((!show_only_active || active) && match_filter(cur->filename))
                            matches++;
                        cur = cur->next;
                    }
                    if (count == 0) continue;
                    total += count;
                    total_matches += matches;
                    if (matches == 0 && (live_filter[0] || show_only_active)) continue;
                    /* Auto-open trees with matches when a filter is active. */
                    if (live_filter[0] || show_only_active)
                        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                    if (ImGui::TreeNode(level_names[lv], "%s (%d, %d match) pause=%d kill=%d",
                                        level_names[lv], count, matches,
                                        list->pause, list->kill))
                    {
                        cur = head->next;
                        int idx = 0;
                        while (cur && cur != &list->last && idx < 256) {
                            const char *name = cur->filename ? cur->filename : "???";
                            bool active = (cur->act != NULL);
                            bool show = (show_only_active ? active : true)
                                     && match_filter(cur->filename);
                            if (!show) { cur = cur->next; idx++; continue; }
                            if (!active)
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                            ImGui::Text("[%d] %-20s act=%s ticks=%d rt=%d",
                                idx, name, active ? "Y" : "DEAD", cur->count, cur->runtime);
                            ImGui::SameLine();
                            if (active && ImGui::SmallButton("Kill")) {
                                ActorNode *next = cur->next;
                                GV_DestroyActorQuick(cur);
                                cur = next; idx++;
                                continue;
                            }
                            if (!active) ImGui::PopStyleColor();
                            cur = cur->next; idx++;
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::Separator();
                if (live_filter[0] || show_only_active)
                    ImGui::Text("Total: %d actors  (%d shown)", total, total_matches);
                else
                    ImGui::Text("Total: %d actors", total);
                ImGui::EndTabItem();
            }

            /* ---------------------------------------------------------- */
            /* DEMO (cinematic scrubber)                                  */
            /* ---------------------------------------------------------- */
            if (ImGui::BeginTabItem("Demo")) {
                extern int port_demo_paused;
                extern int port_demo_seek_target;   /* set by slider */
                extern int port_demo_seek_active;   /* 1 = use the seek target */
                extern int port_demo_max_frame;     /* heuristic upper bound */
                extern int port_demo_force_visible; /* override visible=0 */

                ImGui::SeparatorText("Cinematic scrubber");

                bool active = (port_demo_seek_active != 0);
                if (ImGui::Checkbox("Enable scrubber", &active)) {
                    port_demo_seek_active = active ? 1 : 0;
                    if (active) port_demo_paused = 1;
                }
                ImGui::SetItemTooltip("Scrub the streaming cinematic by demo-frame. Reads .dmo data\ndirectly from DEMO.DAT, seeks to the chosen frame, feeds it to\nFrameRunDemo each tick. Bypasses the streaming SA actor's heap\nparser (can't reach late d00a frames yet).");
                ImGui::SameLine();
                if (ImGui::SmallButton("Release")) {
                    port_demo_seek_active = 0;
                    port_demo_paused = 0;
                }
                ImGui::SetItemTooltip("Stop seeking; let the demo run normally.");
                bool fv = (port_demo_force_visible != 0);
                if (ImGui::Checkbox("Force visible (override visible=0)", &fv)) {
                    port_demo_force_visible = fv ? 1 : 0;
                }
                ImGui::SetItemTooltip("Ignore the demo's visible-flag and render every adjust anyway —\nuseful when something is meant to fade in but you want to see it.");

                extern int port_demo_dump_request;
                ImGui::Spacing();
                if (ImGui::Button("Dump snake render state")) {
                    port_demo_dump_request = 1;
                }
                ImGui::SetItemTooltip(
                    "Dump the snake render chain to stderr: chanl[1] eye_inv,\n"
                    "demo's eye/center/clip, every adjust's pos+rot+visible, each\n"
                    "snake DG_OBJS world matrix, and the first face's eye-space\n"
                    "vertex coords. Diff against editor output to localize where\n"
                    "the divergence lives in the pipeline.");

                int max_frame = port_demo_max_frame > 0 ? port_demo_max_frame : 1980;
                int frame = port_demo_seek_target;
                if (frame < 0) frame = 0;
                if (frame > max_frame) frame = max_frame;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##demo_frame", &frame, 0, max_frame, "frame %d")) {
                    port_demo_seek_target = frame;
                }
                if (ImGui::Button("-10")) { port_demo_seek_target = frame > 10 ? frame - 10 : 0; }
                ImGui::SameLine();
                if (ImGui::Button("-1"))  { port_demo_seek_target = frame > 0 ? frame - 1 : 0; }
                ImGui::SameLine();
                if (ImGui::Button("+1"))  { port_demo_seek_target = frame < max_frame ? frame + 1 : max_frame; }
                ImGui::SameLine();
                if (ImGui::Button("+10")) { port_demo_seek_target = frame + 10 > max_frame ? max_frame : frame + 10; }
                ImGui::SameLine();
                ImGui::Text("paused=%d  active=%d  target=%d/%d",
                            port_demo_paused, port_demo_seek_active,
                            port_demo_seek_target, max_frame);

                ImGui::Separator();
                ImGui::TextDisabled("(Snake's adjust=type-6 trace fires "
                                    "in stderr each tick; check the run "
                                    "log for the current frame's pos.)");

                ImGui::EndTabItem();
            }

            /* ---------------------------------------------------------- */
            /* STAGE / LIGHTING                                           */
            /* ---------------------------------------------------------- */
            if (ImGui::BeginTabItem("Stage")) {
                ImGui::Text("Stage: %s",
                            port_current_stage[0] ? port_current_stage : "(none)");

                /* Custom-stage launcher. Lists every stage discovered under
                   extra_stages/ (port/editor/extra_stages, etc.) and lets
                   the user load it as if a GCL `load "<name>"` had run.
                   Only meaningful once the engine reaches the select menu
                   — by then init/title have set up the Snake actor's
                   dependencies, so jumping directly to s99a actually
                   works (the --stage CLI flag tried to skip too much
                   init and crashed sna_act). */
                if (ImGui::CollapsingHeader("Custom stage launcher",
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
                    extern int   port_fs_stage_count(void);
                    extern const char *port_fs_stage_name(int idx);
                    extern int   port_fs_stage_is_extra(int idx);
                    extern int   GV_StrCode(const char *);
                    extern void  GM_SetArea(int hash, const char *name);
                    extern int   GM_LoadRequest;
                    extern short linkvarbuf[];
                    /* linkvarbuf indices (linkvar.h):
                         6=GM_CurrentStageFlag  7=GM_CurrentMapFlag
                         8=GM_SnakePosX         9=GM_SnakePosY  10=GM_SnakePosZ */
                    static int sx = 0, sy = 0, sz = 8000;
                    int spawn[3] = { sx, sy, sz };
                    if (ImGui::InputInt3("Spawn (X Y Z)", spawn)) {
                        sx = spawn[0]; sy = spawn[1]; sz = spawn[2];
                    }

                    int n = port_fs_stage_count();
                    int found_any = 0;
                    for (int i = 0; i < n; i++) {
                        if (!port_fs_stage_is_extra(i)) continue;
                        found_any = 1;
                        const char *raw = port_fs_stage_name(i);
                        char name[16] = {0};
                        for (int c = 0; c < 8 && raw[c]; c++) name[c] = raw[c];
                        char label[32];
                        snprintf(label, sizeof(label), "Load %s", name);

                        if (ImGui::Button(label)) {
                            int hash = GV_StrCode(name);
                            linkvarbuf[6] = (short)hash;          /* GM_CurrentStageFlag */
                            linkvarbuf[7] = (short)GV_StrCode("main"); /* GM_CurrentMapFlag = HASH_MAIN */
                            linkvarbuf[8] = (short)sx;
                            linkvarbuf[9] = (short)sy;
                            linkvarbuf[10] = (short)sz;
                            GM_SetArea(hash, name);
                            /* 0x91 = high-priority load (0x80) | save var (0x10) | load (1),
                               matching what `load "name" -s b:1` compiles to. */
                            GM_LoadRequest = 0x91;
                            printf("[port] imgui: loading custom stage '%s' "
                                   "spawn=(%d,%d,%d)\n", name, sx, sy, sz);
                        }
                    }
                    if (!found_any) {
                        ImGui::TextDisabled("(no extra_stages found)");
                    }
                }
                ImGui::Separator();

                if (ImGui::Button("Dump lighting state to stderr")) {
                    port_light_dump_request = 1;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(run once; copy stderr lines after press)");
                ImGui::Separator();

                /* Single-button "back to defaults" for the entire lighting
                 * override surface — useful when you've toggled a handful
                 * of mute / disable / ambient knobs and want the stage's
                 * own lighting back without flipping each individually. */
                if (ImGui::Button("Reset lighting to stage defaults")) {
                    port_light_ambient_override = 0;
                    port_light_ambient_rgb[0]   = 32;
                    port_light_ambient_rgb[1]   = 32;
                    port_light_ambient_rgb[2]   = 32;
                    port_light_disable_fixed    = 0;
                    port_light_disable_dynamic  = 0;
                    port_light_ambient_scale    = 1.0f;
                    port_force_gouraud_neutral  = 0;
                    for (int i = 0; i < 8; i++) port_light_group_disabled[i] = 0;
                    for (int i = 0; i < 2; i++)
                        for (int j = 0; j < 8; j++)
                            port_light_dyn_slot_muted[i][j] = 0;
                }
                ImGui::SetItemTooltip("Clear every lighting override / mute / scale knob.");
                ImGui::Separator();

                /* --- Ambient ----------------------------------------- */
                {
                    float amb_r = DG_Ambient.vx / 255.0f;
                    float amb_g = DG_Ambient.vy / 255.0f;
                    float amb_b = DG_Ambient.vz / 255.0f;
                    if (amb_r < 0) amb_r = 0; if (amb_r > 1) amb_r = 1;
                    if (amb_g < 0) amb_g = 0; if (amb_g > 1) amb_g = 1;
                    if (amb_b < 0) amb_b = 0; if (amb_b > 1) amb_b = 1;
                    float amb_col[3] = { amb_r, amb_g, amb_b };
                    ImGui::ColorEdit3("##amb", amb_col,
                                      ImGuiColorEditFlags_NoInputs |
                                      ImGuiColorEditFlags_NoPicker |
                                      ImGuiColorEditFlags_NoLabel);
                    ImGui::SameLine();
                    ImGui::Text("Ambient  (%d, %d, %d)%s",
                                DG_Ambient.vx, DG_Ambient.vy, DG_Ambient.vz,
                                port_light_ambient_override ? " [OVERRIDE]" :
                                (port_light_ambient_scale != 1.0f ? " [SCALED]" : ""));

                    bool ov = port_light_ambient_override != 0;
                    if (ImGui::Checkbox("Override ambient", &ov))
                        port_light_ambient_override = ov ? 1 : 0;
                    if (port_light_ambient_override) {
                        float over[3] = {
                            port_light_ambient_rgb[0] / 255.0f,
                            port_light_ambient_rgb[1] / 255.0f,
                            port_light_ambient_rgb[2] / 255.0f,
                        };
                        if (ImGui::ColorEdit3("Override RGB", over,
                                              ImGuiColorEditFlags_Float)) {
                            port_light_ambient_rgb[0] = (int)(over[0] * 255.0f);
                            port_light_ambient_rgb[1] = (int)(over[1] * 255.0f);
                            port_light_ambient_rgb[2] = (int)(over[2] * 255.0f);
                        }
                    } else {
                        ImGui::SliderFloat("Ambient scale",
                                           &port_light_ambient_scale,
                                           0.0f, 4.0f, "%.2fx");
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Reset##ambscale"))
                            port_light_ambient_scale = 1.0f;
                    }
                }

                ImGui::Separator();

                /* Directional lights: DG_LightMatrix rows = directions (4.12),
                   DG_ColorMatrix columns = colors scaled *16 (DG_SetMainLightCol
                   multiplies by 16; divide by 16 to recover the user 0..255). */
                if (ImGui::CollapsingHeader("Directional lights (3)")) {
                    static const char *names[3] = { "Main", "Sub 1", "Sub 2" };
                    if (ImGui::BeginTable("##dirlights", 4,
                                          ImGuiTableFlags_Borders |
                                          ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 55);
                        ImGui::TableSetupColumn("Direction (x, y, z)", ImGuiTableColumnFlags_WidthFixed, 175);
                        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 140);
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30);
                        ImGui::TableHeadersRow();

                        for (int i = 0; i < 3; i++) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", names[i]);

                            float dx = DG_LightMatrix.m[i][0] / 4096.0f;
                            float dy = DG_LightMatrix.m[i][1] / 4096.0f;
                            float dz = DG_LightMatrix.m[i][2] / 4096.0f;
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%+.2f, %+.2f, %+.2f", dx, dy, dz);

                            /* Color matrix stores columns as R,G,B for each light. */
                            float cr = DG_ColorMatrix.m[0][i] / 16.0f / 255.0f;
                            float cg = DG_ColorMatrix.m[1][i] / 16.0f / 255.0f;
                            float cb = DG_ColorMatrix.m[2][i] / 16.0f / 255.0f;
                            if (cr < 0) cr = 0; if (cr > 1) cr = 1;
                            if (cg < 0) cg = 0; if (cg > 1) cg = 1;
                            if (cb < 0) cb = 0; if (cb > 1) cb = 1;
                            float col[3] = { cr, cg, cb };
                            ImGui::TableSetColumnIndex(2);
                            char id[16]; snprintf(id, sizeof(id), "##c%d", i);
                            ImGui::ColorEdit3(id, col,
                                              ImGuiColorEditFlags_NoInputs |
                                              ImGuiColorEditFlags_NoPicker |
                                              ImGuiColorEditFlags_NoLabel);
                            ImGui::SameLine();
                            ImGui::Text("%d,%d,%d",
                                        (int)(cr*255), (int)(cg*255), (int)(cb*255));
                            ImGui::TableSetColumnIndex(3);
                            float len = sqrtf(dx*dx + dy*dy + dz*dz);
                            ImGui::TextDisabled("%.2f", len);
                        }
                        ImGui::EndTable();
                    }
                }

                /* --- Fixed point lights. Each group has a count and an array,
                   total across all groups in the header.                     */
                int fx_total = 0;
                for (int g = 0; g < 8; g++) fx_total += port_dg_fixed_light_count(g);
                char fx_header[64];
                snprintf(fx_header, sizeof(fx_header),
                         "Fixed point lights (%d%s)",
                         fx_total,
                         port_light_disable_fixed ? " -- OFF" : "");
                if (ImGui::CollapsingHeader(fx_header)) {
                    bool dis = port_light_disable_fixed != 0;
                    if (ImGui::Checkbox("Disable all fixed lights", &dis))
                        port_light_disable_fixed = dis ? 1 : 0;

                    int row = 0;
                    for (int g = 0; g < 8; g++) {
                        int n = port_dg_fixed_light_count(g);
                        PortDG_LIT *p = (PortDG_LIT *)port_dg_fixed_light_data(g);
                        if (n <= 0 || !p) {
                            /* Still show a disabled checkbox if the engine has
                               ever populated this group (cached via our mute),
                               so the user can keep a group off across reloads.
                               Skip empty-and-never-touched groups to reduce noise. */
                            continue;
                        }
                        char gid[32]; snprintf(gid, sizeof(gid), "Group %d##gmute%d", g, g);
                        bool gdis = port_light_group_disabled[g] != 0;
                        if (ImGui::Checkbox(gid, &gdis))
                            port_light_group_disabled[g] = gdis ? 1 : 0;
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%d lights)", n);

                        ImGui::Indent();
                        for (int i = 0; i < n; i++, row++) {
                            PortDG_LIT *lt = &p[i];
                            float rr = lt->color.r/255.0f;
                            float gg = lt->color.g/255.0f;
                            float bb = lt->color.b/255.0f;
                            float col[3] = { rr, gg, bb };
                            char id[24]; snprintf(id, sizeof(id), "##fx%d", row);
                            ImGui::ColorEdit3(id, col,
                                              ImGuiColorEditFlags_NoInputs |
                                              ImGuiColorEditFlags_NoPicker |
                                              ImGuiColorEditFlags_NoLabel);
                            ImGui::SameLine();
                            ImGui::Text("[%d] pos=(%d,%d,%d)  bri=%u  r=%u",
                                        i, lt->pos.vx, lt->pos.vy, lt->pos.vz,
                                        lt->brightness, lt->radius);
                        }
                        ImGui::Unindent();
                    }
                    if (row == 0) ImGui::TextDisabled("(no active groups)");
                }

                /* --- Dynamic (per-tick) light list, double-buffered. Show
                   both buffers so you can see which is active on either
                   frame. Per-slot "mute" zeroes the slot's RGB color each
                   frame (engine keeps re-adding the light, we keep zeroing). */
                char dy_header[80];
                snprintf(dy_header, sizeof(dy_header),
                         "Dynamic lights (buf0=%d, buf1=%d%s)",
                         port_dg_tmp_light_count(0),
                         port_dg_tmp_light_count(1),
                         port_light_disable_dynamic ? " -- OFF" : "");
                if (ImGui::CollapsingHeader(dy_header)) {
                    bool dis = port_light_disable_dynamic != 0;
                    if (ImGui::Checkbox("Disable all dynamic lights", &dis))
                        port_light_disable_dynamic = dis ? 1 : 0;

                    for (int b = 0; b < 2; b++) {
                        int n = port_dg_tmp_light_count(b);
                        ImGui::Text("Buffer %d: %d lights", b, n);
                        if (n > 8) n = 8;
                        ImGui::Indent();
                        for (int i = 0; i < n; i++) {
                            PortDG_LIT *lt = &((PortDG_LIT *)port_dg_tmp_light_data(b))[i];
                            char mid[24]; snprintf(mid, sizeof(mid), "##dymute%d_%d", b, i);
                            bool m = port_light_dyn_slot_muted[b][i] != 0;
                            if (ImGui::Checkbox(mid, &m))
                                port_light_dyn_slot_muted[b][i] = m ? 1 : 0;
                            ImGui::SameLine();

                            float rr = lt->color.r/255.0f;
                            float gg = lt->color.g/255.0f;
                            float bb = lt->color.b/255.0f;
                            float col[3] = { rr, gg, bb };
                            char id[24]; snprintf(id, sizeof(id), "##dy%d_%d", b, i);
                            ImGui::ColorEdit3(id, col,
                                              ImGuiColorEditFlags_NoInputs |
                                              ImGuiColorEditFlags_NoPicker |
                                              ImGuiColorEditFlags_NoLabel);
                            ImGui::SameLine();
                            ImGui::Text("[%d] pos=(%d,%d,%d)  bri=%u  r=%u",
                                        i, lt->pos.vx, lt->pos.vy, lt->pos.vz,
                                        lt->brightness, lt->radius);
                        }
                        ImGui::Unindent();
                    }
                }

                ImGui::EndTabItem();
            }

            /* ---------------------------------------------------------- */
            /* GAME STATE                                                 */
            /* ---------------------------------------------------------- */
            if (ImGui::BeginTabItem("Game")) {
                /* Freeze GV_Time / GV_Clock — re-write the cached values
                 * each frame, so anything reading these globals sees a
                 * constant tick count. Pairs with the actor-pause control
                 * at the top: pausing actors stops their state advance,
                 * freezing GV_Time stops engine-side animation lerps that
                 * sample time directly. */
                static bool freeze_time = false;
                static int  frozen_gv_time = 0;
                static int  frozen_gv_clock = 0;
                bool was_frozen = freeze_time;
                ImGui::Checkbox("Freeze GV_Time / GV_Clock", &freeze_time);
                ImGui::SetItemTooltip(
                    "Hold the engine's tick counters at their current value.\n"
                    "Anything reading GV_Time (animation curves, fades, alert\n"
                    "timers) sees a constant. Pairs with the Pause control to\n"
                    "freeze the scene as completely as possible.");
                if (freeze_time && !was_frozen) {
                    frozen_gv_time  = GV_Time;
                    frozen_gv_clock = GV_Clock;
                }
                if (freeze_time) {
                    GV_Time  = frozen_gv_time;
                    GV_Clock = frozen_gv_clock;
                }
                ImGui::Separator();

                ImGui::Text("GV_Clock:      %d", GV_Clock);
                ImGui::Text("GV_Time:       %d", GV_Time);
                ImGui::Text("GM_GameStatus: 0x%08X", GM_GameStatus);
                /* Decoded flag bits -- names from source/game/gamed.h STATE_*.
                   Only the ones commonly seen during play are listed. */
                static const struct { unsigned long bit; const char *name; } gs[] = {
                    {0x00000001, "PADON"},       {0x00000002, "DEMO"},
                    {0x00000004, "PAUSE"},       {0x00000008, "MENU"},
                    {0x00000010, "RADIO"},       {0x00000020, "MAP"},
                    {0x00000040, "ITEM"},        {0x00000080, "WEAPON"},
                    {0x00000100, "OVERMAP"},     {0x00000200, "MISSION"},
                    {0x00001000, "FADE"},        {0x00010000, "PADRELEASE"},
                    {0x80000000, "INIT/FRESH"},
                };
                ImGui::Indent();
                char buf[256] = {0}; char *w = buf, *end = buf + sizeof(buf);
                int any = 0;
                for (size_t i = 0; i < sizeof(gs)/sizeof(gs[0]); i++) {
                    if ((unsigned long)GM_GameStatus & gs[i].bit) {
                        w += snprintf(w, end - w, "%s%s", any ? " " : "", gs[i].name);
                        any = 1;
                    }
                }
                ImGui::TextDisabled("%s", any ? buf : "(no flags)");
                ImGui::Unindent();

                ImGui::Separator();
                ImGui::Text("DG_FrameRate:  %d  (1=normal, 2=codec)", DG_FrameRate);
                ImGui::Text("Alert mode:  %d", GM_AlertMode);
                ImGui::Text("Alert level: %d", GM_AlertLevel);
                ImGui::Text("GroupID:     0x%X", DG_CurrentGroupID);
                ImGui::Separator();
                ImGui::Text("Snake pos: (%d, %d, %d)",
                    GM_PlayerPosition.vx, GM_PlayerPosition.vy, GM_PlayerPosition.vz);
                ImGui::Text("prev-eye svec_800ABA88: (%d, %d, %d)",
                    svec_800ABA88.vx, svec_800ABA88.vy, svec_800ABA88.vz);
                ImGui::EndTabItem();
            }

            /* ---------------------------------------------------------- */
            /* OTHER — stage / input / audio / controls                   */
            /* ---------------------------------------------------------- */
            if (ImGui::BeginTabItem("Input")) {
                {
                    unsigned long b = (unsigned long)mts_PadRead(0);
                    ImGui::Text("Buttons: 0x%04lX", b);
                    /* PSX pad button bits (active-high after port_update_pad) */
                    static const struct { unsigned long bit; const char *n; } pb[] = {
                        {0x0010, "Up"}, {0x0020, "Right"}, {0x0040, "Down"}, {0x0080, "Left"},
                        {0x1000, "Tri"}, {0x2000, "Circ"}, {0x4000, "Cross"}, {0x8000, "Sq"},
                        {0x0100, "Sel"}, {0x0800, "Start"},
                        {0x0004, "L1"}, {0x0001, "L2"}, {0x0008, "R1"}, {0x0002, "R2"},
                    };
                    for (size_t i = 0; i < sizeof(pb)/sizeof(pb[0]); i++) {
                        bool on = (b & pb[i].bit) != 0;
                        if (on)
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[%s]", pb[i].n);
                        else
                            ImGui::TextDisabled("[%s]", pb[i].n);
                        if (((i + 1) % 5) != 0) ImGui::SameLine();
                    }
                    ImGui::NewLine();
                    /* Analog stick (0..255, 128 = center) drawn as a small 2D pad. */
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    const float sz = 60.0f;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddRect(p, ImVec2(p.x + sz, p.y + sz),
                                IM_COL32(180,180,180,200));
                    float cx = p.x + (port_pad_lx / 255.0f) * sz;
                    float cy = p.y + (port_pad_ly / 255.0f) * sz;
                    dl->AddCircleFilled(ImVec2(cx, cy), 4.0f,
                                        IM_COL32(120,220,120,255));
                    ImGui::Dummy(ImVec2(sz, sz));
                    ImGui::SameLine();
                    ImGui::Text("L stick\n  lx = %u\n  ly = %u",
                                (unsigned)port_pad_lx, (unsigned)port_pad_ly);

                    /* Right stick — wired via mts.c for the freecam. */
                    extern unsigned char port_pad_rx, port_pad_ry;
                    ImGui::SameLine();
                    ImVec2 pr = ImGui::GetCursorScreenPos();
                    dl->AddRect(pr, ImVec2(pr.x + sz, pr.y + sz),
                                IM_COL32(180,180,180,200));
                    float rcx = pr.x + (port_pad_rx / 255.0f) * sz;
                    float rcy = pr.y + (port_pad_ry / 255.0f) * sz;
                    dl->AddCircleFilled(ImVec2(rcx, rcy), 4.0f,
                                        IM_COL32(220,180,120,255));
                    ImGui::Dummy(ImVec2(sz, sz));
                    ImGui::SameLine();
                    ImGui::Text("R stick\n  rx = %u\n  ry = %u",
                                (unsigned)port_pad_rx, (unsigned)port_pad_ry);
                }
                ImGui::EndTabItem();
            }

            /* ---------------------------------------------------------- */
            /* AUDIO                                                      */
            /* ---------------------------------------------------------- */
            if (ImGui::BeginTabItem("Audio")) {
                {
                    /* --- Global mute ------------------------------------ */
                    bool muted = port_spu_is_muted() != 0;
                    if (ImGui::Checkbox("Mute SPU output", &muted))
                        port_spu_set_muted(muted);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(silence all channels; non-destructive)");

                    /* --- engine state ----------------------------------- */
                    ImGui::Text("str_status: %u  (%s)", str_status,
                                str_status == 0 ? "idle" :
                                str_status < 5  ? "setup" :
                                                   "playing (>=5)");
                    ImGui::Text("sng_status: %d   bgm_idx: %d", sng_status, bgm_idx);
                    ImGui::Text("str_volume: %u   vox_on: %d   mute: %d   se_tracks: %d",
                                str_volume, str_vox_on, str_mute_status, se_tracks);

                    short ml = 0, mr = 0;
                    port_spu_get_master(&ml, &mr);
                    ImGui::Text("SPU master: L=%d  R=%d (signed 14-bit, max 0x3FFF)", ml, mr);

                    /* --- BGM queue (16 slots) --------------------------- */
                    if (ImGui::TreeNodeEx("BGM queue (sd_sng_code_buf)",
                                          ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        if (ImGui::BeginTable("##bgm", 4, ImGuiTableFlags_Borders |
                                                         ImGuiTableFlags_SizingFixedFit))
                        {
                            for (int i = 0; i < 16; i++) {
                                ImGui::TableNextColumn();
                                int c = sd_sng_code_buf[i];
                                if (c == 0)
                                    ImGui::TextDisabled("[%02d] .", i);
                                else
                                    ImGui::Text("[%02d] %08X", i, c);
                            }
                            ImGui::EndTable();
                        }
                        ImGui::TreePop();
                    }

                    /* --- Stream PCM buffer ------------------------------ */
                    int rd = 0, wr_r = 0, wr_l = 0, stream_on = 0;
                    unsigned long br = 0, bl = 0;
                    int pcm_max = port_spu_stream_info(&rd, &wr_r, &wr_l, &stream_on,
                                                        &br, &bl);
                    if (ImGui::TreeNodeEx("VOX stream PCM",
                                          ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Text("active: %s   base_r: 0x%lX   base_l: 0x%lX",
                                    stream_on ? "yes" : "no", br, bl);
                        int queued_r = wr_r - rd; if (queued_r < 0) queued_r = 0;
                        int queued_l = wr_l - rd; if (queued_l < 0) queued_l = 0;
                        float fill_r = pcm_max ? (float)queued_r / pcm_max : 0.0f;
                        float fill_l = pcm_max ? (float)queued_l / pcm_max : 0.0f;
                        ImGui::ProgressBar(fill_r, ImVec2(180, 0),
                            ([&]{ static char b[64]; snprintf(b, sizeof b,
                                "R %d smp (%.1f s)", queued_r, queued_r/44100.0f); return b; })());
                        ImGui::SameLine();
                        ImGui::ProgressBar(fill_l, ImVec2(180, 0),
                            ([&]{ static char b[64]; snprintf(b, sizeof b,
                                "L %d smp (%.1f s)", queued_l, queued_l/44100.0f); return b; })());
                        ImGui::TreePop();
                    }

                    /* --- SPU voice table (24 channels) ------------------ */
                    if (ImGui::TreeNodeEx("SPU voices (24)",
                                          ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        static const char *env_names[5] = {
                            "OFF", "ATTACK", "DECAY", "SUSTAIN", "RELEASE"
                        };
                        if (ImGui::SmallButton("Mute all"))
                            port_spu_voice_mute_all(1);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Unmute all"))
                            port_spu_voice_mute_all(0);
                        PortSpuVoiceInfo vi[24];
                        int n = port_spu_get_voices(vi, 24);
                        if (ImGui::BeginTable("##voices", 8,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                        {
                            /* Explicit widths keep the "state" column from
                               jumping around when values toggle between
                               "off" / "ON" / "RELEASE" (different string
                               lengths). */
                            ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed,  24);
                            ImGui::TableSetupColumn("state",    ImGuiTableColumnFlags_WidthFixed,  56);
                            ImGui::TableSetupColumn("envelope", ImGuiTableColumnFlags_WidthFixed,  66);
                            ImGui::TableSetupColumn("lvl",      ImGuiTableColumnFlags_WidthFixed,  64);
                            ImGui::TableSetupColumn("pitch",    ImGuiTableColumnFlags_WidthFixed,  54);
                            ImGui::TableSetupColumn("vol L/R",  ImGuiTableColumnFlags_WidthFixed,  76);
                            ImGui::TableSetupColumn("addr",     ImGuiTableColumnFlags_WidthFixed,  88);
                            ImGui::TableSetupColumn("mute",     ImGuiTableColumnFlags_WidthFixed,  54);
                            ImGui::TableHeadersRow();
                            for (int i = 0; i < n; i++) {
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                ImGui::Text("%2d", i);
                                ImGui::TableNextColumn();
                                const char *state = vi[i].active
                                    ? (vi[i].key_off ? "RELEASE" : "ON")
                                    : "off";
                                if (vi[i].active)
                                    ImGui::TextColored(ImVec4(0.4f,1.0f,0.4f,1.0f),
                                                       "%s", state);
                                else
                                    ImGui::TextDisabled("%s", state);
                                ImGui::TableNextColumn();
                                int ep = vi[i].env_phase;
                                if (ep < 0 || ep > 4) ep = 0;
                                ImGui::Text("%s", env_names[ep]);
                                ImGui::TableNextColumn();
                                /* envelope level bar, 0..0x7FFF */
                                ImGui::ProgressBar((float)vi[i].env_level / 32767.0f,
                                                   ImVec2(60, 0), "");
                                ImGui::TableNextColumn();
                                ImGui::Text("%u", vi[i].pitch);
                                ImGui::TableNextColumn();
                                ImGui::Text("%d/%d", vi[i].vol_l, vi[i].vol_r);
                                ImGui::TableNextColumn();
                                ImGui::Text("0x%lX", vi[i].addr);
                                /* Per-voice mute toggle. Non-destructive:
                                   the voice keeps ticking, we just drop its
                                   samples in the mixer. Color-coded: red
                                   tint when muted so the row stands out. */
                                ImGui::TableNextColumn();
                                ImGui::PushID(i);
                                bool m = port_spu_voice_is_muted(i) != 0;
                                if (m) {
                                    ImGui::PushStyleColor(ImGuiCol_Button,
                                        ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                        ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
                                    if (ImGui::SmallButton("Muted"))
                                        port_spu_voice_set_mute(i, 0);
                                    ImGui::PopStyleColor(2);
                                } else {
                                    if (ImGui::SmallButton("Mute"))
                                        port_spu_voice_set_mute(i, 1);
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        ImGui::TreePop();
                    }
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    (void)show_imgui_demo;   /* reserved for future: link imgui_demo.cpp and show. */

    ImGui::Render();
    if (g_imgui_use_gl)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    else
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}
