#pragma once

#include <vector>

#include "Core/Events/Event.h"
#include "Core/Time/Timestep.h"
#include "Renderer/RHI/Framebuffer.h"
#include "Renderer/PostProcess.h"
#include "Scene/EditorCamera.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <functional>
#include <memory>

namespace Conqueror::Editor
{
    // Panelin görüntüsü. Boyut değişirken (yeni kare çizilene kadar) önceki kare gerilmez: dikey görüş açısı sabit olduğundan
    // yüksekliğe sığdırılır, fazla genişlik ortadan kırpılır, eksik genişlik koyu kalır. Cisimler bir sonraki karedeki boyutlarında
    // görünür (Play'e basınca / editör açılınca ilk görüntü yatayda gerilip geniş açılı görünüyordu). Öğe her zaman tam boyuttadır
    // (fare, sürükle-bırak hedefi). Doku dikey çevrik
    inline void PanelImage(ImTextureID texture, const glm::vec2& textureSize, const ImVec2& size)
    {
        const float textureAspect = textureSize.y > 0.0f ? textureSize.x / textureSize.y : 0.0f;
        const float shownAspect = size.y > 0.0f ? size.x / size.y : 0.0f;
        if (textureAspect <= 0.0f || shownAspect <= 0.0f || std::fabs(textureAspect - shownAspect) < 1e-3f * shownAspect)
        {
            ImGui::Image(texture, size, ImVec2(0, 1), ImVec2(1, 0));
            return;
        }
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(12, 12, 14, 255));
        const float width = size.y * textureAspect;
        if (width >= size.x)
        {
            const float u = 0.5f * size.x / width;
            dl->AddImage(texture, pos, ImVec2(pos.x + size.x, pos.y + size.y), ImVec2(0.5f - u, 1.0f), ImVec2(0.5f + u, 0.0f));
        }
        else
        {
            const float x0 = pos.x + (size.x - width) * 0.5f;
            dl->AddImage(texture, ImVec2(x0, pos.y), ImVec2(x0 + width, pos.y + size.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }
    }

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
        PathTracing,          // Unreal "Path Tracing" görünüm kipi: ilerleyen GPU ışın izleme (Renderer/PathTracer.h)
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
        ImVec2 ViewportMin{ 0.0f, 0.0f }, ViewportMax{ 0.0f, 0.0f };   // ekran koordinatı (araçların sahne görünümü katmanları için)
    };
    // true dönerse araç fareyi kullandı (seçim/gizmo o karede devre dışı)
    using ViewportToolHook = std::function<bool(const ViewportToolContext&)>;

    class ViewportPanel
    {
    public:
        // additive: Ctrl/Shift ile tıklandı (seçime ekle/çıkar)
        using SelectionCallback = std::function<void(Entity entity, bool additive)>;

        static constexpr int kGizmoRect = -2;   // Rect Tool (T): ImGuizmo dışı, 2D görünümde tutamaçlar

        ViewportPanel();
        glm::vec2 WorldToScreen2D(const glm::vec2& world) const;
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
        PostProcessStack& GetPostProcessStack() { return m_PostStack; }

        // Seçili nesneleri kadraja al (F)
        void FrameSelected();
        // Viewport içi fare konumundan zemine (3D: y=0, 2D: z=0) ışın; görünmüyorsa odak noktası
        glm::vec3 DropPointOnGround(const glm::vec2& viewportLocalMouse);
        // Ekran noktasından (ImGui ekran koordinatı) nesne seç; isabet yoksa boş Entity
        Entity PickAt(const ImVec2& screenPos);
        // Create menüsü girişini ekran noktasındaki yüzeye yerleştir (Viewport'a bırakmanın aynısı, önizlemesiz; otomasyon da kullanır)
        Entity PlaceCreateEntry(const std::string& id, const ImVec2& screenPos);
        // Sürükleme karesi: önizlemeyi oluştur / imlecin altındaki yüzeye taşı; delivered: bırakıldı (yerleşir, seçilir, geri alınabilir).
        // Bir kare çağrılmazsa önizleme kaldırılır (sürükleme görünümden çıktı / iptal)
        void UpdatePlacementDrag(const std::string& id, const ImVec2& screenPos, bool delivered);

        static const char* GetShadingName(ViewportShading shading);

    private:
        // Create menüsünden sürükleyip yerleştirme (Unreal Place Actors gibi): imlecin altındaki yüzey noktası ve normali (önizleme
        // nesnesi sayılmaz); nesne sınır kutusu yüzeye oturacak şekilde konur
        bool PlacementPoint(const ImVec2& screenPos, glm::vec3& point, glm::vec3& normal);
        void PlaceOnSurface(Entity e, const glm::vec3& point, const glm::vec3& normal);
        void ClearPlacementPreview(bool destroy);

        void DrawToolbar();
        void DrawOverlays();
        void DrawCanvasBounds(ImDrawList* drawList);
        void DrawSelectionOutline(ImDrawList* drawList);
        void DrawGizmo();
        void DrawViewCube();
        void HandlePicking();
        void Handle2DDrag();
        glm::vec2 ScreenToWorld2D(const ImVec2& screen) const;

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

        // 2D görünümde sprite'ı doğrudan sürükleme / Rect Tool
        enum class Drag2DMode { None, Pending, Move, Resize };
        struct Drag2DItem
        {
            uint64_t Id = 0;
            TransformComponent Old;
            glm::vec2 OldSize{ 0.0f };
            glm::vec2 StartWorld{ 0.0f };
            glm::mat4 ParentInverse{ 1.0f };
        };
        struct Drag2DState
        {
            Drag2DMode Mode = Drag2DMode::None;
            Entity Target;
            ImVec2 Start{ 0, 0 };
            int Handle = -1;
            glm::vec2 StartMin{ 0.0f }, StartMax{ 0.0f }, AxisX{ 1.0f, 0.0f }, AxisY{ 0.0f, 1.0f }, WorldScale{ 1.0f }, GrabOffset{ 0.0f };
            bool UsesSize = false;
            std::vector<Drag2DItem> Items;
        };
        Drag2DState m_Drag2D;

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
        ViewportShading m_AppliedShading = ViewportShading::Shaded;   // son karedeki kip (değişince zamansal geçmiş yeniden başlar)
        void ApplyViewportSize(const glm::vec2& size);
        void RenderView(Timestep ts, bool updateScene);
        EditorViewOptions m_AppliedViewOptions;   // son karede sahneye verilen (değişince zamansal geçmiş yeniden başlar)
        bool m_ViewOptionsApplied = false;
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
        bool m_MouseDownInView[3] = { false, false, false };
        std::vector<uint32_t> m_PrevSelection;
        // Create menüsünden sürüklenen girişin önizlemesi (yerleştirilene kadar geri alma kaydına girmez)
        std::string m_PlaceId;
        std::vector<Entity> m_PlaceRoots;
        std::vector<uint32_t> m_PlaceIgnore;
        int m_PlaceSeenFrame = -10;                     // önceki karenin seçimi (seçimi kalkan nesneyi bulmak için)
        std::vector<std::pair<uint32_t, int>> m_DeselectedGizmos;  // seçimi kalkan nesne, TAA'ya hareketli bildirilecek kare sayısı   // sol / sağ / orta tuş görünümün içinde basıldı (fare gezinmesi yalnız o zaman)

        // Bu görünümün HDR hedefleri + post-process zinciri
        PostProcessStack m_PostStack;
    };
}
