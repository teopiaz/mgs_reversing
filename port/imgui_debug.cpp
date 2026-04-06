#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_sdlrenderer2.h"
#include <SDL.h>

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

} /* extern "C" */

static bool show_actors = false;
static bool show_camera = false;

extern "C" void imgui_init(SDL_Window *window, SDL_Renderer *renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = 0.5f;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
}

extern "C" void imgui_shutdown(void)
{
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

extern "C" void imgui_process_event(SDL_Event *event)
{
    ImGui_ImplSDL2_ProcessEvent(event);
}

extern "C" void imgui_toggle_actors(void)
{
    show_actors = !show_actors;
}

extern "C" void imgui_toggle_camera(void)
{
    show_camera = !show_camera;
    /* Camera panel requires the actors panel to also be active
       (same imgui render path) */
    if (show_camera) show_actors = true;
    printf("[imgui] camera debug: %s\n", show_camera ? "ON" : "OFF");
}

extern "C" void imgui_render(SDL_Renderer *renderer)
{
    if (!show_actors && !show_camera)
        return;

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    /* Camera debug window */
    if (show_camera) {
        PortDG_CHANL_Partial *chanl = &DG_Chanls[1];

        ImGui::SetNextWindowPos(ImVec2(540, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Camera Debug", &show_camera)) {
            ImGui::Text("Snake: (%d, %d, %d)", GM_PlayerPosition.vx, GM_PlayerPosition.vy, GM_PlayerPosition.vz);
            ImGui::Text("Eye.t: (%d, %d, %d)", chanl->eye.t[0], chanl->eye.t[1], chanl->eye.t[2]);
            ImGui::Text("clip_dist: %d  objs: %d  faces: %d",
                chanl->clip_distance, chanl->objs_index, port_last_drawn_faces);
            ImGui::Text("GroupID: 0x%X", DG_CurrentGroupID);

            ImGui::Separator();
            ImGui::Text("eye_inv (view matrix used by renderer):");
            ImGui::Text("  m[0]: %6d %6d %6d", chanl->eye_inv.m[0][0], chanl->eye_inv.m[0][1], chanl->eye_inv.m[0][2]);
            ImGui::Text("  m[1]: %6d %6d %6d", chanl->eye_inv.m[1][0], chanl->eye_inv.m[1][1], chanl->eye_inv.m[1][2]);
            ImGui::Text("  m[2]: %6d %6d %6d", chanl->eye_inv.m[2][0], chanl->eye_inv.m[2][1], chanl->eye_inv.m[2][2]);
            ImGui::Text("  t:    %6d %6d %6d", chanl->eye_inv.t[0], chanl->eye_inv.t[1], chanl->eye_inv.t[2]);

            int dx = chanl->eye.t[0] - GM_PlayerPosition.vx;
            int dz = chanl->eye.t[2] - GM_PlayerPosition.vz;
            ImGui::Text("cam-snake: dx=%d dz=%d", dx, dz);

            ImGui::Separator();
            ImGui::Checkbox("Override Camera", (bool *)&imgui_cam_override);

            if (imgui_cam_override) {
                /* Initialize override values from current camera on first enable */
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

                ImGui::Text("Translation (eye_inv.t):");
                ImGui::SliderInt("t[0] (X)", &imgui_cam_eye_inv_t[0], -30000, 30000);
                ImGui::SliderInt("t[1] (Y)", &imgui_cam_eye_inv_t[1], -30000, 30000);
                ImGui::SliderInt("t[2] (Z)", &imgui_cam_eye_inv_t[2], -30000, 30000);

                ImGui::Text("Clip distance:");
                ImGui::SliderInt("dist", &imgui_cam_clip_dist, 50, 2000);

                ImGui::Text("Rotation (eye_inv.m):");
                for (int r = 0; r < 3; r++) {
                    char label[16];
                    for (int c = 0; c < 3; c++) {
                        snprintf(label, sizeof(label), "m[%d][%d]", r, c);
                        int val = imgui_cam_eye_inv_m[r][c];
                        ImGui::PushItemWidth(100);
                        if (ImGui::DragInt(label, &val, 10, -4096, 4096))
                            imgui_cam_eye_inv_m[r][c] = (short)val;
                        ImGui::PopItemWidth();
                        if (c < 2) ImGui::SameLine();
                    }
                }

                if (ImGui::Button("Reset to current")) {
                    inited = false;
                    imgui_cam_override = 0;
                }
            }
        }
        ImGui::End();
    }

    if (!show_actors) {
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        return;
    }

    /* Also show camera debug when actors panel is open and F2 was pressed */

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("MGS Debug", &show_actors)) {
        ImGui::Text("Clock: %d  Time: %d  Status: 0x%08X", GV_Clock, GV_Time, GM_GameStatus);
        ImGui::Text("Alert: mode=%d level=%d", GM_AlertMode, GM_AlertLevel);
        ImGui::Separator();

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
            while (cur && cur != &list->last && count < 256) {
                count++;
                cur = cur->next;
            }
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
                        idx, name,
                        active ? "Y" : "DEAD",
                        cur->count, cur->runtime);

                    if (!active)
                        ImGui::PopStyleColor();

                    cur = cur->next;
                    idx++;
                }
                ImGui::TreePop();
            }
        }

        ImGui::Separator();
        ImGui::Text("Total: %d actors", total);

        /* ---- Camera Debug ---- */
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            PortDG_CHANL_Partial *chanl = &DG_Chanls[1];
            PortGM_CAMERA *gc = &GM_Camera;
            PortCamStruct2 *s2 = &gUnkCameraStruct2_800B7868;
            PortCamStructB *sb = &gUnkCameraStruct_800B77B8;

            ImGui::Text("Snake:  (%d, %d, %d)",
                GM_PlayerPosition.vx, GM_PlayerPosition.vy, GM_PlayerPosition.vz);
            int dx = chanl->eye.t[0] - GM_PlayerPosition.vx;
            int dz = chanl->eye.t[2] - GM_PlayerPosition.vz;
            ImGui::Text("cam-snake: dx=%d dz=%d", dx, dz);
            ImGui::Text("clip_dist=%d  objs=%d  faces=%d  grp=0x%X",
                chanl->clip_distance, chanl->objs_index, port_last_drawn_faces, DG_CurrentGroupID);
            ImGui::Text("GameStatus=0x%08X  Alert: mode=%d level=%d",
                GM_GameStatus, GM_AlertMode, GM_AlertLevel);

            ImGui::Spacing();
            if (ImGui::TreeNode("GM_Camera (target)")) {
                ImGui::Text("eye:    (%d, %d, %d)", gc->eye.vx, gc->eye.vy, gc->eye.vz);
                ImGui::Text("center: (%d, %d, %d)", gc->center.vx, gc->center.vy, gc->center.vz);
                ImGui::Text("rotate: (%d, %d, %d)", gc->rotate.vx, gc->rotate.vy, gc->rotate.vz);
                ImGui::Text("zoom=%d fp=%d flags=0x%X track=%d interp=%d",
                    gc->zoom, gc->first_person, gc->flags, gc->track, gc->interp);
                ImGui::Text("field_28=%d field_2A=%d", gc->field_28, gc->field_2A);
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
                ImGui::Text("rotate2: (%d, %d, %d)", sb->rotate2.vx, sb->rotate2.vy, sb->rotate2.vz);
                ImGui::Text("track=%d interp=%d", sb->track, sb->interp);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("DG_Chanls[1] (final view matrix)")) {
                ImGui::Text("dblbuf=%d dirty=%d link=%d",
                    chanl->dblbuf, chanl->dirty, chanl->link);
                ImGui::Text("eye.t:     (%d, %d, %d)",
                    chanl->eye.t[0], chanl->eye.t[1], chanl->eye.t[2]);
                ImGui::Text("eye.m[0]:  %6d %6d %6d",
                    chanl->eye.m[0][0], chanl->eye.m[0][1], chanl->eye.m[0][2]);
                ImGui::Text("eye.m[1]:  %6d %6d %6d",
                    chanl->eye.m[1][0], chanl->eye.m[1][1], chanl->eye.m[1][2]);
                ImGui::Text("eye.m[2]:  %6d %6d %6d",
                    chanl->eye.m[2][0], chanl->eye.m[2][1], chanl->eye.m[2][2]);
                ImGui::Spacing();
                ImGui::Text("eye_inv.t: (%d, %d, %d)",
                    chanl->eye_inv.t[0], chanl->eye_inv.t[1], chanl->eye_inv.t[2]);
                ImGui::Text("eye_inv.m[0]: %6d %6d %6d",
                    chanl->eye_inv.m[0][0], chanl->eye_inv.m[0][1], chanl->eye_inv.m[0][2]);
                ImGui::Text("eye_inv.m[1]: %6d %6d %6d",
                    chanl->eye_inv.m[1][0], chanl->eye_inv.m[1][1], chanl->eye_inv.m[1][2]);
                ImGui::Text("eye_inv.m[2]: %6d %6d %6d",
                    chanl->eye_inv.m[2][0], chanl->eye_inv.m[2][1], chanl->eye_inv.m[2][2]);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("svec_800ABA88 (prev eye)")) {
                ImGui::Text("(%d, %d, %d)", svec_800ABA88.vx, svec_800ABA88.vy, svec_800ABA88.vz);
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

                if (ImGui::Button("Reset")) {
                    inited = false;
                    imgui_cam_override = 0;
                }
            }
        }
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}
