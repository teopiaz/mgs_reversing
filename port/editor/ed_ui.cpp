/* ImGui inspector for the editor: stage info, actor list, HZD layer toggles,
   camera controls. Replaces port/imgui_debug.cpp for the editor binary. */

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>

extern "C" {
#include "editor.h"
void port_vram_toggle_debug(void);
int  port_vram_debug_view(void);
}

static bool s_initialized = false;

extern "C" void ed_ui_init(void *sdl_window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL((SDL_Window *)sdl_window, SDL_GL_GetCurrentContext());
    ImGui_ImplOpenGL3_Init("#version 330 core");
    s_initialized = true;
}

extern "C" void ed_ui_shutdown(void)
{
    if (!s_initialized) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    s_initialized = false;
}

extern "C" void ed_ui_process_event(void *sdl_event)
{
    if (s_initialized)
        ImGui_ImplSDL2_ProcessEvent((SDL_Event *)sdl_event);
}

extern "C" void ed_ui_new_frame(void)
{
    if (!s_initialized) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

extern "C" int ed_ui_wants_mouse(void)
{
    return s_initialized && ImGui::GetIO().WantCaptureMouse ? 1 : 0;
}

extern "C" int ed_ui_wants_keyboard(void)
{
    return s_initialized && ImGui::GetIO().WantCaptureKeyboard ? 1 : 0;
}

static char s_actor_filter[32] = {0};

static bool stricontains(const char *hay, const char *needle)
{
    if (!*needle) return true;
    size_t hn = std::strlen(hay), nn = std::strlen(needle);
    if (nn > hn) return false;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t j = 0;
        for (; j < nn; j++) {
            char a = hay[i+j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) break;
        }
        if (j == nn) return true;
    }
    return false;
}

static void inspector_actors(void)
{
    if (!ImGui::CollapsingHeader("Actors", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::Checkbox("Markers",   (bool *)&g_show_actors);    ImGui::SameLine();
    ImGui::Checkbox("Models",    (bool *)&g_show_actor_models); ImGui::SameLine();
    ImGui::Checkbox("Rotations", (bool *)&g_show_actor_rotations);
    ImGui::InputText("filter##actors", s_actor_filter, sizeof(s_actor_filter));
    ImGui::Text("%d spatial actors", g_actor_count);
    if (ImGui::BeginTable("actors", 6, ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
            ImVec2(0, 240))) {
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Inst");
        ImGui::TableSetupColumn("Pos");
        ImGui::TableSetupColumn("Proc");
        ImGui::TableSetupColumn("Rot");
        ImGui::TableSetupColumn("Src");
        ImGui::TableHeadersRow();
        for (int i = 0; i < g_actor_count; i++) {
            EdActor *a = &g_actors[i];
            if (s_actor_filter[0] && !stricontains(a->type, s_actor_filter))
                continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool sel = (i == g_actor_selected);
            char label[64];
            std::snprintf(label, sizeof(label), "%s##%d", a->type, i);
            if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_SpanAllColumns)) {
                g_actor_selected = i;
                if (a->has_pos)
                    ed_camera_focus(a->pos[0], a->pos[1], a->pos[2], 5000.0f);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(a->instance);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d %d %d", a->pos[0], a->pos[1], a->pos[2]);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(a->proc);
            ImGui::TableSetColumnIndex(4);
            if (a->rot_b >= 0) ImGui::Text("b:%d", a->rot_b);
            else                ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(a->from_demo ? "demo" : "scen");
        }
        ImGui::EndTable();
    }
}

/* Generic HZD list. `count_fn`/`get_fn` come from ed_hzd.c.
   `selected_idx` is the matching g_sel_* int so the wireframe highlights it. */
static void hzd_list_table(const char *label, int *selected_idx,
                           int (*count_fn)(void),
                           int (*get_fn)(int, EdHzdItem *))
{
    int total = count_fn();
    if (!ImGui::TreeNode(label, "%s (%d)", label, total)) return;
    if (ImGui::BeginTable(label, 2, ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
            ImVec2(0, 160))) {
        ImGui::TableSetupColumn("Tag");
        ImGui::TableSetupColumn("Pos");
        ImGui::TableHeadersRow();
        for (int i = 0; i < total; i++) {
            EdHzdItem item;
            if (!get_fn(i, &item)) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool sel = (*selected_idx == i);
            char id[40];
            std::snprintf(id, sizeof(id), "%s##%s%d", item.tag, label, i);
            if (ImGui::Selectable(id, sel, ImGuiSelectableFlags_SpanAllColumns)) {
                *selected_idx = i;
                ed_camera_focus(item.cx, item.cy, item.cz, 5000.0f);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d %d %d", item.cx, item.cy, item.cz);
        }
        ImGui::EndTable();
    }
    ImGui::TreePop();
}

static void inspector_hzd_layers(void)
{
    if (!ImGui::CollapsingHeader("HZD overlay", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::Checkbox("Walls",   (bool *)&g_show_walls);   ImGui::SameLine();
    ImGui::Checkbox("Floors",  (bool *)&g_show_floors);  ImGui::SameLine();
    ImGui::Checkbox("Traps",   (bool *)&g_show_traps);
    ImGui::Checkbox("Cameras", (bool *)&g_show_cameras); ImGui::SameLine();
    ImGui::Checkbox("Zones",   (bool *)&g_show_zones);   ImGui::SameLine();
    ImGui::Checkbox("Routes",  (bool *)&g_show_routes);
    ImGui::Checkbox("Trap labels", (bool *)&g_show_trap_labels);

    hzd_list_table("Walls##list",   &g_sel_wall,   ed_hzd_count_walls,   ed_hzd_get_wall);
    hzd_list_table("Floors##list",  &g_sel_floor,  ed_hzd_count_floors,  ed_hzd_get_floor);
    hzd_list_table("Traps##list",   &g_sel_trap,   ed_hzd_count_traps,   ed_hzd_get_trap);
    hzd_list_table("Cameras##list", &g_sel_cam,    ed_hzd_count_cameras, ed_hzd_get_camera);
    hzd_list_table("Routes##list",  &g_sel_route,  ed_hzd_count_routes,  ed_hzd_get_route);
}

static void inspector_stage_meta(void)
{
    if (!ImGui::CollapsingHeader("Stage data"))
        return;
    EdStageEntry entries[256];
    int n = ed_stage_collect_entries(entries, 256);
    ImGui::Text("%d cache entries", n);
    if (ImGui::BeginTable("stage_meta", 3, ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
            ImVec2(0, 200))) {
        ImGui::TableSetupColumn("Ext");
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name hash");
        ImGui::TableHeadersRow();
        for (int i = 0; i < n; i++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%c", entries[i].ext);
            ImGui::TableSetColumnIndex(1); ImGui::Text("0x%X", entries[i].id);
            ImGui::TableSetColumnIndex(2); ImGui::Text("0x%04X", entries[i].id & 0xFFFF);
        }
        ImGui::EndTable();
    }
}

static void inspector_view_options(void)
{
    if (!ImGui::CollapsingHeader("View"))
        return;
    ImGui::Checkbox("World axes", (bool *)&g_show_axes);
    bool vram_dbg = port_vram_debug_view() != 0;
    if (ImGui::Checkbox("VRAM viewer", &vram_dbg))
        port_vram_toggle_debug();
}

static void inspector_camera(void)
{
    if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::Text("Pos:   %.0f  %.0f  %.0f", g_cam.pos[0], g_cam.pos[1], g_cam.pos[2]);
    ImGui::Text("Yaw:   %.3f rad   Pitch: %.3f rad", g_cam.yaw, g_cam.pitch);
    ImGui::SliderFloat("FOV scale", &g_cam.fov_scale, 0.3f, 4.0f, "%.2f");
    if (ImGui::Button("Reset (Home)")) {
        ed_camera_default();
    }
    ImGui::TextDisabled("WASD: move | Q/E: up/down | RMB+drag: look | arrows: rotate");
    ImGui::TextDisabled("Shift: 4× speed | Ctrl: 0.25× speed");
}

/* Stage picker. The combo lists every entry from STAGE.DIR; switching
   triggers ed_load_stage which frees the previous buffer, wipes the GV
   cache and VRAM, then re-runs FS_LoadStageRequest. */
static int s_combo_idx = -1;     /* index in port_fs stage list */
static char s_filter[32] = {0};

static void inspector_stage_picker(void)
{
    int total = ed_stage_count();

    /* Sync combo index to the currently-loaded stage if it shifted. */
    if (s_combo_idx < 0 || s_combo_idx >= total) {
        for (int i = 0; i < total; i++) {
            const char *n = ed_stage_name(i);
            if (n && std::strncmp(n, g_stage.stage_name, 8) == 0) {
                s_combo_idx = i;
                break;
            }
        }
    }

    ImGui::InputText("filter", s_filter, sizeof(s_filter));
    const char *preview = (s_combo_idx >= 0) ? ed_stage_name(s_combo_idx) : "<none>";
    if (ImGui::BeginCombo("stage", preview)) {
        for (int i = 0; i < total; i++) {
            const char *n = ed_stage_name(i);
            if (!n) continue;
            if (s_filter[0] && !std::strstr(n, s_filter)) continue;
            bool sel = (i == s_combo_idx);
            if (ImGui::Selectable(n, sel)) {
                if (i != s_combo_idx) {
                    s_combo_idx = i;
                    ed_load_stage(n);
                    ed_camera_default();
                }
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        ed_load_stage(g_stage.stage_name);
    }
}

/* Floating overlay drawn on the background draw list — sits behind the
   inspector window so it doesn't intercept clicks. */
static void draw_trap_labels(void)
{
    if (!g_show_trap_labels || !g_stage.hzd_map) return;
    EdHzdLabel labels[256];
    int n = ed_hzd_collect_trap_labels(labels, 256);
    if (n <= 0) return;
    ImGuiIO &io = ImGui::GetIO();
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ImU32 col = IM_COL32(180, 255, 80, 220);
    ImU32 sh  = IM_COL32(0, 0, 0, 200);
    for (int i = 0; i < n; i++) {
        float x = labels[i].sx * io.DisplaySize.x;
        float y = labels[i].sy * io.DisplaySize.y;
        dl->AddText(ImVec2(x + 1, y + 1), sh, labels[i].name);
        dl->AddText(ImVec2(x,     y    ), col, labels[i].name);
    }
}

/* Click anywhere in the viewport (i.e. not on an ImGui window) to pick the
   closest actor marker. Uses ed_screen_to_world_ray + ed_actors_pick_ray. */
static void handle_viewport_pick(void)
{
    ImGuiIO &io = ImGui::GetIO();
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (io.WantCaptureMouse) return;
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    float sx = io.MousePos.x / io.DisplaySize.x;
    float sy = io.MousePos.y / io.DisplaySize.y;
    float origin[3], dir[3];
    ed_screen_to_world_ray(sx, sy, origin, dir);
    int hit = ed_actors_pick_ray(origin, dir);
    if (hit >= 0) {
        g_actor_selected = hit;
        EdActor *a = &g_actors[hit];
        if (a->has_pos)
            ed_camera_focus(a->pos[0], a->pos[1], a->pos[2], 5000.0f);
    }
}

extern "C" void ed_ui_draw(void)
{
    if (!s_initialized) return;

    draw_trap_labels();
    handle_viewport_pick();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 720), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("MGS Stage Editor")) {
        ImGui::Text("Stage: %s%s", g_stage.stage_name,
                    g_stage.loaded ? "" : " (not loaded)");
        ImGui::SameLine();
        ImGuiIO &io = ImGui::GetIO();
        ImGui::Text("  |  %.1f fps", io.Framerate);
        ImGui::Separator();

        inspector_stage_picker();
        ImGui::Separator();
        inspector_view_options();
        inspector_camera();
        inspector_hzd_layers();
        inspector_actors();
        inspector_stage_meta();
    }
    ImGui::End();
}

extern "C" void ed_ui_render(void)
{
    if (!s_initialized) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
