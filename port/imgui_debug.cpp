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

} /* extern "C" */

static bool show_actors = false;

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

extern "C" void imgui_render(SDL_Renderer *renderer)
{
    if (!show_actors)
        return;

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

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
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}
