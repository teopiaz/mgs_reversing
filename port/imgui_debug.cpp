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
} ActorList;

extern ActorList gActorsList_800ACC18[7];
extern void GV_DestroyActorQuick(void *actor);
extern int GV_Clock;
extern int GV_Time;
extern int GM_GameStatus;
extern int GM_AlertMode;
extern int GM_AlertLevel;

/* Camera / rendering state */
typedef struct { short m[3][3]; short pad; int t[3]; } PortMATRIX;
typedef struct { short vx, vy, vz, pad; } PortSVECTOR;
typedef struct {
    unsigned long *ot[2];
    short ot_size, link, dblbuf, dirty;
    PortMATRIX eye_inv;
    PortMATRIX eye;
    short clip_distance;
    short queue_size;
    short prim_index;
    short objs_index;
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
    extern PortDG_FixedLight  gFixedLights_800B1E08[8];
    extern PortDG_TmpLightList LightSystems_800B1E48[2];

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
    ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("MGS Debug (F1)", &show_debug)) {
        /* Sticky header: always-on summary. */
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS %.1f  |  frame %.2f ms  |  GL %s  |  tick %d",
                    io.Framerate, 1000.0f / (io.Framerate > 1.0f ? io.Framerate : 1.0f),
                    g_imgui_use_gl ? "on" : "off", GV_Time);
        ImGui::Separator();

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
                    if (ImGui::Checkbox("Wireframe (GL_LINE polygons)", &b))
                        gl_debug_wireframe = b;
                    b = gl_debug_no_textures != 0;
                    if (ImGui::Checkbox("Disable textures (flat color only)", &b))
                        gl_debug_no_textures = b;
                    b = gl_debug_no_cull != 0;
                    if (ImGui::Checkbox("Disable backface cull (see interior)", &b))
                        gl_debug_no_cull = b;
                    b = gl_debug_cull_cw != 0;
                    if (ImGui::Checkbox("Flip front-face (CW instead of CCW)", &b))
                        gl_debug_cull_cw = b;
                    b = port_force_gouraud_neutral != 0;
                    if (ImGui::Checkbox("Force Gouraud neutral (128,128,128)", &b))
                        port_force_gouraud_neutral = b ? 1 : 0;
                    b = gl_debug_face_id != 0;
                    if (ImGui::Checkbox("Per-tri random color (face ID)", &b))
                        gl_debug_face_id = b;
                    b = gl_debug_show_normals != 0;
                    if (ImGui::Checkbox("Show normals (N.xyz*0.5+0.5 as RGB)", &b))
                        gl_debug_show_normals = b;
                    bool vram_dbg = port_vram_debug_view() != 0;
                    if (ImGui::Checkbox("VRAM debug view (show full 1024x512)", &vram_dbg))
                        port_vram_toggle_debug();

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
                ImGui::Checkbox("Override Camera", (bool *)&imgui_cam_override);
                if (imgui_cam_override) {
                    static bool inited = false;
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
                }
                ImGui::EndTabItem();
            }

            /* ---------------------------------------------------------- */
            /* ACTORS                                                     */
            /* ---------------------------------------------------------- */
            if (ImGui::BeginTabItem("Actors")) {
                const char *level_names[] = {
                    "DAEMON", "MANAGER", "LEVEL2", "LEVEL3",
                    "LEVEL4", "LEVEL5", "DAEMON2"
                };
                int total = 0;
                for (int lv = 0; lv < 7; lv++) {
                    ActorList *list = &gActorsList_800ACC18[lv];
                    ActorNode *head = &list->first;
                    ActorNode *cur = head->next;
                    int count = 0;
                    while (cur && cur != &list->last && count < 256) { count++; cur = cur->next; }
                    if (count == 0) continue;
                    total += count;
                    if (ImGui::TreeNode(level_names[lv], "%s (%d) pause=%d kill=%d",
                                        level_names[lv], count, list->pause, list->kill))
                    {
                        cur = head->next;
                        int idx = 0;
                        while (cur && cur != &list->last && idx < 256) {
                            const char *name = cur->filename ? cur->filename : "???";
                            bool active = (cur->act != NULL);
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
                ImGui::Text("Total: %d actors", total);
                ImGui::EndTabItem();
            }

            /* ---------------------------------------------------------- */
            /* STAGE / LIGHTING                                           */
            /* ---------------------------------------------------------- */
            if (ImGui::BeginTabItem("Stage")) {
                ImGui::Text("Stage: %s",
                            port_current_stage[0] ? port_current_stage : "(none)");
                if (ImGui::Button("Dump lighting state to stderr")) {
                    port_light_dump_request = 1;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(run once; copy stderr lines after press)");
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
                if (ImGui::CollapsingHeader("Directional lights (3)",
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
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
                for (int g = 0; g < 8; g++) fx_total += gFixedLights_800B1E08[g].count;
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
                        int n = gFixedLights_800B1E08[g].count;
                        PortDG_LIT *p = gFixedLights_800B1E08[g].p;
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
                         LightSystems_800B1E48[0].n_lights,
                         LightSystems_800B1E48[1].n_lights,
                         port_light_disable_dynamic ? " -- OFF" : "");
                if (ImGui::CollapsingHeader(dy_header)) {
                    bool dis = port_light_disable_dynamic != 0;
                    if (ImGui::Checkbox("Disable all dynamic lights", &dis))
                        port_light_disable_dynamic = dis ? 1 : 0;

                    for (int b = 0; b < 2; b++) {
                        int n = LightSystems_800B1E48[b].n_lights;
                        ImGui::Text("Buffer %d: %d lights", b, n);
                        if (n > 8) n = 8;
                        ImGui::Indent();
                        for (int i = 0; i < n; i++) {
                            PortDG_LIT *lt = &LightSystems_800B1E48[b].lights[i];
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
            if (ImGui::BeginTabItem("Other")) {
                if (ImGui::CollapsingHeader("Stage",
                                            ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Current stage: %s",
                                port_current_stage[0] ? port_current_stage : "(none)");
                    ImGui::Text("Snake pos: (%d, %d, %d)",
                        GM_PlayerPosition.vx, GM_PlayerPosition.vy, GM_PlayerPosition.vz);
                }

                if (ImGui::CollapsingHeader("Input / Pad",
                                            ImGuiTreeNodeFlags_DefaultOpen))
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
                }

                if (ImGui::CollapsingHeader("Audio"))
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

                if (ImGui::CollapsingHeader("Debug controls",
                                            ImGuiTreeNodeFlags_DefaultOpen))
                {
                    extern bool g_running;  /* main.c */
                    if (ImGui::Button("Quit"))
                        g_running = false;
                    ImGui::SameLine();
                    if (ImGui::Button("Restart"))
                    {
                        /* Best-effort: re-exec the current binary. argv[0] is
                           captured at startup in main.c (see port_argv0). */
                        extern const char *port_argv0;
                        if (port_argv0)
                            execl(port_argv0, port_argv0, (char *)NULL);
                    }
                    ImGui::TextDisabled("Restart: re-execs the current binary.");
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
