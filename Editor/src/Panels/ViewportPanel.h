#pragma once

#include <vector>

#include "Core/Events/Event.h"
#include "Core/Time/Timestep.h"
#include "Renderer/RHI/Framebuffer.h"
#include "Renderer/PostProcess.h"
#include "Scene/EditorCamera.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <functional>
#include <memory>

namespace Conqueror::Editor
{
    class StatsPanel;

    // Görünüm gölgelendirme modu (Unity "Draw Mode" karşılığı)
    enum class ViewportShading : int
    {
        Shaded = 0,
        Wireframe,
        Unlit,
        Normals,
        LightingOnly,
        MetallicRoughness,
        AmbientOcclusion,
        Count
    };

    // Viewport'a bağlanan düzenleme araçları (ör. Terrain fırçası) için bağlam
    struct ViewportToolContext
    {
        glm::vec3 RayOrigin{ 0.0f };
        glm::vec3 RayDirection{ 0.0f, 0.0f, -1.0f };
        bool Hovered = false;
        bool MouseDown = false;       // sol tuş basılı (Alt hariç)
        bool MouseClicked = false;
        bool MouseReleased = false;
        bool Shift = false, Ctrl = false;
        float DeltaTime = 0.0f;
        ImDrawList* DrawList = nullptr;
        std::function<bool(const glm::vec3& world, ImVec2& screen)> WorldToScreen;
    };
    // true dönerse araç fareyi kullandı (seçim/gizmo o karede devre dışı)
    using ViewportToolHook = std::function<bool(const ViewportToolContext&)>;

    class ViewportPanel
    {
    public:
        // additive: Ctrl/Shift ile tıklandı (seçime ekle/çıkar)
        using SelectionCallback = std::function<void(Entity entity, bool additive)>;

        ViewportPanel();
        ~ViewportPanel() = default;

        void SetContext(const std::shared_ptr<Scene>& scene) { m_Context = scene; }
        void SetSelectedEntity(Entity entity) { m_SelectedEntity = entity; }
        void SetSelectionCallback(SelectionCallback callback) { m_SelectionCallback = std::move(callback); }
        void SetStatsPanel(StatsPanel* stats, bool* showStats) { m_StatsPanel = stats; m_ShowStats = showStats; }
        void SetToolHook(ViewportToolHook hook) { m_ToolHook = std::move(hook); }

        void OnUpdate(Timestep ts);
        void OnImGuiRender();
        void OnEvent(Event& e);

        glm::vec2 GetViewportSize() const { return m_ViewportSize; }
        const glm::vec2* GetViewportBounds() const { return m_ViewportBounds; }
        bool IsViewportHovered() const { return m_ViewportHovered; }
        // Sekme son karede görünür müydü (Play/Stop odak geçişi için)
        bool IsTabVisible() const { return m_TabVisible; }
        std::shared_ptr<Framebuffer> GetFramebuffer() const { return m_Framebuffer; }
        EditorCamera& GetEditorCamera() { return m_EditorCamera; }

        int GetGizmoType() const { return m_GizmoType; }
        void SetGizmoType(int type) { m_GizmoType = type; }

        ViewportShading GetShading() const { return m_Shading; }
        void SetShading(ViewportShading shading) { m_Shading = shading; }
        EditorViewOptions& GetViewOptions() { return m_ViewOptions; }

        // Seçili nesneleri kadraja al (F)
        void FrameSelected();
        // Viewport içi fare konumundan zemine (3D: y=0, 2D: z=0) ışın; görünmüyorsa odak noktası
        glm::vec3 DropPointOnGround(const glm::vec2& viewportLocalMouse);
        // Ekran noktasından (ImGui ekran koordinatı) nesne seç; isabet yoksa boş Entity
        Entity PickAt(const ImVec2& screenPos);

        static const char* GetShadingName(ViewportShading shading);

    private:
        void DrawToolbar();
        void DrawOverlays();
        void DrawCanvasBounds(ImDrawList* drawList);
        void DrawSelectionOutline(ImDrawList* drawList);
        void DrawGizmo();
        void DrawViewCube();
        void HandlePicking();

        // Dünya uzayında çizgi -> ekran (yakın düzlem kırpmalı)
        void DrawWorldLine(ImDrawList* drawList, const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness) const;

    private:
        std::shared_ptr<Conqueror::Scene> m_Context;
        std::shared_ptr<Framebuffer> m_Framebuffer;
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        glm::vec2 m_ViewportBounds[2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f } };

        EditorCamera m_EditorCamera;
        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        bool m_TabVisible = false;

        // Gizmo
        int m_GizmoType = 7; // ImGuizmo::OPERATION::TRANSLATE
        // UI öğesi sürükleme: başlangıç dünya konumu ve seçili öğelerin başlangıç AnchoredPosition'ları
        bool m_UIDragActive = false;
        glm::vec3 m_UIDragStartWorld{ 0.0f };
        std::vector<std::pair<uint32_t, glm::vec2>> m_UIDragStartAnchors;
        Entity m_SelectedEntity;

        // Görünüm seçenekleri
        ViewportShading m_Shading = ViewportShading::Shaded;
        EditorViewOptions m_ViewOptions;
        bool m_ShowSelectionOutline = true;
        bool m_ShowCanvasBounds = true;
        bool m_ShowViewCube = true;
        bool m_AudioMuted = false;
        float m_SavedMasterVolume = 1.0f;

        // Stats katmanı
        StatsPanel* m_StatsPanel = nullptr;
        bool* m_ShowStats = nullptr;
        bool m_StatsExpanded = false;

        // Seçim (tıklama ile)
        SelectionCallback m_SelectionCallback;
        ViewportToolHook m_ToolHook;
        bool m_ToolConsumed = false;
        bool m_PickPending = false;
        ImVec2 m_PickStart{ 0.0f, 0.0f };
        bool m_ImageHovered = false;

        // Bu görünümün HDR hedefleri + post-process zinciri
        PostProcessStack m_PostStack;
    };
}
