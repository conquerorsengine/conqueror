#include "CQImGuiAutomation.h"
#include "imgui_internal.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
    bool s_Enabled = false;

    ImVector<CQAutomation::ItemRecord> s_Current;
    ImVector<CQAutomation::ItemRecord> s_Previous;

    // Bu karede ItemAdd ile kaydedilen dikdörtgenler (kimlik -> rect)
    ImGuiStorage s_AddIndex;          // id -> s_AddRects içindeki indeks + 1
    ImVector<ImRect> s_AddRects;

    void CopyTruncated(char* dst, size_t dstSize, const char* src)
    {
        if (!src)
        {
            dst[0] = 0;
            return;
        }
        size_t len = strlen(src);
        if (len >= dstSize)
            len = dstSize - 1;
        memcpy(dst, src, len);
        dst[len] = 0;
    }

    // "Play##toolbar" -> "Play", "###CodeEditor" -> ""
    size_t VisibleLength(const char* label)
    {
        const char* hash = strstr(label, "##");
        return hash ? (size_t)(hash - label) : strlen(label);
    }
}

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*)
{
    if (!s_Enabled || id == 0)
        return;
    int slot = s_AddIndex.GetInt(id, 0);
    if (slot > 0)
    {
        s_AddRects[slot - 1] = bb;
    }
    else
    {
        s_AddRects.push_back(bb);
        s_AddIndex.SetInt(id, s_AddRects.Size);
    }
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags)
{
    if (!s_Enabled || !label || !label[0])
        return;

    CQAutomation::ItemRecord record;
    record.ID = id;
    // Dikdörtgen kare sonunda kimlikten çözülür (bazı widget'larda ItemInfo, ItemAdd'den önce gelir)
    if (ctx->LastItemData.ID == id)
    {
        record.Min = ctx->LastItemData.Rect.Min;
        record.Max = ctx->LastItemData.Rect.Max;
    }
    CopyTruncated(record.Label, sizeof(record.Label), label);
    CopyTruncated(record.Window, sizeof(record.Window), ctx->CurrentWindow ? ctx->CurrentWindow->Name : "");
    s_Current.push_back(record);
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...)
{
}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id)
{
    for (const auto& r : s_Previous)
        if (r.ID == id)
            return r.Label;
    return nullptr;
}

namespace CQAutomation
{
    void EnableItemHooks(bool enabled)
    {
        s_Enabled = enabled;
        if (ImGuiContext* ctx = ImGui::GetCurrentContext())
            ctx->TestEngineHookItems = enabled;
    }

    bool AreItemHooksEnabled()
    {
        return s_Enabled;
    }

    void BeginFrame()
    {
        if (!s_Enabled)
            return;
        // Kayıtların dikdörtgenlerini ItemAdd verisinden kesinleştir
        for (auto& r : s_Current)
        {
            int slot = s_AddIndex.GetInt(r.ID, 0);
            if (slot > 0)
            {
                r.Min = s_AddRects[slot - 1].Min;
                r.Max = s_AddRects[slot - 1].Max;
            }
        }
        s_Previous.swap(s_Current);
        s_Current.resize(0);
        s_AddIndex.Clear();
        s_AddRects.resize(0);
        if (ImGuiContext* ctx = ImGui::GetCurrentContext())
            ctx->TestEngineHookItems = true;
    }

    const ItemRecord* FindItem(const char* label, const char* window, int occurrence)
    {
        if (!label)
            return nullptr;
        const size_t wanted = strlen(label);

        // Pass 0: visible text match; pass 1: full label match; pass 2: "##id" suffix match
        // (icon-only buttons: "<glyph>##Settings_Rigidbody" is found by "##Settings_Rigidbody").
        for (int pass = 0; pass < 3; ++pass)
        {
            if (pass == 2 && !(label[0] == '#' && label[1] == '#'))
                break;
            int seen = 0;
            for (const auto& r : s_Previous)
            {
                if (window && window[0] && !strstr(r.Window, window))
                    continue;
                bool match = false;
                if (pass == 0)
                {
                    size_t vis = VisibleLength(r.Label);
                    // Labels often start with an icon glyph + space; accept a suffix match too.
                    match = (vis == wanted && strncmp(r.Label, label, vis) == 0);
                    if (!match && vis > wanted && wanted > 0)
                        match = strncmp(r.Label + (vis - wanted), label, wanted) == 0 && r.Label[vis - wanted - 1] == ' ';
                }
                else if (pass == 1)
                {
                    match = strcmp(r.Label, label) == 0;
                }
                else
                {
                    const size_t len = strlen(r.Label);
                    match = len >= wanted && strcmp(r.Label + (len - wanted), label) == 0;
                }
                if (match && r.Max.x > r.Min.x && r.Max.y > r.Min.y)
                {
                    if (seen == occurrence)
                        return &r;
                    ++seen;
                }
            }
        }
        return nullptr;
    }

    int GetItemCount()
    {
        return s_Previous.Size;
    }

    const ItemRecord* GetItem(int index)
    {
        if (index < 0 || index >= s_Previous.Size)
            return nullptr;
        return &s_Previous[index];
    }
}
