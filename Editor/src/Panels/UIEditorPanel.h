#pragma once

#include "Renderer/RHI/Framebuffer.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <functional>
#include <unordered_map>
#include <memory>
#include <string>

// UI Editor (199, Unreal UMG Designer gibi): ekran üstü (HUD) canvas'lar sahne görünümünde değil burada tasarlanır. Viewport / Game'in
// yanında bir sekme: seçilen canvas kendi çözünürlüğünde (hazır ayarlar, DPI ölçeği, güvenli alan önizlemesi) ayrı bir framebuffer'a
// çizilir; yakınlaştırma / kaydırma, tıklayıp seçme (Hierarchy ve Inspector ile eş), sürükleyip taşıma, kulplarla boyutlandırma,
// çapaların (anchor) gösterimi, ok tuşlarıyla kaydırma, klavye / gamepad gezinme oklarının gösterimi. Palette'ten öğe bırakılır.
// Sekmeler: Designer | Graph (canvas'ın görsel betiği) | Animations (Timeline). Mevcut Hierarchy / Inspector kullanılır.
namespace Conqueror::Editor
{
    class UIEditorPanel
    {
    public:
        UIEditorPanel();

        void SetContext(const std::shared_ptr<Scene>& scene) { m_Context = scene; }
        // Canvas ya da bir UI öğesi (onun canvas'ı açılır)
        bool Open(Entity entityOrCanvas);
        uint64_t GetCanvas() const { return m_Canvas; }
        Entity GetCanvasEntity() const;
        bool IsOpen() const { return m_Open; }
        void Close() { m_Open = false; }
        void Show() { m_Open = true; }

        // ImGui'den önce: canvas'ı framebuffer'a çiz
        void OnRender();
        void OnImGuiRender();

        // Tasarım (canvas pikseli, sol-alt köken)
        glm::ivec2 GetResolution() const { return m_Resolution; }
        void SetResolution(int width, int height);
        void SetSafeZone(int mode) { m_SafeZone = mode; }
        void SetShowNavigation(bool on) { m_ShowNavigation = on; }
        float GetScaleFactor() const;
        Entity PickAt(const glm::vec2& canvasPixel) const;
        // Tıklama: üstteki öğenin içinde bulunduğu seçilebilir (düğme ...) önce, aynı yere yeniden tıklayınca içteki öğe
        Entity PickForClick(const glm::vec2& canvasPixel) const;
        // Seçili öğeyi tasarım pikseli kadar taşı / boyutlandır (handle: 0..7 köşe ve kenarlar, -1 taşı); geri alınabilir
        bool MoveSelected(const glm::vec2& deltaPixels);
        bool ResizeSelected(int handle, const glm::vec2& deltaPixels);
        // Palette'ten öğe ekle: seçili öğenin (yoksa canvas'ın) altına, tasarım pikseline (konum < 0: ortaya)
        Entity AddElement(const std::string& type, const glm::vec2& canvasPixel = { -1.0f, -1.0f });
        std::string LastImagePath() const { return m_LastShot; }
        bool SaveImage(const std::string& path) const;

        std::function<void(Entity)> OnSelect;
        std::function<void(Entity)> OnOpenGraph;
        std::function<void()> OnOpenAnimations;
        std::function<void()> OnDeleteSelected;

    private:
        void DrawToolbar();
        void DrawDesigner();
        void DrawOverlays(ImDrawList* dl, const glm::vec2& imageMin);
        void DrawNavigation(ImDrawList* dl, const glm::vec2& imageMin);
    public:
        glm::vec4 ElementRect(Entity e) const;   // tasarım pikseli: x, y (sol-alt), w, h (son tasarım yerleşiminden)
        // Tasarım yüzeyinin yerleşimini şimdi hesapla (komutlardan sonra, kare beklemeden)
        void Relayout();
    private:
        glm::vec2 ToScreen(const glm::vec2& canvasPixel, const glm::vec2& imageMin) const;
        glm::vec2 ToCanvas(const glm::vec2& screen, const glm::vec2& imageMin) const;
        void BeginEdit(Entity e);
        void SnapshotRects();
        void EndEdit(Entity e, const char* label);

        std::shared_ptr<Scene> m_Context;
        std::shared_ptr<Framebuffer> m_Framebuffer;
        uint64_t m_Canvas = 0;
        bool m_Open = false;
        bool m_Visible = false;
        glm::ivec2 m_Resolution{ 1920, 1080 };
        int m_Preset = 0;
        float m_Dpi = 1.0f;
        int m_SafeZone = 0;               // 0 yok, 1 TV (yazı güvenli alanı %90), 2 telefon çentiği
        bool m_ShowNavigation = false;
        bool m_ShowAnchors = true;
        int m_Tab = 0;                    // 0 Designer, 1 Graph, 2 Animations
        float m_Zoom = 0.5f;
        bool m_Fit = true;
        glm::vec2 m_Pan{ 0.0f };
        // Sürükleme
        int m_DragHandle = -2;            // -2 yok, -1 taşı, 0..7 kulp
        glm::vec2 m_DragLast{ 0.0f };
        std::shared_ptr<Scene> m_EditBefore;
        uint64_t m_EditEntity = 0;
        std::string m_LastShot;
        // Tasarım çözünürlüğündeki yerleşim (UUID -> dikdörtgen). Sahne görünümü / Game aynı karede yerleşimi kendi boyutlarıyla
        // yeniden hesaplar; seçim ve kulplar bu kopyayı kullanır
        std::unordered_map<uint64_t, glm::vec4> m_Rects;
    };
}
