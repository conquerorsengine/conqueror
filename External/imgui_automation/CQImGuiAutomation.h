#pragma once

// Conqueror UI automation hooks for Dear ImGui.
// imgui is built with IMGUI_ENABLE_TEST_ENGINE; these hooks record the
// screen rectangle + label of every widget submitted during a frame so the
// engine's automation runner can locate and click widgets by label.
// The hooks cost nothing unless EnableItemHooks(true) has been called.

#include "imgui.h"

namespace CQAutomation
{
    struct ItemRecord
    {
        ImGuiID ID = 0;
        ImVec2 Min;
        ImVec2 Max;
        char Label[160] = {};
        char Window[160] = {};
    };

    IMGUI_API void EnableItemHooks(bool enabled);
    IMGUI_API bool AreItemHooksEnabled();

    // Called once per frame before ImGui::NewFrame(): the items recorded during
    // the previous frame become queryable and a new recording starts.
    IMGUI_API void BeginFrame();

    // Finds an item recorded in the previous frame. 'label' is matched against
    // the visible part of the widget label (text before "##"), then against the
    // full label. 'window' (optional) must be a substring of the window name.
    // 'occurrence' selects the N-th match (0 = first).
    IMGUI_API const ItemRecord* FindItem(const char* label, const char* window = nullptr, int occurrence = 0);

    IMGUI_API int GetItemCount();
    IMGUI_API const ItemRecord* GetItem(int index);
}
