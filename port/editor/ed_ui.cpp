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

static void inspector_actors(void)
{
    if (!ImGui::CollapsingHeader("Actors", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::Checkbox("Show markers", (bool *)&g_show_actors);
    ImGui::Text("%d spatial actors", g_actor_count);
    if (ImGui::BeginTable("actors", 5, ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
            ImVec2(0, 240))) {
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Inst");
        ImGui::TableSetupColumn("Pos");
        ImGui::TableSetupColumn("Proc");
        ImGui::TableSetupColumn("Rot");
        ImGui::TableHeadersRow();
        for (int i = 0; i < g_actor_count; i++) {
            EdActor *a = &g_actors[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool sel = (i == g_actor_selected);
            char label[64];
            std::snprintf(label, sizeof(label), "%s##%d", a->type, i);
            if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_SpanAllColumns))
                g_actor_selected = i;
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(a->instance);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d %d %d", a->pos[0], a->pos[1], a->pos[2]);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(a->proc);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("0x%08X", a->color);
        }
        ImGui::EndTable();
    }
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

extern "C" void ed_ui_draw(void)
{
    if (!s_initialized) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 700), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("MGS Stage Editor")) {
        ImGui::Text("Stage: %s%s", g_stage.stage_name,
                    g_stage.loaded ? "" : " (not loaded)");
        ImGui::SameLine();
        ImGuiIO &io = ImGui::GetIO();
        ImGui::Text("  |  %.1f fps", io.Framerate);
        ImGui::Separator();

        inspector_camera();
        inspector_hzd_layers();
        inspector_actors();
    }
    ImGui::End();
}

extern "C" void ed_ui_render(void)
{
    if (!s_initialized) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
