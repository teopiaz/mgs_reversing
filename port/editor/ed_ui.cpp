/* ImGui inspector for the editor: stage info, actor list, HZD layer toggles,
   camera controls. Replaces port/imgui_debug.cpp for the editor binary. */

#include "imgui.h"
#include "imgui_internal.h"  /* DockBuilder* APIs (still internal in 1.92) */
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>

extern "C" {
#include "editor.h"
#include "libdg/gl_renderer.h"
void port_vram_toggle_debug(void);
int  port_vram_debug_view(void);
void gl_renderer_stats(int *out_tri_verts, int *out_line_verts);
int  gl_renderer_save_ppm(const char *path);
extern int gl_debug_wireframe;
extern int gl_debug_skip_lines;
extern int gl_debug_no_textures;
/* CLI-supplied path to the disc image (port/libfs/libfs.c). Used by the
 * Play button to spawn the live game with the same ISO the editor is
 * already reading. */
extern const char *port_iso_override;
}

/* True iff the dockable "3D View" panel is hovered or focused this frame.
 * Camera input gates on this so RMB-drag doesn't fight panel interaction. */
static bool s_3dview_active = false;
/* Index of the ortho pane (1=Top, 2=Front, 3=Side) currently hovered.
 * 0 = none. Used by the ortho cam pan/zoom handlers. */
static int  s_ortho_active = 0;
/* 3D View panel's image rect in OS-window screen coords, captured during
 * draw_3dview_window. Used to map ed_world_to_screen 0..1 normalized
 * coords to ImGui screen pixels for label overlays. Zero-sized when the
 * panel hasn't drawn yet. */
static ImVec2 s_3dview_origin = ImVec2(0, 0);
static ImVec2 s_3dview_size   = ImVec2(0, 0);

extern "C" int ed_ui_3dview_active(void) { return s_3dview_active ? 1 : 0; }
extern "C" int ed_ui_ortho_active(void)  { return s_ortho_active; }

#include <ctime>

/* Stage picker state. */
static int  s_combo_idx = -1;
static char s_filter[32] = {0};

/* Camera bookmarks. Slot 0..3 stores pos+yaw+pitch+fov. */
struct CamBookmark { float pos[3]; float yaw, pitch, fov; bool used; };
static CamBookmark s_bookmarks[4] = {};

static void save_bookmark(int i)
{
    s_bookmarks[i].pos[0] = g_cam.pos[0];
    s_bookmarks[i].pos[1] = g_cam.pos[1];
    s_bookmarks[i].pos[2] = g_cam.pos[2];
    s_bookmarks[i].yaw    = g_cam.yaw;
    s_bookmarks[i].pitch  = g_cam.pitch;
    s_bookmarks[i].fov    = g_cam.fov_scale;
    s_bookmarks[i].used   = true;
}

static void load_bookmark(int i)
{
    if (!s_bookmarks[i].used) return;
    g_cam.pos[0]    = s_bookmarks[i].pos[0];
    g_cam.pos[1]    = s_bookmarks[i].pos[1];
    g_cam.pos[2]    = s_bookmarks[i].pos[2];
    g_cam.yaw       = s_bookmarks[i].yaw;
    g_cam.pitch     = s_bookmarks[i].pitch;
    g_cam.fov_scale = s_bookmarks[i].fov;
}

static int s_goto_xyz[3] = {0, 0, 0};

static bool s_show_help = false;

static bool s_initialized = false;

extern "C" void ed_ui_init(void *sdl_window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    /* Hammer-style dockable layout: 3D View + side panels can be torn off,
     * rearranged, and saved to imgui.ini. The PassthruCentralNode flag is
     * used at the DockSpace call site so the empty central area renders
     * transparent until the user docks something into it. */
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
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

/* Actor display mode. 0 = hidden, 1 = cube markers, 2 = real KMD model
   when available (cube fallback otherwise). g_show_actors stays on in both
   modes so actors without a resolved KMD still show as a cube — the
   curated type→KMD table only covers a handful of cases. */
static int s_actor_mode = 1;
/* ITEM markers get a small label showing the resolved item name (cigs /
 * ration / box1 / ...) projected through the active 3D viewport. */
static int g_show_item_labels = 1;

static void apply_actor_mode(void)
{
    g_show_actors       = (s_actor_mode != 0) ? 1 : 0;
    g_show_actor_models = (s_actor_mode == 2) ? 1 : 0;
}

static void tab_actors(void)
{
    apply_actor_mode();

    /* Display mode + rotation arrows on one row. */
    const char *modes[] = { "Hidden", "Cubes", "Models" };
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("display", &s_actor_mode, modes, 3)) apply_actor_mode();
    ImGui::SameLine();
    ImGui::Checkbox("rotations", (bool *)&g_show_actor_rotations);
    ImGui::SameLine();
    ImGui::Checkbox("item labels", (bool *)&g_show_item_labels);
    ImGui::SameLine();
    ImGui::Checkbox("vision cones", (bool *)&g_show_vision_cones);

    /* Filter + reload + save on one row. */
    ImGui::SetNextItemWidth(140);
    ImGui::InputTextWithHint("##filter", "filter type...", s_actor_filter, sizeof(s_actor_filter));
    ImGui::SameLine();
    char tsv_path[64];
    /* Path is passed as JSON; ed_actors_load falls back to the sibling
     * .tsv if no JSON is present. Save still writes TSV (the format the
     * import pipeline reads back via tools/build_editor_data.py). */
    std::snprintf(tsv_path, sizeof(tsv_path), "data/%s_actors.json",
                  g_stage.stage_name);
    if (ImGui::Button("Reload##actors")) ed_actors_load(tsv_path);
    ImGui::SameLine();
    if (g_actors_dirty) {
        if (ImGui::Button("Save*")) ed_actors_save(tsv_path);
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Save");
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    int visible = 0;
    for (int i = 0; i < g_actor_count; i++) {
        if (!s_actor_filter[0] || stricontains(g_actors[i].type, s_actor_filter))
            visible++;
    }
    ImGui::Text("(%d / %d)%s", visible, g_actor_count,
                g_actors_dirty ? "  unsaved" : "");

    if (ImGui::BeginTable("actors", 4, ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
            ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Type",   ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Inst",   ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Pos",    ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Src",    ImGuiTableColumnFlags_WidthFixed,   38);
        ImGui::TableSetupScrollFreeze(0, 1);
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

static void tab_hzd(void)
{
    /* All visibility toggles in a tight grid. Trap labels grouped here too
       since it's a HZD-driven overlay. */
    ImGui::Checkbox("walls",   (bool *)&g_show_walls);   ImGui::SameLine();
    ImGui::Checkbox("floors",  (bool *)&g_show_floors);  ImGui::SameLine();
    ImGui::Checkbox("traps",   (bool *)&g_show_traps);   ImGui::SameLine();
    ImGui::Checkbox("cameras", (bool *)&g_show_cameras);
    ImGui::Checkbox("zones",   (bool *)&g_show_zones);   ImGui::SameLine();
    ImGui::Checkbox("routes",  (bool *)&g_show_routes);  ImGui::SameLine();
    ImGui::Checkbox("labels",  (bool *)&g_show_trap_labels);
    ImGui::Separator();

    hzd_list_table("walls##list",   &g_sel_wall,   ed_hzd_count_walls,   ed_hzd_get_wall);
    hzd_list_table("floors##list",  &g_sel_floor,  ed_hzd_count_floors,  ed_hzd_get_floor);
    hzd_list_table("traps##list",   &g_sel_trap,   ed_hzd_count_traps,   ed_hzd_get_trap);
    hzd_list_table("cameras##list", &g_sel_cam,    ed_hzd_count_cameras, ed_hzd_get_camera);
    hzd_list_table("routes##list",  &g_sel_route,  ed_hzd_count_routes,  ed_hzd_get_route);
}

/* Format byte count as KiB / MiB depending on size. Same convention as
 * the rest of the engine debug output. */
static void fmt_bytes(char *buf, size_t buflen, int bytes)
{
    if (bytes >= 1024 * 1024)
        std::snprintf(buf, buflen, "%.1f MiB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        std::snprintf(buf, buflen, "%.1f KiB", bytes / 1024.0);
    else
        std::snprintf(buf, buflen, "%d B", bytes);
}

/* Diagnostic tab: live counters for memory pools, texture cache, actor
 * list, and stage geometry. Read-only; useful for spotting leaks during
 * stage authoring iteration. */
static void tab_info(void)
{
    /* --- Memory pools (GV heap) --- */
    EdMemStats mem;
    ed_collect_memory_stats(&mem);
    {
        char tot[24], used[24];
        fmt_bytes(tot,  sizeof(tot),  mem.total);
        fmt_bytes(used, sizeof(used), mem.used);
        ImGui::Text("Memory: %s used / %s total", used, tot);
        if (mem.total > 0) {
            float frac = (float)mem.used / (float)mem.total;
            ImGui::ProgressBar(frac, ImVec2(-1, 0));
        }
    }
    if (ImGui::BeginTable("mem_heaps", 5, ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Pool");
        ImGui::TableSetupColumn("Used");
        ImGui::TableSetupColumn("Free");
        ImGui::TableSetupColumn("Largest free");
        ImGui::TableSetupColumn("Allocs");
        ImGui::TableHeadersRow();
        for (int i = 0; i < 3; i++) {
            EdHeapStat *h = &mem.heap[i];
            char u[24], f[24], m[24];
            fmt_bytes(u, sizeof(u), h->used);
            fmt_bytes(f, sizeof(f), h->freed);
            fmt_bytes(m, sizeof(m), h->max_free_block);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(h->name);
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(u);
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(f);
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(m);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%d", h->n_units);
        }
        ImGui::EndTable();
    }
    ImGui::Separator();

    /* --- Textures (DG_TEX cache → PSX VRAM) --- */
    EdTexStats tex;
    ed_collect_texture_stats(&tex);
    {
        char vb[24];
        fmt_bytes(vb, sizeof(vb), tex.vram_bytes);
        ImGui::Text("Textures: %d active   VRAM %s   pixels %d",
                    tex.n_textures, vb, tex.vram_pixels);
        ImGui::Text("by bpp: 4 = %d   8 = %d   16 = %d",
                    tex.n_4bpp, tex.n_8bpp, tex.n_16bpp);
        ImGui::TextDisabled("(PSX VRAM is 1 MiB — frame buffer + textures share it)");
    }
    ImGui::Separator();

    /* --- Actors --- */
    EdActorStats act;
    ed_collect_actor_stats(&act);
    ImGui::Text("Actors: %d total", act.total);
    ImGui::Text("  with pos %d   with model %d   with rotation %d",
                act.with_pos, act.with_model, act.with_rotation);
    ImGui::Text("  source: scen %d   demo %d", act.from_scen, act.from_demo);
    ImGui::Separator();

    /* --- Stage geometry --- */
    EdGeomStats geom;
    ed_collect_geom_stats(&geom);
    ImGui::Text("Geometry: %d KMD%s   %d models   %d faces   %d verts",
                geom.n_kmds, geom.n_kmds == 1 ? "" : "s",
                geom.n_models, geom.n_faces, geom.n_vertices);
}

/* Combined Scene tab — stage picker + view toggles + cache table. The
   stage picker is the most-used widget so it goes at the very top. */
static void tab_scene(void)
{
    /* Stage picker (logic unchanged; visuals tightened). */
    int total = ed_stage_count();
    if (s_combo_idx < 0 || s_combo_idx >= total) {
        for (int i = 0; i < total; i++) {
            const char *n = ed_stage_name(i);
            if (n && std::strncmp(n, g_stage.stage_name, 8) == 0) {
                s_combo_idx = i; break;
            }
        }
    }
    ImGui::SetNextItemWidth(140);
    ImGui::InputTextWithHint("##stagefilter", "filter stage...", s_filter, sizeof(s_filter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    const char *preview = (s_combo_idx >= 0) ? ed_stage_name(s_combo_idx) : "<none>";
    if (ImGui::BeginCombo("##stagecombo", preview)) {
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
    if (ImGui::Button("Reload##stage"))
        ed_load_stage(g_stage.stage_name);

    /* Reimport button — only shown when the current stage came from
       extra_stages/<name>/manifest.json. Shells out to the Python
       importer with the stored args, then reloads the stage. */
    bool has_manifest = false;
    {
        char manifest_path[128];
        std::snprintf(manifest_path, sizeof(manifest_path),
                      "extra_stages/%s/manifest.json", g_stage.stage_name);
        FILE *mf = std::fopen(manifest_path, "r");
        if (mf) {
            std::fclose(mf);
            has_manifest = true;
            ImGui::SameLine();
            if (ImGui::Button("Reimport")) {
                char cmd[512];
                std::snprintf(cmd, sizeof(cmd),
                    "cd ../.. && python3 tools/import_stage.py "
                    "--manifest port/editor/extra_stages/%s/manifest.json",
                    g_stage.stage_name);
                int rc = system(cmd);
                if (rc == 0) ed_load_stage(g_stage.stage_name);
                else std::fprintf(stderr, "editor: reimport failed (rc=%d)\n", rc);
            }
        }
    }

    /* Play button: launches the live game (./mgs) on this stage in a
     * detached background process so the editor stays responsive. For
     * custom stages with a manifest, reimports first so any unsaved
     * GCL/OBJ edits land in the datacnf before the game reads it.
     * Disabled when no stage is loaded — there's nothing to play. */
    ImGui::SameLine();
    if (!g_stage.loaded) ImGui::BeginDisabled();
    if (ImGui::Button("Play")) {
        /* Reimport first so edits to scenerio.gcl / OBJs reach the
         * live game without the user having to press Reimport. */
        if (has_manifest) {
            char rc_cmd[512];
            std::snprintf(rc_cmd, sizeof(rc_cmd),
                "cd ../.. && python3 tools/import_stage.py "
                "--manifest port/editor/extra_stages/%s/manifest.json",
                g_stage.stage_name);
            int rc = system(rc_cmd);
            if (rc != 0)
                std::fprintf(stderr,
                    "editor: Play: reimport failed (rc=%d) — launching anyway\n", rc);
        }
        /* Reuse the ISO the editor was launched with. Fallback to the
         * conventional location next to the editor binary. */
        const char *iso = port_iso_override;
        if (!iso || !*iso) iso = "./ISO/mgs.cue";
        /* Background launch (`&`) so the editor doesn't block, and
         * `setsid` so killing the editor doesn't take the game with it.
         * Output goes to /tmp/mgs.log for post-mortem if the game
         * crashes; stdin is redirected from /dev/null to avoid the bg
         * job freezing on TTY. */
        char cmd[768];
        std::snprintf(cmd, sizeof(cmd),
            "cd .. && PORT_AUTOLOAD_STAGE=%s PORT_GL=1 "
            "nohup ./mgs '%s' </dev/null >/tmp/mgs_%s.log 2>&1 &",
            g_stage.stage_name, iso, g_stage.stage_name);
        int rc = system(cmd);
        if (rc != 0)
            std::fprintf(stderr,
                "editor: Play launch failed (rc=%d): %s\n", rc, cmd);
        else
            std::printf("editor: Play — launched mgs on '%s' (log: /tmp/mgs_%s.log)\n",
                        g_stage.stage_name, g_stage.stage_name);
    }
    if (ImGui::IsItemHovered() && g_stage.loaded) {
        ImGui::SetTooltip("Launch ./mgs with PORT_AUTOLOAD_STAGE=%s.\n"
                          "%sBackground process; output → /tmp/mgs_%s.log",
                          g_stage.stage_name,
                          has_manifest ? "Reimports the custom stage first.\n" : "",
                          g_stage.stage_name);
    }
    if (!g_stage.loaded) ImGui::EndDisabled();

    ImGui::Separator();

    /* Render toggles. */
    ImGui::Checkbox("axes",       (bool *)&g_show_axes);          ImGui::SameLine();
    ImGui::Checkbox("wireframe",  (bool *)&gl_debug_wireframe);   ImGui::SameLine();
    ImGui::Checkbox("untextured", (bool *)&gl_debug_no_textures);
    bool vram_dbg = port_vram_debug_view() != 0;
    if (ImGui::Checkbox("VRAM viewer", &vram_dbg))
        port_vram_toggle_debug();
    ImGui::Separator();

    /* Compact cache-entry list (collapsed by default; rarely needed). */
    if (ImGui::CollapsingHeader("Cache entries")) {
        EdStageEntry entries[256];
        int n = ed_stage_collect_entries(entries, 256);
        ImGui::Text("%d entries", n);
        if (ImGui::BeginTable("stage_meta", 3, ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                ImVec2(0, 200))) {
            ImGui::TableSetupColumn("Ext", ImGuiTableColumnFlags_WidthFixed, 30);
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
}

/* Cast the mouse cursor as a world ray and intersect with the y=0 plane
   (PSX ground level). Returns 1 if the ray hits, 0 if parallel/behind. */
static int mouse_world_at_ground(int *wx, int *wy, int *wz)
{
    ImGuiIO &io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return 0;
    float sx = io.MousePos.x / io.DisplaySize.x;
    float sy = io.MousePos.y / io.DisplaySize.y;
    float origin[3], dir[3];
    ed_screen_to_world_ray(sx, sy, origin, dir);
    if (dir[1] < 1e-4f && dir[1] > -1e-4f) return 0;
    float t = -origin[1] / dir[1];   /* y=0 plane */
    if (t < 0) return 0;
    *wx = (int)(origin[0] + t * dir[0]);
    *wy = 0;
    *wz = (int)(origin[2] + t * dir[2]);
    return 1;
}

static void tab_camera(void)
{
    /* State readout. */
    ImGui::Text("pos   %.0f  %.0f  %.0f", g_cam.pos[0], g_cam.pos[1], g_cam.pos[2]);
    ImGui::Text("yaw   %.1f°    pitch %.1f°",
                g_cam.yaw * 57.2958f, g_cam.pitch * 57.2958f);
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("FOV", &g_cam.fov_scale, 0.3f, 4.0f, "%.2f");

    /* Mode toggle (mirrors the overlay button on the 3D View pane). */
    int mode = g_cam.mode;
    const char *modes[] = { "Fly", "Orbit" };
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("mode", &mode, modes, 2)) ed_camera_set_mode(mode);
    if (g_cam.mode == ED_CAM_MODE_ORBIT) {
        ImGui::SameLine();
        ImGui::TextDisabled("dist %.0f", g_cam.orbit_dist);
    }

    /* Presets. */
    if (ImGui::Button("Top-down")) ed_camera_default();
    ImGui::SameLine();
    if (ImGui::Button("First-person")) ed_camera_first_person();
    ImGui::SameLine();
    if (ImGui::Button("Frame all")) {
        float bmin[3], bmax[3];
        if (ed_compute_stage_aabb(bmin, bmax)) ed_camera_frame_aabb(bmin, bmax);
    }
    ImGui::SameLine();
    if (ImGui::Button("Screenshot")) {
        char ts[64];
        std::time_t now = std::time(NULL);
        std::strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", std::localtime(&now));
        char path[96];
        std::snprintf(path, sizeof(path), "screenshot-%s-%s.ppm",
                      g_stage.stage_name, ts);
        if (gl_renderer_save_ppm(path) == 0)
            std::printf("editor: wrote %s\n", path);
    }

    /* Goto. */
    ImGui::Separator();
    ImGui::PushItemWidth(70);
    ImGui::InputInt("##gx", &s_goto_xyz[0], 0); ImGui::SameLine();
    ImGui::InputInt("##gy", &s_goto_xyz[1], 0); ImGui::SameLine();
    ImGui::InputInt("##gz", &s_goto_xyz[2], 0); ImGui::SameLine();
    ImGui::PopItemWidth();
    if (ImGui::Button("Goto"))
        ed_camera_focus(s_goto_xyz[0], s_goto_xyz[1], s_goto_xyz[2], 5000.0f);
    int wx, wy, wz;
    if (mouse_world_at_ground(&wx, &wy, &wz)) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%d %d %d)", wx, wy, wz);
    }

    /* Bookmarks: one row of 4. Left-click loads, right-click saves. */
    ImGui::Separator();
    ImGui::TextUnformatted("Bookmarks:"); ImGui::SameLine();
    for (int b = 0; b < 4; b++) {
        char id[8];
        std::snprintf(id, sizeof(id), "%d##bm", b + 1);
        if (!s_bookmarks[b].used) ImGui::BeginDisabled();
        if (ImGui::Button(id)) load_bookmark(b);
        if (!s_bookmarks[b].used) ImGui::EndDisabled();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            save_bookmark(b);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(s_bookmarks[b].used
                ? "left-click: load   |   right-click: overwrite"
                : "right-click: save current view");
        }
        if (b < 3) ImGui::SameLine();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("?##help")) s_show_help = !s_show_help;
}

/* Floating "Selected" card, only shown while something is selected.
   Position + rotation are editable and feed back into g_actors[] live —
   the marker moves as the user types. */
static void draw_selected_card(void)
{
    if (g_actor_selected < 0 || g_actor_selected >= g_actor_count) return;
    EdActor *a = &g_actors[g_actor_selected];

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 290, 10),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoCollapse;
    bool open = true;
    if (ImGui::Begin("Selected", &open, flags)) {
        ImU32 col = IM_COL32((a->color >> 16) & 0xFF,
                             (a->color >>  8) & 0xFF,
                             (a->color >>  0) & 0xFF, 255);
        ImGui::ColorButton("##swatch", ImColor(col), ImGuiColorEditFlags_NoTooltip,
                           ImVec2(14, 14));
        ImGui::SameLine();
        ImGui::TextUnformatted(a->type);
        ImGui::Text("inst   %s", a->instance);
        ImGui::Text("from   %s   proc %s",
                    a->from_demo ? "demo" : "scen", a->proc);
        ImGui::Text("model  %s", a->kmd_def ? "yes" : "-");
        ImGui::Separator();

        /* Editable position. InputInt3 returns true on any change. */
        if (ImGui::InputInt3("pos", a->pos)) g_actors_dirty = 1;

        /* Editable rotation (toggleable: actor may have none). */
        bool has_rot = a->rot_b >= 0;
        if (ImGui::Checkbox("##has_rot", &has_rot)) {
            a->rot_b = has_rot ? 0 : -1;
            g_actors_dirty = 1;
        }
        ImGui::SameLine();
        if (a->rot_b >= 0) {
            int r = a->rot_b;
            char buf[24];
            std::snprintf(buf, sizeof(buf), "b:%d (%.0f°)",
                          r, r * 360.0f / 256.0f);
            if (ImGui::SliderInt("rot", &r, 0, 255, buf)) {
                a->rot_b = r & 0xFF;
                g_actors_dirty = 1;
            }
        } else {
            ImGui::TextDisabled("rot    -");
        }

        ImGui::Separator();
        if (ImGui::Button("Focus"))
            ed_camera_focus(a->pos[0], a->pos[1], a->pos[2], 5000.0f);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) g_actor_selected = -1;
    }
    ImGui::End();
    if (!open) g_actor_selected = -1;
}

/* Tab / Shift-Tab cycles through the (filtered) actor list. Wraps around. */
static void handle_actor_cycle(void)
{
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;
    bool fwd  = ImGui::IsKeyPressed(ImGuiKey_Tab) && !io.KeyShift;
    bool back = ImGui::IsKeyPressed(ImGuiKey_Tab) &&  io.KeyShift;
    if (!fwd && !back) return;

    int n = g_actor_count;
    if (n <= 0) return;

    int start = g_actor_selected;
    int dir = fwd ? 1 : -1;
    for (int step = 0; step < n; step++) {
        start = (start + dir + n) % n;
        EdActor *a = &g_actors[start];
        if (s_actor_filter[0] && !stricontains(a->type, s_actor_filter)) continue;
        g_actor_selected = start;
        if (a->has_pos)
            ed_camera_focus(a->pos[0], a->pos[1], a->pos[2], 5000.0f);
        return;
    }
}

static void draw_help_window(void)
{
    if (!s_show_help) return;
    ImGui::SetNextWindowPos(ImVec2(440, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 240), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Keybindings", &s_show_help)) {
        ImGui::TextUnformatted(
            "WASD / QE        move / up-down\n"
            "RMB + drag       look (or orbit when in Orbit mode)\n"
            "Alt + RMB + drag orbit around target\n"
            "Wheel (3D)       dolly fly / zoom orbit\n"
            "Wheel (ortho)    zoom that pane\n"
            "MMB drag (ortho) pan that pane\n"
            "F                frame selection (active pane)\n"
            "arrows           rotate\n"
            "Shift / Ctrl     4× / 0.25× speed\n"
            "Home             top-down view\n"
            "click in 3D      pick actor / HZD entity\n"
            "Tab / Shift-Tab  cycle actors\n"
            "G                goto coordinate\n"
            "F1               toggle this window\n"
            "Esc              quit\n"
            "F11 / Alt-Enter  fullscreen\n"
            "RMB on bookmark  save current view");
    }
    ImGui::End();
}

/* (s_combo_idx / s_filter declared at the top of the file alongside the
   other inspector state.) */

/* Map a normalized (sx, sy) ∈ [0,1]² coming from ed_world_to_screen()
 * into the 3D View panel's screen-space rect. Returns 0 if the panel
 * isn't yet sized; caller should skip drawing in that case. */
static int viewport_to_panel(float sx_norm, float sy_norm,
                             float *out_x, float *out_y)
{
    if (s_3dview_size.x <= 0 || s_3dview_size.y <= 0) return 0;
    *out_x = s_3dview_origin.x + sx_norm * s_3dview_size.x;
    *out_y = s_3dview_origin.y + sy_norm * s_3dview_size.y;
    return 1;
}

/* Floating label overlay drawn on the foreground draw list. Foreground
 * (not background) so the labels render on top of all panels — the 3D
 * View panel's ImGui::Image otherwise hides them. Clipped to the panel
 * rect so labels don't bleed onto adjacent docked windows. */
static void draw_trap_labels(void)
{
    if (!g_show_trap_labels || !g_stage.hzd_map) return;
    EdHzdLabel labels[256];
    int n = ed_hzd_collect_trap_labels(labels, 256);
    if (n <= 0) return;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImU32 col = IM_COL32(180, 255, 80, 220);
    ImU32 sh  = IM_COL32(0, 0, 0, 200);
    dl->PushClipRect(s_3dview_origin,
                     ImVec2(s_3dview_origin.x + s_3dview_size.x,
                            s_3dview_origin.y + s_3dview_size.y), true);
    for (int i = 0; i < n; i++) {
        float x, y;
        if (!viewport_to_panel(labels[i].sx, labels[i].sy, &x, &y)) continue;
        dl->AddText(ImVec2(x + 1, y + 1), sh, labels[i].name);
        dl->AddText(ImVec2(x,     y    ), col, labels[i].name);
    }
    dl->PopClipRect();
}

static void draw_item_labels(void)
{
    if (!g_show_item_labels) return;
    if (s_3dview_size.x <= 0 || s_3dview_size.y <= 0) return;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    dl->PushClipRect(s_3dview_origin,
                     ImVec2(s_3dview_origin.x + s_3dview_size.x,
                            s_3dview_origin.y + s_3dview_size.y), true);
    for (int i = 0; i < g_actor_count; i++) {
        EdActor *a = &g_actors[i];
        if (!a->has_pos) continue;
        if (std::strcmp(a->type, "ITEM") != 0) continue;
        float sx, sy;
        if (!ed_world_to_screen(a->pos[0], a->pos[1], a->pos[2], &sx, &sy))
            continue;
        float x, y;
        if (!viewport_to_panel(sx, sy, &x, &y)) continue;
        const char *name = (a->item_id >= 0) ? ed_item_name(a->item_id) : "?";
        uint32_t rgb = (a->item_id >= 0) ? ed_item_color(a->item_id) : 0xC0C0C0;
        ImU32 col = IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 235);
        ImU32 sh  = IM_COL32(0, 0, 0, 200);
        /* Offset slightly above-right of the marker so the cube/model is
         * still visible underneath. */
        x += 6.0f; y -= 6.0f;
        dl->AddText(ImVec2(x + 1, y + 1), sh, name);
        dl->AddText(ImVec2(x,     y    ), col, name);
    }
    dl->PopClipRect();
}

/* Click anywhere in the viewport (i.e. not on an ImGui window) to pick the
   closest actor marker. Falls back to picking HZD traps/cameras if no
   actor is hit, so any selectable entity in the world responds to clicks. */
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

    int actor = ed_actors_pick_ray(origin, dir);
    if (actor >= 0) {
        g_actor_selected = actor;
        EdActor *a = &g_actors[actor];
        if (a->has_pos)
            ed_camera_focus(a->pos[0], a->pos[1], a->pos[2], 5000.0f);
        return;
    }
    /* No actor under the cursor — try HZD traps + cameras. */
    int kind = ed_hzd_pick_ray(origin, dir);
    if (kind > 0) {
        EdHzdItem item;
        bool ok = (kind == 1)
                ? ed_hzd_get_trap(g_sel_trap, &item)
                : ed_hzd_get_camera(g_sel_cam, &item);
        if (ok) ed_camera_focus(item.cx, item.cy, item.cz, 5000.0f);
    }
}

/* Goto modal — opened by 'G' anywhere in the editor (when ImGui isn't
   eating keystrokes). Three int inputs, Enter applies, Esc cancels. */
static bool s_goto_open = false;

static void draw_goto_modal(void)
{
    if (s_goto_open) {
        ImGui::OpenPopup("Goto coord");
        s_goto_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Goto coord", NULL,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        bool apply = false;
        ImGui::PushItemWidth(70);
        if (ImGui::InputInt("##gx", &s_goto_xyz[0], 0,
                ImGuiInputTextFlags_EnterReturnsTrue)) apply = true;
        ImGui::SameLine();
        if (ImGui::InputInt("##gy", &s_goto_xyz[1], 0,
                ImGuiInputTextFlags_EnterReturnsTrue)) apply = true;
        ImGui::SameLine();
        if (ImGui::InputInt("##gz", &s_goto_xyz[2], 0,
                ImGuiInputTextFlags_EnterReturnsTrue)) apply = true;
        ImGui::PopItemWidth();
        /* Cursor-coord shortcut button. */
        int wx, wy, wz;
        if (mouse_world_at_ground(&wx, &wy, &wz)) {
            if (ImGui::Button("from cursor")) {
                s_goto_xyz[0] = wx; s_goto_xyz[1] = wy; s_goto_xyz[2] = wz;
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Goto") || apply) {
            ed_camera_focus(s_goto_xyz[0], s_goto_xyz[1], s_goto_xyz[2], 5000.0f);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void handle_global_shortcuts(void)
{
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;
    if (ImGui::IsKeyPressed(ImGuiKey_G)) s_goto_open = true;
}

/* Dockable "3D View" panel: shows viewport 0's FBO via ImGui::Image, resizes
 * the FBO to match the panel content region, and tracks hover/focus so the
 * camera input only fires when the user is interacting with this view.
 * Handles hover-only wheel (dolly / orbit-zoom), F-to-frame, and shows a
 * small Fly/Orbit toggle button overlaid on the top-left of the image. */
static void draw_3dview_window(void)
{
    /* No padding so the image fills the panel exactly. */
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    bool open = ImGui::Begin("3D View", NULL, ImGuiWindowFlags_NoCollapse);
    if (open) {
        ImVec2 sz = ImGui::GetContentRegionAvail();
        int iw = (int)sz.x, ih = (int)sz.y;
        if (iw > 0 && ih > 0) gl_renderer_resize_viewport(GL_VIEWPORT_3D, iw, ih);

        ImVec2 img_pos = ImGui::GetCursorScreenPos();
        unsigned int tex = gl_renderer_get_viewport_color(GL_VIEWPORT_3D);
        if (tex && iw > 0 && ih > 0) {
            /* PSX FBO has +Y down; ImGui texture coords are top-left = (0,0)
             * but GL textures are bottom-left = (0,0). Flip V to display
             * upright by mapping uv0=(0,1) bottom-left and uv1=(1,0) top-right. */
            ImGui::Image((ImTextureID)(intptr_t)tex,
                         ImVec2((float)iw, (float)ih),
                         ImVec2(0, 1), ImVec2(1, 0));
        }
        bool img_hover = ImGui::IsItemHovered();
        s_3dview_active = ImGui::IsWindowHovered() || ImGui::IsWindowFocused();
        /* Capture the image rect for label overlays (item / trap names
         * project through the 3D viewport's matrix, so their normalized
         * coords need to land inside *this* panel — not the OS window). */
        s_3dview_origin = img_pos;
        s_3dview_size   = ImVec2((float)iw, (float)ih);

        /* Hover-only wheel: dolly in fly mode, scale orbit_dist in orbit mode. */
        if (img_hover) {
            ImGuiIO &io = ImGui::GetIO();
            if (io.MouseWheel != 0.0f) ed_camera_zoom(io.MouseWheel);
            if (ImGui::IsKeyPressed(ImGuiKey_F)) {
                float bmin[3], bmax[3];
                if (ed_compute_active_aabb(bmin, bmax))
                    ed_camera_frame_aabb(bmin, bmax);
            }
        }

        /* Fly/Orbit toggle overlay — small button anchored top-left of the
         * image so it doesn't interfere with hover input on the rest of
         * the pane. */
        if (iw > 0 && ih > 0) {
            ImGui::SetCursorScreenPos(ImVec2(img_pos.x + 6.0f, img_pos.y + 6.0f));
            const char *label = (g_cam.mode == ED_CAM_MODE_ORBIT) ? "Orbit" : "Fly";
            if (ImGui::SmallButton(label))
                ed_camera_set_mode(g_cam.mode == ED_CAM_MODE_ORBIT
                                   ? ED_CAM_MODE_FLY : ED_CAM_MODE_ORBIT);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Camera mode (click to toggle).\n"
                                  "Fly: WASD + RMB look.\n"
                                  "Orbit: RMB rotates around target.\n"
                                  "Alt+RMB always orbits.\n"
                                  "F: frame selection (or whole stage).");
        }
    } else {
        s_3dview_active = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

/* Dockable ortho pane (Top / Front / Side). Same as the 3D View but renders
 * an ortho wireframe and handles MMB-drag pan + wheel zoom directly. The
 * matching ortho cam state is in g_top_cam / g_front_cam / g_side_cam. */
static void draw_ortho_window(const char *title, int viewport_idx,
                              EdOrthoCam *cam, int active_id)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(480, 360), ImGuiCond_FirstUseEver);
    bool open = ImGui::Begin(title, NULL, ImGuiWindowFlags_NoCollapse);
    if (open) {
        ImVec2 sz = ImGui::GetContentRegionAvail();
        int iw = (int)sz.x, ih = (int)sz.y;
        if (iw > 0 && ih > 0) gl_renderer_resize_viewport(viewport_idx, iw, ih);

        unsigned int tex = gl_renderer_get_viewport_color(viewport_idx);
        if (tex && iw > 0 && ih > 0) {
            ImGui::Image((ImTextureID)(intptr_t)tex,
                         ImVec2((float)iw, (float)ih),
                         ImVec2(0, 1), ImVec2(1, 0));
        }

        /* Hover-only input: MMB drag pans, wheel zooms, F frames target.
         * IsItemHovered() fires only over the Image, not the dock tab area. */
        bool img_hover = ImGui::IsItemHovered();
        if (img_hover) {
            s_ortho_active = active_id;
            ImGuiIO &io = ImGui::GetIO();
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
                ed_camera_ortho_pan(cam, iw, ih,
                                    (int)io.MouseDelta.x,
                                    (int)io.MouseDelta.y);
            }
            if (io.MouseWheel != 0.0f) {
                ed_camera_ortho_zoom(cam, io.MouseWheel);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F)) {
                float bmin[3], bmax[3];
                if (ed_compute_active_aabb(bmin, bmax))
                    ed_camera_ortho_frame_aabb(cam, bmin, bmax);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

/* First-run dock layout: classic Hammer 4-pane grid in the centre + a
 * narrow inspector strip on the left. Stamped once when no imgui.ini
 * is found (DockBuilder needs an empty leaf node to split). */
static void build_default_dock_layout(ImGuiID dock_id)
{
    ImGui::DockBuilderRemoveNodeChildNodes(dock_id);
    ImGui::DockBuilderSetNodeSize(dock_id, ImGui::GetMainViewport()->Size);

    ImGuiID left, center, top_row, bot_row;
    ImGuiID tl, tr, bl, br;
    ImGui::DockBuilderSplitNode(dock_id, ImGuiDir_Left,  0.20f, &left,    &center);
    ImGui::DockBuilderSplitNode(center,  ImGuiDir_Up,    0.50f, &top_row, &bot_row);
    ImGui::DockBuilderSplitNode(top_row, ImGuiDir_Left,  0.50f, &tl,      &tr);
    ImGui::DockBuilderSplitNode(bot_row, ImGuiDir_Left,  0.50f, &bl,      &br);

    ImGui::DockBuilderDockWindow("MGS Stage Editor", left);
    ImGui::DockBuilderDockWindow("3D View",          tl);
    ImGui::DockBuilderDockWindow("Top (XZ)",         tr);
    ImGui::DockBuilderDockWindow("Front (XY)",       bl);
    ImGui::DockBuilderDockWindow("Side (YZ)",        br);
    ImGui::DockBuilderFinish(dock_id);
}

extern "C" void ed_ui_draw(void)
{
    if (!s_initialized) return;

    /* Top-level DockSpace: covers the entire OS window. Other windows
     * (3D View, inspector tabs, help) dock into it. PassthruCentralNode
     * keeps the empty centre transparent (we paint the dark grey clear
     * underneath in main.c). */
    ImGuiID dock_id =
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                     ImGuiDockNodeFlags_PassthruCentralNode);

    /* On first launch (no saved layout), stamp a sensible default. */
    static bool dock_layout_built = false;
    if (!dock_layout_built) {
        ImGuiDockNode *node = ImGui::DockBuilderGetNode(dock_id);
        if (!node || node->IsLeafNode()) {
            build_default_dock_layout(dock_id);
        }
        dock_layout_built = true;
    }

    /* Reset hover trackers each frame; per-window draw funcs re-set them. */
    s_ortho_active = 0;

    draw_3dview_window();
    draw_ortho_window("Top (XZ)",   GL_VIEWPORT_TOP,   &g_top_cam,   1);
    draw_ortho_window("Front (XY)", GL_VIEWPORT_FRONT, &g_front_cam, 2);
    draw_ortho_window("Side (YZ)",  GL_VIEWPORT_SIDE,  &g_side_cam,  3);

    draw_trap_labels();
    draw_item_labels();
    handle_viewport_pick();
    handle_actor_cycle();
    handle_global_shortcuts();
    if (ImGui::IsKeyPressed(ImGuiKey_F1) && !ImGui::GetIO().WantCaptureKeyboard)
        s_show_help = !s_show_help;
    draw_help_window();
    draw_goto_modal();
    draw_selected_card();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("MGS Stage Editor")) {
        /* Compact status line: stage + perf counters + cursor coord. */
        ImGuiIO &io = ImGui::GetIO();
        int tv = 0, lv = 0;
        gl_renderer_stats(&tv, &lv);
        ImGui::Text("%s%s   %.0f fps   %dt %dl",
                    g_stage.stage_name,
                    g_stage.loaded ? "" : " (none)",
                    io.Framerate, tv / 3, lv / 2);
        int wx, wy, wz;
        if (mouse_world_at_ground(&wx, &wy, &wz))
            ImGui::TextDisabled("cursor: %d %d %d", wx, wy, wz);
        else
            ImGui::TextDisabled("cursor: -");

        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("Scene"))   { tab_scene();   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Camera"))  { tab_camera();  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Actors"))  { tab_actors();  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("HZD"))     { tab_hzd();     ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Info"))    { tab_info();    ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

extern "C" void ed_ui_render(void)
{
    if (!s_initialized) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
