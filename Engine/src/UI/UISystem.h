#pragma once

#include "Core/Base/Base.h"
#include "Scene/Entity.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Conqueror
{
    class Scene;

    // Bir karelik UI girdisi (piksel, sol-alt köken)
    struct UIInputState
    {
        bool Valid = false;                 // imleç görünüm içinde
        glm::vec2 MousePosition{ 0.0f };
        bool MouseDown = false;
        bool MousePressed = false;
        bool MouseReleased = false;
        float Scroll = 0.0f;
        std::vector<uint32_t> Characters;   // yazılan karakterler (UTF-32)
        bool Backspace = false, Delete = false, Enter = false, Escape = false, Tab = false;
        bool Left = false, Right = false, Home = false, End = false;
        bool Up = false, Down = false, Space = false;   // klavye gezinmesi (gamepad UISystem içinde okunur)
    };

    struct UIEvent
    {
        std::string Name;     // OnClick/OnValueChanged adı (boşsa varlık adı)
        std::string Type;     // "Click", "ValueChanged", "Submit"
        Entity Source;
        float Value = 0.0f;
    };

    // Canvas Render Target çizim komutu (sol üst köken, piksel)
    struct CanvasCommand
    {
        enum class Kind { Clear, Rect, Text, Image } Type = Kind::Clear;
        glm::vec4 Color{ 1.0f };
        glm::vec4 Box{ 0.0f };   // x, y, w, h
        std::string Text;        // yazı ya da doku yolu
        float Size = 24.0f;
    };

    // Unity UGUI karşılığı: RectTransform yerleşimi, Canvas Scaler, widget'lar,
    // olay sistemi ve çizim (ekran üstü / dünya uzayı / editör).
    class CQ_API UISystem
    {
    public:
        // Yerleşim + girdi (ekran üstü canvas'lar). screenSize: piksel.
        static void Update(Scene& scene, const glm::vec2& screenSize, const UIInputState& input, float dt);
        // Ekran üstü canvas'ları şu anki framebuffer'a çiz (post-process sonrası)
        static void RenderScreenSpace(Scene& scene, const glm::vec2& screenSize);
        // Dünya uzayı canvas'ları (ve editörde ekran üstü olanları sahne içinde) çiz
        static void RenderWorldSpace(Scene& scene, const glm::mat4& viewProjection, bool includeOverlayCanvases, const glm::vec2& editorScreenSize);
        // Editör: ekran üstü canvas'ları sahne görünümünde ızgaradan sonra çiz (ızgara UI'yi örtmesin)
        static void RenderEditorOverlayCanvases(Scene& scene, const glm::mat4& viewProjection, const glm::vec2& editorScreenSize,
                                                bool depthTest = false);
        // Editör: yerleşimi hesapla ve UI varlıklarının WorldTransform'unu sahne görünümü için ayarla
        static void UpdateEditorTransforms(Scene& scene, const glm::vec2& referenceScreenSize);

        // Olaylar (kare başına temizlenir)
        static const std::vector<UIEvent>& GetEvents();
        static bool WasClicked(const std::string& nameOrEntity);
        static bool IsPointerOverUI();
        // Ekran klavyesi (Steam Deck / kumanda / dokunmatik): 0 otomatik, 1 her zaman, 2 kapalı (cvar ui.OnScreenKeyboard)
        static void SetOnScreenKeyboardMode(int mode);
        static int GetOnScreenKeyboardMode();
        static bool IsOnScreenKeyboardOpen();
        // Otomasyon: etiketli tuşun ekran dikdörtgeni (x, y, w, h; sol-alt köken); klavye kapalıysa false
        static bool GetOnScreenKeyRect(const std::string& label, glm::vec4& rect);
        // Klavye / gamepad gezinmesi: seçili öğe
        static Entity GetSelected(Scene& scene);
        static void SetSelected(Scene& scene, Entity element);

        // Canvas pikselinden dünya matrisi (editör sahne görünümü)
        static glm::mat4 GetCanvasWorldMatrix(Entity canvas, const glm::vec2& screenSize);
        static float GetCanvasScaleFactor(Entity canvas, const glm::vec2& screenSize);
        static glm::vec2 GetCanvasSize(Entity canvas, const glm::vec2& screenSize);

        // Hazır UI öğeleri (Hierarchy menüsü, scriptler): parent bir canvas ya da UI öğesi olmalı
        static Entity CreateCanvas(Scene& scene, const std::string& name = "Canvas");
        static Entity CreateElement(Scene& scene, const std::string& type, Entity parent);
        // Sahne görünümünde UI öğesini taşı (dünya deltası -> AnchoredPosition)
        static void MoveElementWorld(Entity element, const glm::vec3& worldDelta, const glm::vec2& screenSize);

        // Canvas Render Target'a çiz (framebuffer: hedefin FBO'su)
        static void DrawCanvas(uint32_t framebuffer, uint32_t width, uint32_t height, const std::vector<CanvasCommand>& commands);

        static void Shutdown();

    private:
        static void RenderCanvasesInWorld(Scene& scene, const glm::mat4& viewProjection, bool includeWorld, bool includeOverlay, const glm::vec2& editorScreenSize,
                                          bool depthTest = false);
    };
}
